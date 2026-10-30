/* Compile the production hardware adapter with host MMIO/copy shims.
 * These tests inject faults; they do not model tensor arithmetic or RTL. */
#include "memory_fixture.h"
#include <vxl/submit_timing.h>
#include <stdbool.h>
typedef vxl_u8 u8;
typedef vxl_u32 u32;
typedef vxl_u64 u64;
#define VXL_INTERNAL_H
struct vxl_device {bool cdma_fastwait_active;struct vxl_engine engine;struct vxl_hunet_ctx hunet;void *gpu,*cdma,*ps;u64 ps_dma;int *ready;u32 cdma_last;bool approved;struct vxl_submit_trace submit_trace;};
static unsigned ack_writes,reset_writes;static u64 adapter_ns;static struct vxl_device device;
static int dma_emulate,dma_running;static unsigned dma_polls;
static u32 readl(const void *p){if(dma_emulate&&p==(u8 *)device.cdma+4){adapter_ns+=5;if(!dma_running)return 2;if(!dma_polls++)return 0;return 0x1002;}return *(const u32 *)p;}
static void writel(u32 value,void *p){if(dma_emulate&&p==(u8 *)device.cdma+4)return;if(dma_emulate&&p==(u8 *)device.cdma+0x28){dma_running=1;dma_polls=0;}if(p==(u8 *)device.gpu+8){if(value==2)++ack_writes;if(value!=1&&value!=2)++reset_writes;}*(u32 *)p=value;}
static void dma_wmb(void){}
static void dma_rmb(void){}
static u64 ktime_get_ns(void){return adapter_ns;}
static void usleep_range(unsigned a,unsigned b){if(dma_emulate)adapter_ns+=200;(void)a;(void)b;}
static int gpiod_get_value_cansleep(int *p){return *p;}
static void *u64_to_user_ptr(u64 p){return (void *)(uintptr_t)p;}
static int copy_from_user(void *dst,const void *src,u64 n){memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,u64 n){memcpy(dst,src,n);return 0;}
#include "../../driver/vxl_hw.c"
static int real_adapter_poll(void *p,struct vxl_event *e){struct fixture *f=p;int rc;device.engine=f->e;device.hunet=f->c;device.engine.hunet=&device.hunet;device.hunet.engine=&device.engine;adapter_ns=fns(f);rc=poll_gpu(&device,e);f->c=device.hunet;f->c.engine=&f->e;return rc;}
#include "../../generated/hunet_fused/catalog.h"
#include "../../generated/hunet_fused/identity.h"
static void setup(struct fixture*f,struct vxl_fused_release*r){
 init_fixture(f);seal_fixture(f);memset(r,0,sizeof(*r));r->wire_bytes=f->r.program[0].wire_bytes;r->ro_bytes=f->r.program[0].ro_bytes;
 memcpy(r->wire_sha,f->r.program[0].wire_sha,32);memcpy(r->ro_sha,f->r.program[0].ro_sha,32);memcpy(r->binding.descriptors_sha256,vxl_fused_descriptors_sha256,32);
 f->c.fused.release=r;f->c.program_id=9;f->c.active=1;f->c.next_sequence=1;f->c.hybrid.enabled=1;f->c.hybrid.owner=f->e.owner;f->c.hybrid.generation=f->e.generation;
 assert(!vxl_fused_restore_descriptors(&f->c));assert(!vxl_hunet_guards(&f->c,1));assert(!vxl_fused_begin(&f->c));
}
static void launch(struct fixture*f,struct vxl_request*r){
 assert(!vxl_hunet_stage_args(&f->c,vxl_fused_catalog_job(1),128));*r=req();r->handle=f->c.handles[0];r->handle2=f->c.handles[3];r->trust_id=9;r->deadline_ms=60000;
 assert(!vxl_engine_call(&f->e,123,VXL_SUBMIT,r));assert(f->starts==1&&f->c.evidence.stage_id==0&&f->c.evidence.op==15);
}
static void owners(struct fixture*f){
 vxl_u32 n=vxl_fused_metadata[0].trace_elements,active=(n+3)/4,offset=0;if(active>4)active=4;
 for(vxl_u32 c=0;c<active;c++)for(vxl_u32 j=0;j<n/active+(c<n%active);j++){vxl_u32 v=c+1;memcpy(map(f,0x86d12e40+4*offset++,4),&v,4);}
}
static void run_case(int fail){
 struct fixture f;struct vxl_fused_release release;struct vxl_ops ops=fops;struct vxl_request r;struct vxl_hunet_delivery delivery;struct vxl_hunet_job_evidence_v1 ev;u32 regs[64]={0},cdma[64]={0};int ready=1;
 setup(&f,&release);ops.poll=real_adapter_poll;f.e.ops=&ops;launch(&f,&r);owners(&f);
 device.gpu=regs;device.cdma=cdma;device.ready=&ready;device.cdma_last=0x1002;ack_writes=reset_writes=0;
 regs[3]=2;regs[11]=1;regs[6]=4000000000u;regs[10]=57;regs[13]=1;
 for(unsigned c=0;c<4;c++)memset(f.ps+0x2088+256*c,0,4);
 if(fail==1)f.ps[0x2000]^=1;
 if(fail==2)*map(&f,0x86d12e40,4)=0;
 if(fail==3)regs[6]=1;
 if(fail==4){regs[3]=0x100;f.clock_ns=f.c.evidence.execution_deadline_ns+1;}
 if(fail==5)device.cdma_last=0x4772;
 assert(!vxl_engine_poll(&f.e));assert(!reset_writes&&f.c.next_sequence==1);
 if(fail){assert(f.e.quarantined&&f.e.job.pinned&&!ack_writes);assert(f.c.evidence.result<0&&f.c.evidence.elapsed_raw==57);vxl_engine_close(&f.e,123);assert(f.e.owner&&f.e.objects[0].handle);}
 else{
  assert(ack_writes==1&&f.c.fused.acked&&f.e.job.state==VXL_COMPLETED);assert(f.c.evidence.valid_flags&VXL_HUNET_VALID_OWNER);
  r=req();r.user_ptr=(uintptr_t)&ev;r.length=256;r.job_id=f.e.job.id;f.output_fault=1;assert(vxl_engine_call(&f.e,123,VXL_HUNET_GET_EVIDENCE,&r)==-EFAULT);assert(f.c.next_sequence==1);
  f.output_fault=0;assert(!vxl_engine_call(&f.e,123,VXL_HUNET_GET_EVIDENCE,&r));vxl_hunet_delivery_capture(&f.c,VXL_HUNET_GET_EVIDENCE,&delivery);assert(f.c.next_sequence==1);
  vxl_hunet_delivery_finish(&f.c,&delivery);assert(f.c.next_sequence==97&&f.c.hybrid.gpu_jobs==1&&f.c.hybrid.logical_jobs==96);
  vxl_hunet_delivery_finish(&f.c,&delivery);assert(f.c.next_sequence==97&&f.c.hybrid.gpu_jobs==1);
 }
 destroy_fixture(&f);
}
static void query_and_metadata(void){
 struct fixture f;struct vxl_fused_release release;struct vxl_fused_binding_v3 binding;struct vxl_request r;struct vxl_prepared p;vxl_u32 words[32];setup(&f,&release);
 r=req();r.user_ptr=(uintptr_t)&binding;r.length=512;assert(!vxl_engine_call(&f.e,123,VXL_HYBRID_GET_BINDING,&r));assert(binding.v2.v1.generation==f.e.generation);
 memcpy(words,vxl_fused_catalog_job(1),128);words[6]++;assert(!vxl_hunet_stage_args(&f.c,words,128));assert(vxl_hunet_prepare(&f.c,&p,9));
 memcpy(words,vxl_fused_catalog_job(1),128);words[28]++;assert(!vxl_hunet_stage_args(&f.c,words,128));assert(vxl_hunet_prepare(&f.c,&p,9));destroy_fixture(&f);
}
int main(void){query_and_metadata();for(int fail=0;fail<=5;fail++)run_case(fail);puts("FUSED_PRODUCTION_ADAPTER_SNAPSHOT_ACK_DELIVERY_FAULTS_PASS_NOT_BOARD");}

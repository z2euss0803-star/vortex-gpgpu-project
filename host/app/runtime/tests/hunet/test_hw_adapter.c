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
int main(int argc,char **argv){
 struct fixture *f=calloc(1,sizeof(*f));struct vxl_ops ops=fops;struct vxl_request r;struct vxl_hunet_job_evidence_v1 out;u32 regs[64]={0},cdma[64]={0},exits[4]={17,29,41,53};int ready=1;unsigned i;bool deadline=argc>1&&!strcmp(argv[1],"deadline");
 assert(f);init_fixture(f);seal_fixture(f);f->c.active=1;f->c.next_sequence=1;ops.poll=real_adapter_poll;f->e.ops=&ops;assert(!vxl_hunet_stage_args(&f->c,vxl_hunet_catalog_job(1),128));
 r=req();r.handle=f->c.handles[0];r.handle2=f->c.handles[3];r.trust_id=6;r.deadline_ms=60000;assert(!vxl_engine_call(&f->e,123,VXL_SUBMIT,&r));assert(f->starts==1);
 device.gpu=regs;device.cdma=cdma;device.ready=&ready;device.cdma_last=0x1002;
 {static struct vxl_submit_time traced;device.submit_trace.active=&traced;f->c.submit_time=&traced;}
 regs[3]=deadline?0x100:2;regs[8]=deadline?7:0;regs[9]=deadline?3:0;regs[11]=deadline?f->e.job.jobs_before:f->e.job.jobs_before+1;regs[6]=4000000000u;regs[10]=deadline?772:587;regs[13]=1;
 for(i=0;i<4;i++)memcpy(f->ps+0x2088+i*0x100,&exits[i],4);
 if(deadline)f->clock_ns=f->c.evidence.execution_deadline_ns+1;else f->ps[0x2000]^=1;
 assert(!vxl_engine_poll(&f->e));assert(f->e.quarantined&&f->e.job.pinned);assert(!ack_writes&&!reset_writes);assert(!vxl_hunet_get_evidence(&f->c,r.job_id,&out));
 assert(out.status==regs[3]&&out.pending_w==regs[8]&&out.pending_r==regs[9]&&out.jobs_after==regs[11]);
 assert(out.watchdog==4000000000u&&out.elapsed_raw==regs[10]&&out.cdma_status==0x1002);assert(!memcmp(out.exitcode,exits,16));
 assert((out.valid_flags&(VXL_HUNET_VALID_HW_SNAPSHOT|VXL_HUNET_VALID_EXIT_SNAPSHOT))==24);assert(!(out.valid_flags&VXL_HUNET_VALID_CRT));assert(out.done_ns==(deadline?0:adapter_ns));
 assert(out.software_state==VXL_QUARANTINED&&out.result==(deadline?-ETIMEDOUT:-EIO));
 assert(device.submit_trace.active->state_poll_calls==1&&!device.submit_trace.active->flags);
 assert(device.submit_trace.active->gpu_done_observed_ns==(deadline?0:adapter_ns));
 {struct vxl_submit_time *t=device.submit_trace.active;u64 prior=adapter_ns;t->guard_depth=1;dma_emulate=1;dma_running=0;assert(!dma_block(&device,0x100ff000,0x57fff0000ULL,64));assert(t->cdma_calls==1&&t->cdma_bytes==64&&t->guard_cdma_calls==1&&t->guard_cdma_bytes==64);assert(t->cdma_pause_calls==1&&t->cdma_sleep_ns==200&&t->guard_cdma_sleep_ns==200&&t->cdma_ns>=200&&adapter_ns>prior);t->guard_depth=0;dma_emulate=0;}
 puts(deadline?"REAL_ADAPTER_DEADLINE_SNAPSHOT_NO_ACK_RESET_PASS":"REAL_ADAPTER_CRT_GUARD_FAILURE_SNAPSHOT_NO_ACK_RESET_PASS");destroy_fixture(f);free(f);return 0;
}

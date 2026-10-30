/* Production vxl_hw.c with MMIO/CDMA boundaries only. NOT a board/RTL test. */
#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/submit_timing.h>
#include <vxl/s8.h>
#include <vxl/s8_runtime.h>
#include <vxl/s8_timing.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef vxl_u8 u8;typedef vxl_u32 u32;typedef vxl_u64 u64;
#define VXL_INTERNAL_H
struct vxl_device {bool cdma_fastwait_active;struct vxl_engine engine;struct vxl_hunet_ctx hunet;void *gpu,*cdma,*ps;u64 ps_dma;int *ready;u32 cdma_last;bool approved;struct vxl_submit_trace submit_trace;};
static struct vxl_device device;static struct vxl_s8_ctx ctx;static struct vxl_s8_program program;
static const u8 frozen_flat[256]={0};
static u32 regs[64],dma_regs[64];static int ready=1;
static unsigned serial,elapsed_read,ack_write,start_write,watchdog_read,acks,starts;
static int wrong_watchdog,dma_emulate,dma_started,dma_polls;static u64 ns;
struct bank {u64 begin;u32 bytes;u8 *data;};static struct bank banks[9];
static u32 readl(const void *p) {
 ++serial;ns+=5;
 if(p==(u8 *)device.gpu+0x28)elapsed_read=serial;
 if(p==(u8 *)device.gpu+0x18){watchdog_read=serial;if(wrong_watchdog)return 17;}
 if(dma_emulate && p==(u8 *)device.cdma+4){if(!dma_started)return 2;return dma_polls++?0x1002:0;}
 return *(const u32 *)p;
}
static void writel(u32 value,void *p) {
 ++serial;ns+=5;
 if(dma_emulate && p==(u8 *)device.cdma+4)return;
 if(dma_emulate && p==(u8 *)device.cdma+0x28){dma_started=1;dma_polls=0;}
 if(p==(u8 *)device.gpu+8) {
  if(value==1){start_write=serial;starts++;}
  else if(value==2){ack_write=serial;acks++;regs[3]=0;}
  else assert(!"unexpected reset/control write");
 }
 *(u32 *)p=value;
}
static void dma_wmb(void){}static void dma_rmb(void){}
static u64 ktime_get_ns(void){return ++ns;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;ns+=200;}
static int gpiod_get_value_cansleep(int *p){return *p;}
static void *u64_to_user_ptr(u64 p){return (void *)(uintptr_t)p;}
static int copy_from_user(void *a,const void *b,u64 n){memcpy(a,b,n);return 0;}
static int copy_to_user(void *a,const void *b,u64 n){memcpy(a,b,n);return 0;}
#include "../../driver/vxl_hw.c"
#include "../../driver/vxl_s8.c"
static u64 fake_ns(void *p){(void)p;return ktime_get_ns();}
static int bank_rw(void *p,u64 a,void *data,u64 n,int write) {
 (void)p;ns+=20;
 for(unsigned i=0;i<9;++i)if(a>=banks[i].begin && a+n<=banks[i].begin+banks[i].bytes) {
  if(write)memcpy(banks[i].data+a-banks[i].begin,data,n);else memcpy(data,banks[i].data+a-banks[i].begin,n);
  return 0;
 }
 return -ERANGE;
}
static u64 get(FILE *f,unsigned n){u64 value=0;for(unsigned i=0;i<n;++i){int c=fgetc(f);assert(c!=EOF);value|=(u64)c<<(8*i);}return value;}
static void init(void) {
 memset(&device,0,sizeof(device));memset(&ctx,0,sizeof(ctx));memset(regs,0,sizeof(regs));memset(dma_regs,0,sizeof(dma_regs));
 serial=elapsed_read=ack_write=start_write=watchdog_read=acks=starts=0;ns=1000;ready=1;wrong_watchdog=dma_emulate=dma_started=dma_polls=0;
 device.gpu=regs;device.cdma=dma_regs;device.ready=&ready;device.approved=true;device.ps_dma=0x10000000;
 vxl_u8 tag[16]={0x55};assert(!vxl_engine_init_profile(&device.engine,&vxl_hw_ops,&device,1,5));
 assert(!vxl_s8_context_init(&ctx,&device.engine,tag));ctx.io=(struct vxl_s8_io){.context=0,.rw=bank_rw,.now_ns=fake_ns};
 assert(!vxl_engine_open(&device.engine,1));vxl_s8_timing_reset(&ctx);
 ctx.job_args=(struct vxl_s8_job_v1){.case_id=1,.k=0,.core_mask=1,.generation=device.engine.generation,.sequence=1,.run_id={1},.binding_tag={0x55}};
 ctx.program=&program;device.engine.job.id=1;device.engine.job.deadline=60000;
 struct vxl_event start={.kind=VXL_START,.id=1};assert(!vxl_job_step(&device.engine.job,&start));
 regs[0]=0x56580204;regs[1]=0x40404;regs[13]=1;dma_regs[1]=0x1002;
 FILE *f=fopen("build/s8/hw-fixture.bin","rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"S8HWFIX1",8));
 for(unsigned i=0;i<9;++i){banks[i].begin=get(f,8);banks[i].bytes=get(f,4);banks[i].data=malloc(banks[i].bytes);assert(banks[i].data);assert(fread(banks[i].data,1,banks[i].bytes,f)==banks[i].bytes);}assert(fgetc(f)==EOF);fclose(f);
 program=(struct vxl_s8_program){.flat=frozen_flat,.flat_bytes=256,.entry=0x80000000,.layout={.runtime_bytes=9216,.tls_base=0x80002000,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.static_stack_bytes=4096,.objects_count=53}};
 for(unsigned i=0;i<8;++i)program.layout.regions[i]=(struct vxl_s8_region){banks[i].begin,banks[i].bytes};
}
static void cleanup(void){for(unsigned i=0;i<9;++i)free(banks[i].data);}
static int start(void) {struct vxl_prepared p={.program_id=11,.code_gpu=0x80000000,.args_gpu=0x80100040};return start_gpu(&device,&p,&device.engine.job.jobs_before);}
static void terminal(void){regs[3]=0x502;regs[10]=123456;regs[11]=1;}
int main(void) {
 assert(vxl_s8_attach(&device)==-ENODEV && !device.engine.s8);
 init();assert(!start());assert(starts==1 && regs[6]==4000000000u && watchdog_read<start_write);terminal();
 {struct vxl_event stale={.idle=1};assert(!vxl_s8_hw_snapshot(&device,&stale));assert(stale.idle==0&&stale.status==0x502&&ctx.hw.event.idle==0);}
 assert(!vxl_engine_poll(&device.engine));assert(acks==1 && elapsed_read<ack_write);
 assert(ctx.hw.elapsed_raw==123456 && ctx.hw.elapsed_valid && ctx.hw.pre_ack && ctx.hw.elapsed_address==0xa0000028ULL);
 assert(ctx.capture_pending && device.engine.job.state==VXL_COMPLETED);
 assert(ctx.times.valid_events&(1ULL<<VXL_S8_GPU_START));assert(ctx.times.metrics[VXL_S8_STATE_POLL].calls==1);cleanup();
 init();wrong_watchdog=1;assert(start()<0 && starts==0 && acks==0);cleanup();
 init();assert(!start());terminal();regs[10]=0;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.quarantined && ctx.hw.elapsed_raw==0 && ctx.error_class==VXL_S8_TIMING_INVALID);cleanup();
 init();assert(!start());terminal();regs[8]=1;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.job.state==VXL_RUNNING);regs[8]=0;assert(!vxl_engine_poll(&device.engine));assert(acks==1 && device.engine.job.state==VXL_COMPLETED);cleanup();
 init();assert(!start());terminal();regs[11]=2;assert(!vxl_engine_poll(&device.engine));assert(!acks);ns=60000000001ULL;assert(!vxl_engine_poll(&device.engine));assert(device.engine.quarantined && !acks && ctx.hw.elapsed_raw==123456);cleanup();
 init();assert(!start());terminal();banks[8].data[8+2*256]=1;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.quarantined && ctx.hw.exitcode[2]==1 && ctx.hw.elapsed_raw==123456);cleanup();
 init();assert(!start());terminal();banks[4].data[0]^=1;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.quarantined && ctx.hw.elapsed_raw==123456);cleanup();
 init();assert(!start());terminal();banks[5].data[64+31*32+16]^=1;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.quarantined && ctx.error_class==VXL_S8_IDENTITY_MISMATCH && ctx.hw.elapsed_raw==123456);cleanup();
 init();assert(!start());terminal();banks[6].data[64+16]=1;assert(!vxl_engine_poll(&device.engine));assert(!acks && device.engine.quarantined && ctx.hw.elapsed_raw==123456);cleanup();
 init();ctx.job_args.case_id=101;ctx.job_args.k=1;
 {struct vxl_s8_result_v1 m={.magic=VXL_S8_RESULT_MAGIC,.version=1,.bytes=256,.case_id=101,.generation=2,.sequence=1,.k=1,.valid_bits=S8_RAW_VALID|S8_DECODED_VALID|S8_STATUS_VALID,.pages_count=24,.status_count=6,.format=1,.primary_elements=64,.primary_accepted=1,.primary_bitmap=0xffffff,.done=VXL_S8_DONE,.detail=1};m.run_id[0]=1;m.binding_tag[0]=0x55;memcpy(banks[4].data+0x3c0,&m,256);}
 assert(!vxl_s8_results_check(&ctx) && ctx.hw.identity_valid && ctx.hw.metadata_valid && ctx.hw.metadata_count==1);
 {struct vxl_s8_result_v1 *m=(struct vxl_s8_result_v1 *)(banks[4].data+0x3c0);ctx.job_args.flags=1;m->flags=1;m->valid_bits|=S8_PHASE_VALID;m->phase_count=12;
 struct vxl_s8_phase_v1 *p=(struct vxl_s8_phase_v1 *)(banks[4].data+0x6c0);p->magic=0x31503853;p->version=1;p->bytes=512;p->valid_mask=4095;p->width=44;p->count=12;
 for(unsigned i=0;i<12;++i){p->events[i].event=i;p->events[i].flags=1;p->events[i].raw_lo=100+i;}
 assert(!vxl_s8_results_check(&ctx));p->events[7].raw_hi=4096;assert(vxl_s8_results_check(&ctx)<0);p->events[7].raw_hi=0;assert(!vxl_s8_results_check(&ctx));}
 banks[4].data[0x3c0+88]^=1;assert(vxl_s8_results_check(&ctx)<0 && !ctx.hw.metadata_valid && !acks);cleanup();
 init();ctx.job_args.case_id=206u<<8;ctx.job_args.k=2;
 {struct vxl_s8_result_v1 m={.magic=VXL_S8_RESULT_MAGIC,.version=1,.bytes=256,.case_id=206u<<8,.generation=2,.sequence=1,.k=2,.valid_bits=S8_RAW_VALID|S8_DECODED_VALID|S8_STATUS_VALID|S8_RECOVERY_RAW_VALID|S8_RECOVERY_DECODED_VALID,.pages_count=24,.status_count=14,.format=1,.primary_elements=64,.primary_accepted=2,.primary_bitmap=0xffffff,.firsterror_event=5,.detail=3,.done=VXL_S8_DONE,.recovery_elements=64};m.run_id[0]=1;m.binding_tag[0]=0x55;memcpy(banks[4].data+0x3c0,&m,256);
 struct vxl_s8_sync sync={.phase_seq=1,.owner_ready=0x20009,.contender_done=0x2000e};memcpy(banks[6].data+64,&sync,64);}
 assert(!vxl_s8_results_check(&ctx) && ctx.hw.metadata_valid);
 banks[6].data[64+4]^=1;assert(vxl_s8_results_check(&ctx)<0 && !ctx.hw.metadata_valid);banks[6].data[64+4]^=1;
 banks[4].data[0x3c0+104]^=1;assert(vxl_s8_results_check(&ctx)<0 && !ctx.hw.metadata_valid);cleanup();
 init();device.cdma_fastwait_active=true;dma_emulate=1;ctx.times.guard_depth=1;
 assert(!dma_block(&device,0x100ff000ULL,0x501000000ULL,64));
 assert(ctx.times.metrics[VXL_S8_CDMA].calls==1 && ctx.times.metrics[VXL_S8_CDMA].bytes==64 && ctx.times.metrics[VXL_S8_GUARD_CDMA].calls==1);
 assert(ctx.times.metrics[VXL_S8_CDMA_SLEEP].calls==1);cleanup();
 for(unsigned id=2;id<=5;++id){init();ctx.job_args.case_id=id;ctx.job_args.core_mask=4;ctx.job_args.owner_warp=3;assert(!start());
  if(id>=4){assert(regs[16]==0x81000040u+11u*0x20000u && regs[17]==2 && regs[18]==2 && regs[19]==8 && regs[21]==16 && regs[20]==(id==4?9u:8u));}
  else assert(regs[20]==0);
  cleanup();}
 puts("S8_REAL_ADAPTER_ORDER_AND_FAILURES_PASS MMIO_SOFTWARE_UNIT_NOT_BOARD");return 0;
}

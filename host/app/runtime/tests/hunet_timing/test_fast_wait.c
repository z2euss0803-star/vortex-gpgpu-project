/* Production CDMA and hardware callbacks with timed MMIO; not board evidence. */
#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <vxl/submit_timing.h>
#include <stdbool.h>
typedef vxl_u8 u8; typedef vxl_u32 u32; typedef vxl_u64 u64;
#define VXL_INTERNAL_H
struct vxl_device {bool cdma_fastwait_active;struct vxl_engine engine;struct vxl_hunet_ctx hunet;void *gpu,*cdma,*ps;u64 ps_dma;int *ready;u32 cdma_last;bool approved;struct vxl_submit_trace submit_trace;};
static struct vxl_device device;
static u64 ticks;
static unsigned polls,complete_at,fail_at,sleeps,relaxes,relax_step,btt,writes;
static int running,old_ioc;
static u32 readl(const void *p){
 ticks+=20;
 if(p==(u8 *)device.cdma+4){
  if(!running)return old_ioc?0x1002:2;
  polls++;
  if(fail_at&&polls>=fail_at)return 0x4772;
  return complete_at&&polls>=complete_at?0x1002:0;
 }
 return *(const u32 *)p;
}
static void writel(u32 v,void *p){
 writes++;
 if(p==(u8 *)device.cdma+4){if(!old_ioc)running=0;return;}
 if(p==(u8 *)device.cdma+0x28){running=1;polls=0;btt++;}
 *(u32 *)p=v;
}
static void dma_wmb(void){} static void dma_rmb(void){}
static u64 ktime_get_ns(void){return ticks;}
static void usleep_range(unsigned a,unsigned b){assert(a==50&&b==100);sleeps++;ticks+=100000;}
static void __attribute__((unused)) fast_relax(void){relaxes++;ticks+=relax_step;}
#define cpu_relax fast_relax
static int gpiod_get_value_cansleep(int *p){return *p;}
static void *u64_to_user_ptr(u64 p){return (void *)(uintptr_t)p;}
static int copy_from_user(void *d,const void *s,u64 n){memcpy(d,s,n);return 0;}
static int copy_to_user(void *d,const void *s,u64 n){memcpy(d,s,n);return 0;}
#include "../../driver/vxl_hw.c"
static u32 cdma_regs[64]; static struct vxl_submit_time row;
static void setup(void){
 memset(&device,0,sizeof(device));memset(cdma_regs,0,sizeof(cdma_regs));memset(&row,0,sizeof(row));
 device.cdma=cdma_regs;device.engine.profile_id=4;device.engine.hunet=&device.hunet;device.hunet.program_id=9;device.submit_trace.active=&row;device.cdma_fastwait_active=true;device.engine.job.state=VXL_READY;
 ticks=100000;polls=sleeps=relaxes=btt=writes=0;running=old_ioc=0;complete_at=3;fail_at=0;relax_step=50;
}
int main(void){
 setup();assert(!dma_block(&device,0x10020000,0x580020000ULL,64));assert(!sleeps&&relaxes==2&&btt==1);
 assert(row.cdma_fast_poll_calls==2&&row.cdma_pause_calls==0&&row.cdma_fast_poll_ns==100);
 assert(row.cdma_fast_poll_ns<=row.cdma_ns);
 assert(cdma_regs[0x24/4]==5&&cdma_regs[0x20/4]==0x80020000);
 setup();complete_at=1;assert(!dma_block(&device,16,32,64)&&!sleeps&&!relaxes);
 setup();complete_at=70;assert(!dma_block(&device,16,32,64));assert(relaxes==64&&sleeps==5);
 setup();complete_at=10;relax_step=2000;assert(!dma_block(&device,16,32,64));assert(relaxes==4&&sleeps==5);
 setup();fail_at=2;assert(dma_block(&device,16,32,64)==-EIO&&!sleeps&&relaxes==1);
 setup();complete_at=0;fail_at=70;assert(dma_block(&device,16,32,64)==-EIO&&sleeps==5);
 setup();old_ioc=1;assert(dma_block(&device,16,32,64)==-EIO&&!btt&&!relaxes);
 setup();complete_at=0;assert(dma_block(&device,16,32,64)==-ETIMEDOUT&&relaxes==64&&sleeps>0);
 setup();assert(dma_block(&device,1ULL<<40,32,64)==-ERANGE&&!writes);
 setup();assert(dma_block(&device,16,32,17)==-EINVAL&&!writes);
 setup();device.engine.profile_id=1;assert(!dma_block(&device,16,32,64)&&sleeps==2&&!relaxes);
 setup();device.hunet.program_id=8;assert(!dma_block(&device,16,32,64)&&sleeps==2&&!relaxes);
 setup();device.submit_trace.active=NULL;device.submit_trace.used=device.submit_trace.capacity=2048;assert(!dma_block(&device,16,32,64)&&!sleeps&&relaxes==2);
 setup();device.cdma_fastwait_active=false;assert(!dma_block(&device,16,32,64)&&sleeps==2&&!relaxes);
 setup();device.cdma_fastwait_active=false;device.engine.job.state=VXL_RUNNING;device.submit_trace.active=NULL;assert(!dma_block(&device,16,32,64)&&!sleeps&&relaxes==2);
 setup();assert(!dma_block(&device,16,32,64));assert(!dma_block(&device,16,32,64));assert(relaxes==4&&!sleeps&&btt==2);
 {
  struct vxl_opt_release release={0};struct vxl_mgmt_row management={.flags=VXL_MGMT_INCOMPLETE};
  setup();device.hunet.program_id=6;device.hunet.opt.release=&release;device.hunet.opt_load_locked=1;
  device.hunet.opt_load_owner=device.engine.owner=7;device.hunet.opt_load_generation=device.engine.generation=3;
  device.hunet.mgmt_row=&management;device.submit_trace.active=NULL;
  complete_at=70;assert(!dma_block(&device,16,32,64));assert(relaxes==64&&sleeps==5);
  assert(management.nested_calls[VXL_MGMT_CDMA_FASTPOLL]==64&&management.nested_calls[VXL_MGMT_CDMA_SLEEP]==5);
  assert(management.nested_calls[VXL_MGMT_CDMA_TOTAL]==1&&management.nested_bytes[VXL_MGMT_CDMA_TOTAL]==64);
  device.hunet.opt_load_generation=4;sleeps=relaxes=0;complete_at=3;
  assert(!dma_block(&device,16,32,64)&&sleeps==2&&!relaxes);
 }
 setup();ticks=~(u64)0-500;assert(!dma_block(&device,16,32,64));
 puts("CDMA_FASTWAIT_REAL_CORE_ADAPTER_BUDGET_FALLBACK_ERRORS_LEGACY_PASS_NOT_BOARD");return 0;
}

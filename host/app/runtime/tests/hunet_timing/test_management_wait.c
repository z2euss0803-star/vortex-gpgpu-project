#include <stdbool.h>
/* Real ioctl wrapper; only kernel primitives and hardware transport injected. */
#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static struct vxl_request req(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=sizeof(r);return r;}
#include <vxl/submit_timing.h>
#include <stddef.h>
typedef vxl_u64 u64;
#define VXL_INTERNAL_H
#define __user
#define THIS_MODULE ((void *)0)
#define no_llseek ((void *)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct inode {int unused;};struct mutex {int unused;};struct miscdevice {int unused;};struct file {void *private_data;};
struct vxl_device {bool cdma_fastwait_active;struct miscdevice misc;struct mutex mutex;struct vxl_engine engine;struct vxl_hunet_ctx hunet;struct vxl_submit_trace submit_trace;struct vxl_mgmt_trace mgmt_trace;};
struct file_operations {void *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);long (*unlocked_ioctl)(struct file *,unsigned int,unsigned long);long (*compat_ioctl)(struct file *,unsigned int,unsigned long);void *llseek;};
static int outer_fault,outer_input_fault;
static void mutex_lock(struct mutex *m){(void)m;}
static void mutex_unlock(struct mutex *m){(void)m;}
static u64 ktime_get_ns(void){return 100000000;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;}
#define current ((void *)0)
static int signal_pending(void *p){(void)p;return 0;}
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static int copy_from_user(void *dst,const void *src,size_t n){if(outer_input_fault)return 1;memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,size_t n){if(outer_fault)return 1;memcpy(dst,src,n);return 0;}
static int seen_fast,produce_begin_token;
static int observed_engine(struct vxl_engine *e,vxl_u64 owner,vxl_u32 cmd,struct vxl_request *r){
 struct vxl_device *d=container_of(e,struct vxl_device,engine);(void)owner;(void)cmd;(void)r;seen_fast=d->cdma_fastwait_active;if(produce_begin_token&&cmd==VXL_HYBRID_CPU_BEGIN){d->hunet.hybrid.token=7;return 0;}return -EIO;
}
#define vxl_engine_call observed_engine
#include "../../driver/vxl_ioctl.c"
#undef vxl_engine_call
static void probe(unsigned cmd,unsigned profile,unsigned program,int owner,unsigned state,unsigned trust,int want){
 struct vxl_device d={0};struct file f={&d};struct vxl_request r=req();
 d.engine.profile_id=profile;d.hunet.program_id=program;d.engine.owner=owner?(uintptr_t)&f:1;d.engine.job.state=state;r.trust_id=trust;seen_fast=-1;
 assert(ioctl_file(&f,cmd,(unsigned long)&r)==-EIO);assert(seen_fast==want);assert(!d.cdma_fastwait_active);
 outer_fault=1;seen_fast=-1;assert(ioctl_file(&f,cmd,(unsigned long)&r)==-EFAULT);assert(seen_fast==want&&!d.cdma_fastwait_active);outer_fault=0;
}
int main(void){
 unsigned management[]={VXL_HUNET_BEGIN_ROI,VXL_HYBRID_CPU_BEGIN,VXL_HYBRID_CPU_COMMIT,VXL_HYBRID_END};
 for(unsigned i=0;i<4;i++){
  probe(management[i],4,9,1,VXL_READY,0,1);
  probe(management[i],1,9,1,VXL_READY,0,0);probe(management[i],4,8,1,VXL_READY,0,0);
  probe(management[i],4,9,0,VXL_READY,0,0);probe(management[i],4,9,1,VXL_RUNNING,0,0);
 }
 unsigned ordinary[]={VXL_GET_INFO,VXL_COPY_TO,VXL_COPY_FROM,VXL_COPY_DEVICE,VXL_HYBRID_SEAL,VXL_HYBRID_GET_CPU_EVIDENCE,VXL_HUNET_END_ROI,VXL_HYBRID_GET_BINDING};
 for(unsigned i=0;i<sizeof(ordinary)/sizeof(*ordinary);i++)probe(ordinary[i],4,9,1,VXL_READY,9,0);
 probe(VXL_SUBMIT,4,9,1,VXL_READY,9,1);probe(VXL_SUBMIT,4,9,1,VXL_READY,8,0);
 {
  struct vxl_device d={0};struct file f={&d};struct vxl_request r=req();struct vxl_mgmt_row rows[4];
  d.engine.profile_id=4;d.engine.owner=(uintptr_t)&f;d.engine.generation=3;d.engine.job.state=VXL_READY;d.hunet.program_id=9;
  d.mgmt_trace.rows=rows;d.mgmt_trace.capacity=4;
  assert(ioctl_file(&f,VXL_HYBRID_CPU_BEGIN,(unsigned long)&r)==-EIO);
  assert(d.mgmt_trace.used==1&&!d.mgmt_trace.active&&!d.hunet.mgmt_row&&rows[0].result==-EIO);
  outer_fault=1;assert(ioctl_file(&f,VXL_HYBRID_CPU_COMMIT,(unsigned long)&r)==-EFAULT);outer_fault=0;
  assert(d.mgmt_trace.used==2&&!d.mgmt_trace.active&&rows[1].result==-EFAULT);
  outer_input_fault=1;assert(ioctl_file(&f,VXL_HYBRID_CPU_BEGIN,(unsigned long)&r)==-EFAULT);outer_input_fault=0;
  assert(d.mgmt_trace.used==3&&!d.mgmt_trace.active&&rows[2].result==-EFAULT);
 }
 {
  struct vxl_device d={0};struct file f={&d};struct vxl_request r=req();struct vxl_opt_release release={0};
  d.engine.profile_id=4;d.engine.generation=3;d.engine.owner=(uintptr_t)&f;d.engine.opened=1;d.engine.job.state=VXL_READY;
  struct vxl_opt_catalog cat={.runtime_flags=1};release.catalog=&cat;
  d.hunet.engine=&d.engine;d.hunet.program_id=6;d.hunet.opt.release=&release;
  struct vxl_hunet_binding_v1 *b=&release.binding.v3.v2.v1;
  b->magic=VXL_HUNET_BINDING_MAGIC;b->version=4;b->bytes=512;b->profile_id=4;b->program_id=10;b->features=31;release.wire_bytes=8192;
  d.engine.objects[0]=(struct vxl_object){.handle=0x300000001ULL,.domain=VXL_PL,.purpose=VXL_CODE,.gpu=VXL_HUNET_CODE,.length=8388608};
  r.handle=d.engine.objects[0].handle;r.length=4096;r.user_ptr=16;
  seen_fast=-1;assert(ioctl_file(&f,VXL_COPY_TO,(unsigned long)&r)==-EIO&&seen_fast==1);
  assert(!d.cdma_fastwait_active&&!d.hunet.opt_load_locked&&!d.hunet.opt_load_owner);
  seen_fast=-1;r.offset=8192;assert(ioctl_file(&f,VXL_COPY_TO,(unsigned long)&r)==-EIO&&seen_fast==0);
  seen_fast=-1;assert(ioctl_file(&f,VXL_HYBRID_SEAL,(unsigned long)&(struct vxl_request){.abi_major=1,.struct_size=sizeof(r)})==-EIO&&seen_fast==1);
 }
 {
  struct vxl_device d={0};struct file f={&d};struct vxl_request r=req();struct vxl_mgmt_row rows[1];
  d.engine.profile_id=4;d.engine.owner=(uintptr_t)&f;d.engine.generation=3;d.engine.job.state=VXL_READY;d.hunet.program_id=9;d.hunet.hybrid.token=5;
  d.mgmt_trace.rows=rows;d.mgmt_trace.capacity=1;produce_begin_token=1;
  assert(!ioctl_file(&f,VXL_HYBRID_CPU_BEGIN,(unsigned long)&r));produce_begin_token=0;
  assert(rows[0].key.token==7&&rows[0].key.owner_cookie==(((vxl_u64)3<<32)^7));
 }
 puts("MANAGEMENT_REAL_IOCTL_OWNER_PROFILE_COMMAND_SCOPE_ERROR_COPYOUT_CLEAR_PASS_NOT_BOARD");return 0;
}


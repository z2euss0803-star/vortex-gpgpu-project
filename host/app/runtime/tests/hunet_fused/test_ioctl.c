#include <stdbool.h>
/* Real ioctl wrapper; only kernel primitives and hardware transport injected. */
#include "../hunet_hybrid/fixture.h"
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
static int outer_fault;
static void mutex_lock(struct mutex *m){(void)m;}
static void mutex_unlock(struct mutex *m){(void)m;}
static u64 ktime_get_ns(void){return 100000000;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;}
#define current ((void *)0)
static int signal_pending(void *p){(void)p;return 0;}
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static int copy_from_user(void *dst,const void *src,size_t n){memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,size_t n){if(outer_fault)return 1;memcpy(dst,src,n);return 0;}
static unsigned submit_scope_seen;
static int scope_engine_call(struct vxl_engine *e,vxl_u64 owner,vxl_u32 cmd,struct vxl_request *r){
 struct vxl_device *d=container_of(e,struct vxl_device,engine);
 int expected=cmd==VXL_SUBMIT&&e->profile_id==4&&d->hunet.program_id==9&&e->owner==owner&&e->job.state==VXL_READY&&r->trust_id==9;
 assert(d->cdma_fastwait_active==expected);
 if(expected)submit_scope_seen++;
 return vxl_engine_call(e,owner,cmd,r);
}
#define vxl_engine_call scope_engine_call
#include "../../driver/vxl_ioctl.c"
#undef vxl_engine_call
static void check_submit_scope(void){
 struct vxl_device d={0};struct file f={&d};struct vxl_request r=req();
 d.engine.profile_id=4;d.engine.hunet=&d.hunet;d.engine.opened=1;d.engine.owner=(uintptr_t)&f;d.engine.job.state=VXL_READY;d.hunet.program_id=9;
 r.trust_id=9;r.deadline_ms=60000;r.handle=1;r.handle2=2;
 assert(ioctl_file(&f,VXL_SUBMIT,(unsigned long)&r)==-EBADF);assert(submit_scope_seen==1&&!d.cdma_fastwait_active);
 d.engine.owner=123;assert(ioctl_file(&f,VXL_SUBMIT,(unsigned long)&r)==-EBADF);assert(!d.cdma_fastwait_active);
 d.engine.owner=(uintptr_t)&f;r.trust_id=8;assert(ioctl_file(&f,VXL_SUBMIT,(unsigned long)&r)==-EBADF);assert(!d.cdma_fastwait_active);
}

#include "../../generated/hunet_fused/catalog.h"
int main(void){
 check_submit_scope();
 struct hfixture h;struct vxl_fused_release release={0};struct vxl_device d={0};struct file f={&d};struct vxl_hunet_job_evidence_v1 ev;struct vxl_request r;
 (void)seal_fixture;(void)hbegin_request;hinit(&h);h.f.c.program_id=9;h.f.c.fused.release=&release;h.f.c.next_sequence=1;
 d.engine=h.f.e;d.hunet=h.f.c;d.engine.hunet=&d.hunet;d.hunet.engine=&d.engine;d.engine.owner=(uintptr_t)&f;d.hunet.seal_owner=d.hunet.hybrid.owner=d.engine.owner;
 d.hunet.fused.pending=vxl_fused_metadata[0];d.hunet.fused.acked=1;
 ev=(struct vxl_hunet_job_evidence_v1){.job_id=7,.software_state=VXL_COMPLETED,.sequence=1,.stage_id=0,.op=15,.valid_flags=31,.generation=d.engine.generation};d.hunet.evidence=ev;
 r=req();r.length=256;r.user_ptr=(uintptr_t)&ev;r.job_id=7;h.f.output_fault=1;
 assert(ioctl_file(&f,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r)==-EFAULT);assert(d.hunet.next_sequence==1&&!d.hunet.evidence_delivered);
 h.f.output_fault=0;outer_fault=1;assert(ioctl_file(&f,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r)==-EFAULT);assert(d.hunet.next_sequence==1&&!d.hunet.evidence_delivered);
 outer_fault=0;assert(!ioctl_file(&f,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r));assert(d.hunet.next_sequence==97&&d.hunet.hybrid.gpu_jobs==1);
 assert(!ioctl_file(&f,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r));assert(d.hunet.next_sequence==97&&d.hunet.hybrid.gpu_jobs==1);
 hfree(&h);puts("FUSED_REAL_IOCTL_TWO_COPY_FAULTS_AND_EXACTLY_ONCE_GPU_DELIVERY_PASS_NOT_BOARD");
}

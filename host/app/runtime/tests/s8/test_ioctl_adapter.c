/* Exercise production ioctl wrapper with software copy/clock/mutex boundaries. */
#include "state_fixture.h"
#include <vxl/hunet.h>
#include <vxl/submit_timing.h>
#include <vxl/mgmt_timing.h>
#include <vxl/s8_trace.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef vxl_u64 u64;
#define VXL_INTERNAL_H
#define __user
#define THIS_MODULE ((void *)0)
#define no_llseek ((void *)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct inode {int unused;};struct mutex {int unused;};struct miscdevice {int unused;};
struct file {void *private_data;};
struct vxl_device {bool cdma_fastwait_active;struct miscdevice misc;struct mutex mutex;struct vxl_engine engine;struct vxl_hunet_ctx hunet;struct vxl_submit_trace submit_trace;struct vxl_mgmt_trace mgmt_trace;};
struct file_operations {void *owner;int (*open)(struct inode *,struct file *);int (*release)(struct inode *,struct file *);long (*unlocked_ioctl)(struct file *,unsigned int,unsigned long);long (*compat_ioctl)(struct file *,unsigned int,unsigned long);void *llseek;};
static u64 clock_ns;static int copy_fault;
static void mutex_lock(struct mutex *p){(void)p;clock_ns+=3;}
static void mutex_unlock(struct mutex *p){(void)p;}
static u64 ktime_get_ns(void){return ++clock_ns;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;clock_ns+=1000000;}
#define current ((void *)0)
static int signal_pending(void *p){(void)p;return 0;}
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static int copy_from_user(void *dst,const void *src,size_t n){memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,size_t n){if(copy_fault)return 1;memcpy(dst,src,n);return 0;}
#include "../../driver/vxl_ioctl.c"
static u64 fake_now(void *p){(void)p;return ktime_get_ns();}
static void deliver(struct vxl_s8_ctx *c){for(unsigned file=0;file<3;++file){struct vxl_s8_reader r;assert(!vxl_s8_reader_begin(c,file,1,&r));assert(!vxl_s8_reader_advance(c,&r,0,1,1));assert(!vxl_s8_reader_finish(c,&r));}}
static void reap_and_next_load(void){
 struct state_fixture *s=calloc(1,sizeof(*s));struct vxl_device *d=calloc(1,sizeof(*d));struct vxl_s8_trace trace;struct file f;vxl_u8 boot[16]={9};
 struct vxl_s8_job_row *jobs=calloc(256,sizeof(*jobs));struct vxl_s8_op_row *ops=calloc(2048,sizeof(*ops));assert(s&&d&&jobs&&ops);
 state_init(s);struct vxl_s8_hw_snapshot saved=s->c.hw;assert(!vxl_s8_trace_init(&s->c,&trace,jobs,ops,boot));assert(!vxl_s8_trace_begin(&s->c,&s->c.job_args,1));s->c.hw=saved;state_complete(s);
 d->engine=s->m.engine;d->engine.s8=&s->c;s->c.engine=&d->engine;s->c.io.now_ns=fake_now;f.private_data=d;d->engine.owner=(unsigned long)&f;vxl_s8_trace_sync(&s->c);
 for(unsigned i=0;i<s->c.capture_count;++i){s->c.capture[i].delivered=s->c.capture[i].bytes;}
 deliver(&s->c);assert(s->c.trace_delivered==7);
 struct vxl_request r=sreq();r.job_id=1;assert(!ioctl_file(&f,VXL_REAP,(unsigned long)&r));assert(d->engine.job.state==VXL_READY&&!s->c.capture_pending);
 deliver(&s->c);vxl_u64 revision=trace.revision;unsigned operations=trace.operations_used;r=sreq();assert(!ioctl_file(&f,VXL_GET_INFO,(unsigned long)&r));
 assert(trace.revision==revision&&trace.operations_used==operations&&s->c.trace_delivered==7);
 vxl_u32 payload=0;r=sreq();r.handle=s->output;r.length=4;r.user_ptr=(vxl_u64)(uintptr_t)&payload;assert(!ioctl_file(&f,VXL_COPY_TO,(unsigned long)&r));assert(trace.revision==revision&&s->c.trace_delivered==7);
 assert(!vxl_s8_timing_add(&s->c,VXL_S8_COPY_TO_TIME,3,4,4));assert(trace.revision==revision&&s->c.trace_delivered==7);
 struct vxl_s8_job_v1 next=s->c.job_args;next.sequence++;assert(!vxl_s8_trace_begin(&s->c,&next,2));assert(trace.jobs_used==2&&trace.active);
 state_destroy(s);free(ops);free(jobs);free(d);free(s);
}
int main(void){
 reap_and_next_load();
 struct state_fixture *s=calloc(1,sizeof(*s));struct vxl_device *d=calloc(1,sizeof(*d));struct file f;assert(s&&d);
 state_init(s);state_complete(s);d->engine=s->m.engine;d->engine.s8=&s->c;s->c.engine=&d->engine;
 f.private_data=d;d->engine.owner=(unsigned long)&f;s->c.io.now_ns=fake_now;
 struct vxl_request r=sreq();assert(!ioctl_file(&f,VXL_GET_INFO,(unsigned long)&r));
 assert(r.profile_id==5 && !d->cdma_fastwait_active);
 assert(s->c.times.metrics[VXL_S8_MUTEX_WAIT].calls==1);
 assert(s->c.times.metrics[VXL_S8_DELIVERY].bytes==176);
 r=sreq();r.job_id=1;assert(!ioctl_file(&f,VXL_WAIT,(unsigned long)&r));
 assert(s->c.times.metrics[VXL_S8_WAIT_IOCTL].calls==1);
 assert(s->c.times.valid_events&(1ULL<<VXL_S8_WAIT_RETURN));
 copy_fault=1;r=sreq();assert(ioctl_file(&f,VXL_GET_INFO,(unsigned long)&r)==-EFAULT);
 assert(s->c.delivery_fault&&d->engine.quarantined&&d->engine.job.pinned);
 assert(s->c.hw.elapsed_raw==123456 && vxl_s8_can_reap(&s->c)==-EBUSY);
 /* Rejected copy-in/command cannot mutate a previous raw snapshot. */
 assert(ioctl_file(&f,0,(unsigned long)&r)==-ENOTTY);assert(s->c.hw.elapsed_raw==123456);
 state_destroy(s);free(d);free(s);
 puts("S8_PRODUCTION_IOCTL_TIMING_AND_ENVELOPE_FAULT_PRESERVED_PASS SOFTWARE_ONLY");return 0;
}

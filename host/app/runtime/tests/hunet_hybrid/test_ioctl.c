#include <stdbool.h>
/* Real ioctl wrapper; only kernel primitives and hardware transport injected. */
#include "fixture.h"
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
#include "../../driver/vxl_ioctl.c"
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 struct hfixture h;hinit(&h);float *x=malloc(1572864),*y=calloc(393216,4);assert(x&&y);struct vxl_hybrid_begin_v1 b=hbegin_request(&h,x);assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));
 struct vxl_hybrid_commit_v1 commit={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.generation=b.generation,.sequence=b.sequence,.stage=b.stage,.algorithm=1,.token=b.token,.output_ptr=(uintptr_t)y,.output_bytes=b.output_bytes,.threads=4};memcpy(commit.run_id,b.run_id,16);vxl_hunet_hash(y,1572864,commit.output_sha256);
 struct vxl_device d={0};d.engine=h.f.e;d.hunet=h.f.c;d.engine.hunet=&d.hunet;d.hunet.engine=&d.engine;struct file f={&d};d.engine.owner=(uintptr_t)&f;d.hunet.seal_owner=d.hunet.hybrid.owner=d.engine.owner;
 struct vxl_request r=req();r.user_ptr=(uintptr_t)&commit;r.length=128;outer_fault=1;assert(ioctl_file(&f,VXL_HYBRID_CPU_COMMIT,(unsigned long)&r)==-EFAULT);assert(d.hunet.hybrid.phase==VXL_HYBRID_CPU_COMMITTED&&d.hunet.next_sequence==296);unsigned writes=h.write_blocks;outer_fault=0;assert(!ioctl_file(&f,VXL_HYBRID_CPU_COMMIT,(unsigned long)&r));assert(h.write_blocks==writes);
 struct vxl_hybrid_cpu_evidence_v1 ev;r=req();r.length=384;r.user_ptr=(uintptr_t)&ev;r.job_id=b.token;
 h.f.output_fault=1;assert(ioctl_file(&f,VXL_HYBRID_GET_CPU_EVIDENCE,(unsigned long)&r)==-EFAULT);h.f.output_fault=0;assert(d.hunet.next_sequence==296);
 outer_fault=1;assert(ioctl_file(&f,VXL_HYBRID_GET_CPU_EVIDENCE,(unsigned long)&r)==-EFAULT);outer_fault=0;assert(d.hunet.next_sequence==296);
 assert(!ioctl_file(&f,VXL_HYBRID_GET_CPU_EVIDENCE,(unsigned long)&r));assert(d.hunet.next_sequence==297&&d.hunet.hybrid.cpu_jobs==1&&d.hunet.hybrid.logical_jobs==1&&ev.check_flags==63);
 assert(!ioctl_file(&f,VXL_HYBRID_GET_CPU_EVIDENCE,(unsigned long)&r));assert(d.hunet.next_sequence==297&&d.hunet.hybrid.cpu_jobs==1&&h.write_blocks==writes);
 r.job_id++;assert(ioctl_file(&f,VXL_HYBRID_GET_CPU_EVIDENCE,(unsigned long)&r)==-ESTALE);
 {struct vxl_submit_time row;struct vxl_submit_time *t;d.submit_trace.rows=&row;d.submit_trace.capacity=1;t=vxl_time_begin(&d.submit_trace,2,99999900);d.hunet.submit_time=t;d.engine.job.state=VXL_RUNNING;d.engine.job.pinned=1;timing_ioctl_done(&d,t,VXL_SUBMIT,-EFAULT,0,10,0,0);assert(!d.submit_trace.active&&!d.hunet.submit_time&&row.submit_return_ns==100000000&&row.result==(vxl_u64)(long long)-EFAULT&&d.engine.job.state==VXL_RUNNING&&d.engine.job.pinned==1);}
 h.f.c.hybrid.cpu_input=h.f.c.hybrid.cpu_output=NULL;hfree(&h);free(x);free(y);puts("PRODUCTION_HYBRID_IOCTL_COMMIT_AND_EVIDENCE_COPYOUT_FAULTS_PASS_NOT_BOARD");return 0;
}

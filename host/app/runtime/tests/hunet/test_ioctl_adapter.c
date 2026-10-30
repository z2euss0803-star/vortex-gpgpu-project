#include <stdbool.h>
/* Exercise the production ioctl wrapper, including both copyout boundaries. */
#include "memory_fixture.h"
#include <vxl/submit_timing.h>
#include <stddef.h>
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
static struct vxl_device device;static int outer_fault,complete_on_outer;static u64 clock_ns=100000000;
static void mutex_lock(struct mutex *m){(void)m;}
static void mutex_unlock(struct mutex *m){(void)m;}
static u64 ktime_get_ns(void){return clock_ns;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;}
#define current ((void *)0)
static int signal_pending(void *p){(void)p;return 0;}
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static int copy_from_user(void *dst,const void *src,size_t n){memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,size_t n){if(outer_fault)return 1;if(complete_on_outer){device.hunet.evidence.software_state=VXL_COMPLETED;device.hunet.evidence.done_ns=clock_ns+10;device.hunet.evidence.ack_ns=clock_ns+20;device.engine.job.state=VXL_COMPLETED;device.engine.job.pinned=0;complete_on_outer=0;}memcpy(dst,src,n);return 0;}
#include "../../driver/vxl_ioctl.c"
static int no_poll(void *p,struct vxl_event *e){(void)p;(void)e;return 0;}
int main(void){
 struct fixture *f=calloc(1,sizeof(*f));struct vxl_ops ops=fops;struct file file;struct vxl_request r;struct vxl_hunet_end_v1 end,saved;struct vxl_hunet_job_evidence_v1 ev;vxl_u32 owners[1014];unsigned i;
 assert(f);init_fixture(f);seal_fixture(f);assert(!vxl_hunet_guards(&f->c,1));f->c.active=1;f->c.next_sequence=1024;vxl_hunet_hash(f->ws+64,196608,f->c.input_sha);f->c.evidence.valid_flags=VXL_HUNET_VALID_CRT;f->c.evidence.crt_guard_flags=3;
 for(i=0;i<1014;i++){owners[i]=i<254?1:i<508?2:i<761?3:4;}
 assert(!fio(f,0x87044080,owners,sizeof(owners),1));
 device.engine=f->e;device.hunet=f->c;device.hunet.engine=&device.engine;device.engine.hunet=&device.hunet;ops.poll=no_poll;device.engine.ops=&ops;file.private_data=&device;device.engine.owner=(unsigned long)&file;device.hunet.seal_owner=device.engine.owner;
 r=req();r.length=128;r.user_ptr=(uintptr_t)&end;f->output_fault=1;assert(ioctl_file(&file,VXL_HUNET_END_ROI,(unsigned long)&r)==-EFAULT);assert(device.hunet.active&&device.hunet.end_pending);f->output_fault=0;
 outer_fault=1;assert(ioctl_file(&file,VXL_HUNET_END_ROI,(unsigned long)&r)==-EFAULT);saved=end;assert(device.hunet.active&&device.hunet.end_pending);outer_fault=0;
 assert(!ioctl_file(&file,VXL_HUNET_END_ROI,(unsigned long)&r));assert(!memcmp(&end,&saved,128)&&!device.hunet.active&&!device.hunet.end_pending);
 device.engine.job.id=55;device.engine.job.state=VXL_RUNNING;device.engine.job.pinned=1;memset(&device.hunet.evidence,0,256);device.hunet.evidence.job_id=55;device.hunet.evidence.generation=device.engine.generation;device.hunet.evidence.software_state=VXL_RUNNING;device.hunet.evidence.execution_deadline_ns=clock_ns+60000000000ULL;device.hunet.evidence_delivered=0;
 r=req();r.length=256;r.job_id=55;r.user_ptr=(uintptr_t)&ev;complete_on_outer=1;assert(!ioctl_file(&file,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r));assert(ev.software_state==VXL_RUNNING&&!device.hunet.evidence_delivered);
 r=req();r.job_id=55;assert(ioctl_file(&file,VXL_REAP,(unsigned long)&r)==-EBUSY);
 r=req();r.length=256;r.job_id=55;r.user_ptr=(uintptr_t)&ev;assert(!ioctl_file(&file,VXL_HUNET_GET_EVIDENCE,(unsigned long)&r));assert(ev.software_state==VXL_COMPLETED&&device.hunet.evidence_delivered);
 r=req();r.job_id=55;assert(!ioctl_file(&file,VXL_REAP,(unsigned long)&r));
 {
  struct vxl_device epoch={0};struct file one,two;struct vxl_mgmt_key key={0};vxl_u32 generation;
  vxl_engine_init_profile(&epoch.engine,&ops,f,1,4);epoch.mgmt_trace.capacity=512;
  epoch.mgmt_trace.rows=calloc(512,sizeof(struct vxl_mgmt_row));assert(epoch.mgmt_trace.rows);
  one.private_data=&epoch.misc;assert(!open_file(NULL,&one));generation=epoch.engine.generation;
  key.generation=generation;key.command=17;key.owner_cookie=(vxl_u64)generation<<32;
  assert(vxl_mgmt_begin(&epoch.mgmt_trace,&key,100));vxl_mgmt_finish(&epoch.mgmt_trace,200,0);
  assert(!release_file(NULL,&one));two.private_data=&epoch.misc;assert(open_file(NULL,&two)==-EBUSY);assert(epoch.engine.generation==generation);
  assert(epoch.mgmt_trace.used==1);epoch.mgmt_trace.exported_used=1;
  two.private_data=&epoch.misc;assert(!open_file(NULL,&two));assert(epoch.engine.generation>generation&&!epoch.mgmt_trace.used);
  assert(!release_file(NULL,&two));free(epoch.mgmt_trace.rows);
 }
 puts("PRODUCTION_IOCTL_END_TWO_COPYOUT_FAULTS_AND_EVIDENCE_WORKER_RACE_PASS");destroy_fixture(f);free(f);return 0;
}

#define MOSAIC_IOCTL_EMBED_TEST
#include "test_ioctl.c"
#include <vxl/submit_timing.h>
#include <vxl/mgmt_timing.h>
#include <stdbool.h>
#include <vxl/profile_switch.h>
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
static u64 clock_ns;static int copy_fault,copy_input_fault;
static void mutex_lock(struct mutex *p){(void)p;clock_ns+=3;}
static void mutex_unlock(struct mutex *p){(void)p;}
static u64 ktime_get_ns(void){return ++clock_ns;}
static void usleep_range(unsigned a,unsigned b){(void)a;(void)b;clock_ns+=1000000;}
#define current ((void *)0)
static int signal_pending(void *p){(void)p;return 0;}
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static int copy_from_user(void *dst,const void *src,size_t n){if(copy_input_fault)return 1;memcpy(dst,src,n);return 0;}
static int copy_to_user(void *dst,const void *src,size_t n){if(copy_fault)return 1;memcpy(dst,src,n);return 0;}
#include "../../driver/vxl_ioctl.c"
static u64 fake_now(void *p){(void)p;return ktime_get_ns();}
static struct vxl_device device;
static struct file fd;
static void bind_driver(const char *assets){
 setup(assets);e.ops=&ops;memset(&device,0,sizeof(device));device.engine=e;h.engine=&device.engine;device.engine.mosaic=&c;
 fd.private_data=&device;device.engine.owner=(unsigned long)&fd;
 h.io.now_ns=fake_now;
}
static int driver_call(vxl_u32 cmd,struct vxl_request *r){return ioctl_file(&fd,cmd,(unsigned long)r);}
int main(int argc,char **argv){
 assert(argc==2);bind_driver(argv[1]);struct vxl_request r=request();assert(!driver_call(VXL_GET_INFO,&r)&&r.profile_id==6);
 r=request();r.profile_id=5;assert(driver_call(VXL_SELECT_PROFILE,&r)==-ENOTTY);
 struct vxl_profile_factory factory={.enabled=1};device.engine.profile_factory=&factory;
 r=request();r.profile_id=5;assert(driver_call(VXL_SELECT_PROFILE,&r)==-EBUSY);
 r=request();r.profile_id=5;r.length=1;assert(driver_call(VXL_SELECT_PROFILE,&r)==-EINVAL);device.engine.profile_factory=NULL;
 struct vxl_mosaic_seal_v1 s=seal();r=request();r.user_ptr=(uintptr_t)&s;r.length=128;assert(!driver_call(VXL_MOSAIC_SEAL,&r));
 struct vxl_mosaic_frame_v1 fr=frame();r=request();r.user_ptr=(uintptr_t)&fr;r.length=128;assert(!driver_call(VXL_MOSAIC_BEGIN,&r));
 struct mosaic_job_v1 j;assert(!mosaic_next_job(&c,1,&j));r=request();r.handle=device.engine.objects[4].handle;r.user_ptr=(uintptr_t)&j;r.length=128;assert(!driver_call(VXL_COPY_TO,&r));
 r=request();r.handle=device.engine.objects[0].handle;r.handle2=device.engine.objects[4].handle;r.trust_id=12;r.deadline_ms=60000;
 clock_ns=2000;assert(!driver_call(VXL_SUBMIT,&r));assert(h.evidence.observed.submit_enter>=2000&&f.starts==1);done();assert(mosaic_runtime_poll(&device.engine)==1);
 vxl_u8 wire[512];r=request();r.job_id=device.engine.job.id;r.user_ptr=(uintptr_t)wire;r.length=512;copy_fault=1;
 assert(driver_call(VXL_MOSAIC_EVIDENCE,&r)==-EFAULT);copy_fault=0;
 assert(device.engine.quarantined&&device.engine.job.pinned&&h.evidence_bytes==0&&!h.pending_delivery.serial);
 assert(f.starts==1&&f.acks==1);r=request();r.job_id=device.engine.job.id;assert(driver_call(VXL_REAP,&r)<0);
 r=request();r.job_id=device.engine.job.id;r.user_ptr=(uintptr_t)wire;r.length=512;assert(!driver_call(VXL_MOSAIC_EVIDENCE,&r));assert(get32(wire+168)==0&&get32(wire+156)==(vxl_u32)-EFAULT);
 release_file(NULL,&fd);assert(device.engine.owner==(unsigned long)&fd&&device.engine.job.pinned&&!device.engine.opened);cleanup();
 bind_driver(argv[1]);s=seal();r=request();r.user_ptr=(uintptr_t)&s;r.length=128;assert(!driver_call(VXL_MOSAIC_SEAL,&r));fr=frame();r=request();r.user_ptr=(uintptr_t)&fr;r.length=128;assert(!driver_call(VXL_MOSAIC_BEGIN,&r));
 copy_input_fault=1;r=request();assert(driver_call(VXL_SUBMIT,&r)==-EFAULT);copy_input_fault=0;
 assert(h.first_error==-EFAULT&&device.engine.quarantined&&device.engine.job.pinned&&!f.starts);release_file(NULL,&fd);assert(h.first_error==-EFAULT&&device.engine.job.pinned);cleanup();
 puts("ACTUAL_KERNEL_IOCTL_SOURCE_MOSAIC_ENVELOPE_FAULT_PIN_AND_FAILED_EVIDENCE_PASS_NOT_KERNEL_EXECUTION");return 0;
}

#include "vxl_internal.h"
#include "v2/linux.h"
#include <vxl/submit_timing.h>
#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/mosaic_runtime.h>
#include <vxl/profile_switch.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
static int open_file(struct inode *inode,struct file *f) {
 struct miscdevice *misc=f->private_data;struct vxl_device *d=container_of(misc,struct vxl_device,misc);int rc;(void)inode;
 mutex_lock(&d->mutex);
 if(d->compute){struct vxl_v2_ctx *c=d->compute->port.ctx;
  if(d->engine.owner)rc=-EBUSY;
  else if(c->epoch==~(vxl_u64)0)rc=-EOVERFLOW;
  else rc=vxl_v2_owner_enter(c,(unsigned long)f,c->epoch+1);
  if(!rc)f->private_data=d;
  mutex_unlock(&d->mutex);return rc;
 }
 /* Reject an unsnapshotted/faulted epoch before engine_open advances generation
  * or clears digest counters. Preserve the engine's original admission order. */
 if(d->engine.quarantined)rc=-EIO;
 else if(d->engine.owner)rc=-EBUSY;
 else if(d->engine.generation==0xffffffffU)rc=-EOVERFLOW;
 else{rc=vxl_mgmt_new_epoch(&d->mgmt_trace);if(!rc)rc=vxl_engine_open(&d->engine,(unsigned long)f);}
 if(!rc){if(d->mgmt_trace.rows&&!d->submit_trace.active){d->submit_trace.used=0;d->submit_trace.flags=0;}f->private_data=d;}
 mutex_unlock(&d->mutex);return rc;
}
static int release_file(struct inode *inode,struct file *f){struct vxl_device *d=f->private_data;(void)inode;mutex_lock(&d->mutex);if(d->compute){vxl_v2_close_locked(&d->compute->port,(unsigned long)f);mutex_unlock(&d->mutex);return 0;}vxl_engine_close(&d->engine,(unsigned long)f);if(d->submit_trace.active){d->submit_trace.active->flags|=VXL_TIME_BUSY;vxl_time_finish(&d->submit_trace,ktime_get_ns(),-ECANCELED);d->hunet.submit_time=NULL;}if(d->mgmt_trace.active){vxl_mgmt_finish(&d->mgmt_trace,ktime_get_ns(),-ECANCELED);d->hunet.mgmt_row=NULL;}mutex_unlock(&d->mutex);return 0;}
static int mgmt_command(unsigned cmd){return cmd==VXL_HUNET_BEGIN_ROI||cmd==VXL_HYBRID_CPU_BEGIN||cmd==VXL_HYBRID_CPU_COMMIT||cmd==VXL_HYBRID_END||cmd==VXL_HYBRID_SEAL||cmd==VXL_HUNET_SEAL||cmd==VXL_COPY_TO;}
static struct vxl_mgmt_row *mgmt_start_locked(struct vxl_device *d,struct file *f,unsigned cmd,u64 entered){
 struct vxl_mgmt_key key;
 if(!d->mgmt_trace.rows||!mgmt_command(cmd)||d->engine.profile_id!=4||d->engine.owner!=(unsigned long)f)return NULL;
 key.owner_cookie=((u64)d->engine.generation<<32)^d->hunet.hybrid.token;key.generation=d->engine.generation;key.token=d->hunet.hybrid.token;key.command=cmd;
 return vxl_mgmt_begin(&d->mgmt_trace,&key,entered);
}
static void mgmt_ioctl_done(struct vxl_device *d,struct vxl_mgmt_row *row,int rc){
 if(!row)return;
 mutex_lock(&d->mutex);if(d->mgmt_trace.active==row)vxl_mgmt_finish(&d->mgmt_trace,ktime_get_ns(),rc);mutex_unlock(&d->mutex);
}
static void mgmt_early_error(struct vxl_device *d,struct file *f,unsigned cmd,u64 entered,int rc){
 struct vxl_mgmt_row *row;if(!d->mgmt_trace.rows||!mgmt_command(cmd))return;
 mutex_lock(&d->mutex);row=mgmt_start_locked(d,f,cmd,entered);if(row)vxl_mgmt_finish(&d->mgmt_trace,ktime_get_ns(),rc);mutex_unlock(&d->mutex);
}
static void timing_ioctl_done(struct vxl_device *d,struct vxl_submit_time *t,unsigned cmd,int rc,int result,u64 lock_ns,u64 sleep_ns,u64 sleeps){
 if(!t)return;
 mutex_lock(&d->mutex);
 if(d->submit_trace.active==t){
  u64 end=ktime_get_ns();
  if(cmd==VXL_SUBMIT){t->submit_return_ns=end;vxl_time_add(t,&t->submit_mutex_wait_ns,0,lock_ns);}
  else {vxl_time_add(t,&t->wait_mutex_wait_ns,0,lock_ns);vxl_time_add(t,&t->wait_sleep_ns,0,sleep_ns);t->wait_sleep_calls+=sleeps;}
  if((cmd==VXL_SUBMIT&&rc)||(cmd==VXL_WAIT&&rc!=-ETIMEDOUT)){vxl_time_finish(&d->submit_trace,end,rc?rc:result);d->hunet.submit_time=NULL;}
 }
 mutex_unlock(&d->mutex);
}
#include "vxl_mosaic_ioctl.inc"
static long ioctl_file(struct file *f,unsigned int cmd,unsigned long arg) {
 struct vxl_device *d=f->private_data;struct vxl_request r;struct vxl_hunet_delivery delivered={0};struct vxl_hybrid_delivery hybrid_delivered={0};struct vxl_submit_time *request_time=NULL;struct vxl_mgmt_row *management_time=NULL;u64 entered=ktime_get_ns(),lock_begin,lock_wait=0,sleep_begin,sleep_time=0,sleeps=0,end;int rc;
 if(d->compute)return vxl_v2_dispatch(&d->compute->port,(unsigned long)f,cmd,arg);
 if(cmd==VXL_SELECT_PROFILE){
  if(!d->engine.profile_factory||!d->engine.profile_factory->enabled)return -ENOTTY;
  if(copy_from_user(&r,(void __user *)arg,sizeof(r)))return -EFAULT;
  mutex_lock(&d->mutex);rc=vxl_profile_select(d,(unsigned long)f,&r);mutex_unlock(&d->mutex);
  if(!rc&&copy_to_user((void __user *)arg,&r,sizeof(r)))return -EFAULT;
  return rc;
 }
 if(d->engine.profile_id==6)return mosaic_ioctl_file(f,cmd,arg,entered);
 if(_IOC_TYPE(cmd)!='V'||_IOC_NR(cmd)<1||_IOC_NR(cmd)>22||_IOC_SIZE(cmd)!=sizeof(r))return -ENOTTY;
 if(copy_from_user(&r,(void __user *)arg,sizeof(r))){mgmt_early_error(d,f,cmd,entered,-EFAULT);return -EFAULT;}
 rc=vxl_uapi_validate(cmd,&r,sizeof(r),0);if(rc){mgmt_early_error(d,f,cmd,entered,rc);return rc;}
 if(r.wait_ms>60000){mgmt_early_error(d,f,cmd,entered,-EINVAL);return -EINVAL;}
 end=ktime_get_ns()/1000000ULL+r.wait_ms;
 do {
  lock_begin=ktime_get_ns();mutex_lock(&d->mutex);lock_wait+=ktime_get_ns()-lock_begin;
  if(d->engine.s8&&d->engine.owner==(unsigned long)f){
   if(cmd!=VXL_REAP)vxl_s8_timing_add(d->engine.s8,VXL_S8_MUTEX_WAIT,lock_begin,ktime_get_ns(),0);
   if(cmd==VXL_SUBMIT){d->engine.s8->submit_enter_ns=entered;vxl_s8_timing_event(d->engine.s8,VXL_S8_SUBMIT_ENTER,entered);}
   if(cmd==VXL_WAIT)vxl_s8_timing_event(d->engine.s8,VXL_S8_WAIT_ENTER,entered);
  }
  if(cmd==VXL_SUBMIT&&d->engine.profile_id==4&&d->engine.owner==(unsigned long)f&&d->engine.job.state==VXL_READY&&!d->submit_trace.active){request_time=vxl_time_begin(&d->submit_trace,d->engine.generation,entered);d->hunet.submit_time=request_time;}
  if(cmd==VXL_WAIT&&d->submit_trace.active&&d->submit_trace.active->job_id==r.job_id){request_time=d->submit_trace.active;if(!request_time->wait_enter_ns)request_time->wait_enter_ns=entered;}
  /* Scope cannot outlive this mutex-protected engine call, including errors.
     Payload/token/idle checks remain inside the unchanged engine handlers. */
  d->hunet.opt_load_locked=1;d->hunet.opt_load_owner=(unsigned long)f;d->hunet.opt_load_generation=d->engine.generation;
  d->cdma_fastwait_active=d->engine.profile_id==4&&(d->hunet.program_id==9||(d->hunet.program_id==10&&d->hunet.opt.release))&&d->engine.owner==(unsigned long)f&&d->engine.job.state==VXL_READY&&
   ((cmd==VXL_SUBMIT&&(r.trust_id==9||r.trust_id==10))||cmd==VXL_HUNET_BEGIN_ROI||cmd==VXL_HYBRID_CPU_BEGIN||cmd==VXL_HYBRID_CPU_COMMIT||cmd==VXL_HYBRID_END);
  if(d->engine.profile_id==4&&vxl_opt_load_fastwait_allowed(&d->hunet,cmd,cmd==VXL_COPY_TO?r.handle:0,cmd==VXL_COPY_TO?r.offset:0,cmd==VXL_COPY_TO?r.length:0))d->cdma_fastwait_active=true;
  management_time=mgmt_start_locked(d,f,cmd,entered);d->hunet.mgmt_row=management_time;
  rc=vxl_engine_call(&d->engine,(unsigned long)f,cmd,&r);
  if(d->engine.s8&&d->engine.owner==(unsigned long)f&&d->engine.s8->trace&&rc&&rc!=-ETIMEDOUT){
   d->engine.s8->trace->failed=1;
   if(!d->engine.s8->error_class)d->engine.s8->error_class=d->engine.quarantined?VXL_S8_RUNTIME_FAULT:VXL_S8_HOST_REJECT;
  }
  /* BEGIN creates its token inside the locked handler. Attribute this row to
     that actual transaction, including a later payload/envelope copy fault. */
  if(management_time&&cmd==VXL_HYBRID_CPU_BEGIN&&management_time->key.generation==d->engine.generation){management_time->key.token=d->hunet.hybrid.token;management_time->key.owner_cookie=((u64)d->engine.generation<<32)^d->hunet.hybrid.token;}
  d->cdma_fastwait_active=false;d->hunet.opt_load_locked=0;d->hunet.opt_load_owner=0;d->hunet.opt_load_generation=0;d->hunet.mgmt_row=NULL;
  if(!rc&&d->engine.hunet){vxl_hunet_delivery_capture(d->engine.hunet,cmd,&delivered);vxl_hybrid_delivery_capture(d->engine.hunet,cmd,&hybrid_delivered);}mutex_unlock(&d->mutex);
  if(cmd!=VXL_WAIT||rc!=-ETIMEDOUT||ktime_get_ns()/1000000ULL>=end)break;
  if(signal_pending(current)){timing_ioctl_done(d,request_time,cmd,-ERESTARTSYS,0,lock_wait,sleep_time,sleeps);return -ERESTARTSYS;}
  sleep_begin=ktime_get_ns();usleep_range(1000,2000);sleep_time+=ktime_get_ns()-sleep_begin;sleeps++;
  mutex_lock(&d->mutex);if(d->engine.s8&&d->engine.owner==(unsigned long)f)vxl_s8_timing_add(d->engine.s8,VXL_S8_WAIT_SLEEP,sleep_begin,ktime_get_ns(),0);mutex_unlock(&d->mutex);
 }while(1);
 /* An output-pointer fault after successful SUBMIT never unpins that job. */
 {u64 copy_begin=ktime_get_ns();int fault=copy_to_user((void __user *)arg,&r,sizeof(r));
  mutex_lock(&d->mutex);
  if(d->engine.s8&&d->engine.owner==(unsigned long)f){
   struct vxl_s8_ctx *c=d->engine.s8;u64 returned=ktime_get_ns();
   vxl_s8_timing_add(c,VXL_S8_DELIVERY,copy_begin,returned,sizeof(r));
   if(fault)vxl_s8_delivery_fault(c,-EFAULT);
   if(cmd==VXL_SUBMIT)vxl_s8_timing_add(c,VXL_S8_SUBMIT_IOCTL,entered,returned,0);
   if(cmd==VXL_WAIT){vxl_s8_timing_add(c,VXL_S8_WAIT_IOCTL,entered,returned,0);if(rc!=-ETIMEDOUT)vxl_s8_timing_event(c,VXL_S8_WAIT_RETURN,returned);}
   if(cmd==VXL_COPY_FROM||cmd==VXL_COPY_TO)vxl_s8_timing_add(c,cmd==VXL_COPY_FROM?VXL_S8_COPY_FROM_TIME:VXL_S8_COPY_TO_TIME,entered,returned,r.length);
   vxl_s8_trace_sync(c);
  }
  mutex_unlock(&d->mutex);
  if(fault){mgmt_ioctl_done(d,management_time,-EFAULT);timing_ioctl_done(d,request_time,cmd,-EFAULT,0,lock_wait,sleep_time,sleeps);return -EFAULT;}
 }
 if(!rc&&delivered.command){mutex_lock(&d->mutex);vxl_hunet_delivery_finish(d->engine.hunet,&delivered);mutex_unlock(&d->mutex);}
 if(!rc&&hybrid_delivered.command){mutex_lock(&d->mutex);vxl_hybrid_delivery_finish(d->engine.hunet,&hybrid_delivered);mutex_unlock(&d->mutex);}
 mgmt_ioctl_done(d,management_time,rc);timing_ioctl_done(d,request_time,cmd,rc,r.result,lock_wait,sleep_time,sleeps);return rc;
}
static long compat_file(struct file *f,unsigned int cmd,unsigned long arg){(void)f;(void)cmd;(void)arg;return -EOPNOTSUPP;}
const struct file_operations vxl_fops={.owner=THIS_MODULE,.open=open_file,.release=release_file,.unlocked_ioctl=ioctl_file,.compat_ioctl=compat_file,.poll=vxl_v2_linux_poll,.llseek=no_llseek};

/* Included after kernel copy/clock/mutex declarations. Old nodes retain their
 * original whitelist. No userspace completion object is ever accepted. */
static long mosaic_ioctl_file(struct file *f,unsigned int cmd,unsigned long arg,u64 entered){
 struct vxl_device *d=f->private_data;struct vxl_request r;struct mosaic_delivery delivery={0};struct mosaic_ctx *ctx=NULL;int rc;
 if(_IOC_TYPE(cmd)!='V'||_IOC_SIZE(cmd)!=sizeof(r)||_IOC_DIR(cmd)!=(_IOC_READ|_IOC_WRITE)||!((_IOC_NR(cmd)>=1&&_IOC_NR(cmd)<=11)||(_IOC_NR(cmd)>=65&&_IOC_NR(cmd)<=69)))return -ENOTTY;
 if(copy_from_user(&r,(void __user *)arg,sizeof(r))){
  mutex_lock(&d->mutex);ctx=d->engine.mosaic;
  if(ctx&&d->engine.profile_id==6&&d->engine.owner==(unsigned long)f&&(ctx->host->loaded||ctx->host->frame_active||ctx->host->job_active)){
   if(cmd==VXL_SUBMIT&&!ctx->host->evidence.job_id)ctx->host->evidence.observed.submit_enter=entered;
   (void)mosaic_state_fault(ctx,-EFAULT);
  }
  mutex_unlock(&d->mutex);return -EFAULT;
 }
 if(r.wait_ms>60000)return -EINVAL;
 u64 deadline=ktime_get_ns()/1000000ULL+r.wait_ms;
 do{
  mutex_lock(&d->mutex);ctx=d->engine.mosaic;
  if(!ctx||d->engine.profile_id!=6||d->engine.owner!=(unsigned long)f){mutex_unlock(&d->mutex);return -ESTALE;}
  if(cmd==VXL_SUBMIT)ctx->host->submit_enter_ns=entered;
  rc=vxl_engine_call(&d->engine,(unsigned long)f,cmd,&r);
  if(!rc&&(cmd==VXL_MOSAIC_EVIDENCE||cmd==VXL_COPY_FROM))mosaic_delivery_capture(ctx,&delivery);
  mutex_unlock(&d->mutex);
  if(cmd!=VXL_WAIT||rc!=-ETIMEDOUT||ktime_get_ns()/1000000ULL>=deadline)break;
  if(signal_pending(current))return -ERESTARTSYS;
  usleep_range(1000,2000);
 }while(1);
 int fault=copy_to_user((void __user *)arg,&r,sizeof(r));
 mutex_lock(&d->mutex);
 if(delivery.serial){int finish=mosaic_delivery_finish(ctx,&delivery,!fault&&!rc);if(!rc&&finish)rc=finish;}
 else if(fault&&ctx&&ctx->host->engine==&d->engine&&d->engine.mosaic==ctx&&(ctx->host->loaded||ctx->host->frame_active||ctx->host->job_active))(void)mosaic_state_fault(ctx,-EFAULT);
 mutex_unlock(&d->mutex);return fault?-EFAULT:rc;
}

#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/mosaic_runtime.h>
#include <vxl/profile_switch.h>
static struct vxl_object *object(struct vxl_engine *e,vxl_u64 h) {
 unsigned i;for(i=0;i<VXL_MAX_OBJECTS;i++)if(h&&e->objects[i].handle==h)return &e->objects[i];return 0;
}
int vxl_diagnostic_args(struct vxl_engine *e,const vxl_u32 addresses[3]) {
 unsigned i,j;int found;
 if(!e||!addresses)return -EINVAL;
 for(i=0;i<3;i++){
  found=0;
  for(j=0;j<VXL_MAX_OBJECTS;j++){
   const struct vxl_object *o=&e->objects[j];
   if(o->handle&&o->purpose==VXL_DATA&&!(addresses[i]&3)&&addresses[i]>=o->gpu&&!vxl_range_check(o->length,addresses[i]-o->gpu,256)){found=1;break;}
  }
  if(!found)return -ERANGE;
 }
 return 0;
}
static void destroy(struct vxl_engine *e,struct vxl_object *o) {
 if(o->allocated){e->ops->free(e->context,o);vxl_pool_free(&e->pools[o->domain-1],o->offset,o->allocated);}memset(o,0,sizeof(*o));
}
static void cleanup(struct vxl_engine *e) {
 unsigned i;if(e->hunet)vxl_hybrid_cleanup(e->hunet);for(i=0;i<VXL_MAX_OBJECTS;i++)if(e->objects[i].handle)destroy(e,&e->objects[i]);e->owner=0;
 if(e->hunet){e->hunet->sealed=0;e->hunet->active=0;e->hunet->end_pending=0;memset(&e->hunet->end_receipt,0,sizeof(e->hunet->end_receipt));e->hunet->seen_count=0;e->hunet->program_sealed=0;memset(&e->hunet->evidence,0,sizeof(e->hunet->evidence));memset(e->hunet->handles,0,sizeof(e->hunet->handles));}
}
void vxl_engine_init(struct vxl_engine *e,const struct vxl_ops *ops,void *ctx,vxl_u32 gen) {
 memset(e,0,sizeof(*e));e->ops=ops;e->context=ctx;e->generation=gen;e->profile_id=1;
 e->profile.ps_base=VXL_PS_BASE;e->profile.ps_size=VXL_PS_SIZE;e->profile.pl_base=VXL_PL_BASE;e->profile.pl_size=VXL_PL_SIZE;
 e->pools[0].capacity=VXL_PS_SIZE;e->pools[1].capacity=VXL_PL_SIZE;
 vxl_pool_reserve(&e->pools[0],VXL_FENCE-VXL_PS_BASE,64);e->job.state=VXL_READY;
}
int vxl_engine_init_profile(struct vxl_engine *e,const struct vxl_ops *ops,void *ctx,vxl_u32 gen,vxl_u32 profile) {
 int rc;
 if(profile!=1&&profile!=VXL_HUNET_PROFILE&&profile!=VXL_S8_PROFILE&&profile!=MOSAIC_PROFILE)return -EINVAL;
 vxl_engine_init(e,ops,ctx,gen);e->profile_id=profile;
 if(profile==VXL_S8_PROFILE||profile==MOSAIC_PROFILE) {
  rc=vxl_pool_reserve(&e->pools[0],0x2000,0x1000);if(rc)return rc;
  rc=vxl_pool_reserve(&e->pools[0],0xff000,0x1000);if(rc)return rc;
 }
 return 0;
}
int vxl_engine_open(struct vxl_engine *e,vxl_u64 owner) {
 if(e->quarantined)return -EIO;
 if(!owner)return -EINVAL;
 if(e->owner)return -EBUSY;
 if(e->generation==0xffffffffU)return -EOVERFLOW;
 if(e->profile_id==VXL_S8_PROFILE&&!e->s8)return -ENODEV;
 if(e->profile_id==MOSAIC_PROFILE&&(!e->mosaic||!e->mosaic->host||!e->mosaic->host->program||!e->mosaic->host->io.rw))return -ENODEV;
 if(e->s8&&(e->s8->capture_pending||e->s8->delivery_fault||e->s8->error_class))return -EBUSY;
 if(e->s8&&vxl_s8_trace_open_allowed(e->s8))return -EBUSY;
 e->generation++;if(e->hunet&&e->hunet->opt.release)memset(&e->hunet->opt.digest_audit,0,sizeof(e->hunet->opt.digest_audit));e->owner=owner;e->opened=1;e->job.state=VXL_READY;
 if(e->mosaic){e->mosaic->host->generation=e->generation;e->mosaic->host->owner=owner;}
 if(e->s8){vxl_s8_owner_open(e->s8);vxl_s8_trace_new_epoch(e->s8);}
 return 0;
}
void vxl_engine_close(struct vxl_engine *e,vxl_u64 owner) {
 if(e->mosaic){mosaic_engine_close(e,owner);return;}
 struct vxl_event ev={0};if(owner!=e->owner)return;
 if(e->s8)vxl_s8_trace_close(e->s8);
 if(e->s8&&(e->s8->capture_pending||e->job.pinned||e->job.state==VXL_RUNNING))vxl_s8_delivery_fault(e->s8,-ECANCELED);
 if(e->hunet&&vxl_hybrid_busy(e->hunet))vxl_hybrid_abort(e->hunet,-ECANCELED,0);
 e->opened=0;ev.kind=VXL_CLOSE;ev.id=e->job.id;vxl_job_step(&e->job,&ev);
 if(!e->job.pinned&&!e->quarantined)cleanup(e);
}
int vxl_engine_poll(struct vxl_engine *e) {
 if(e->mosaic){int result=mosaic_runtime_poll(e);return result<0?result:0;}
 struct vxl_event ev={0};int rc;
 if(e->hunet)vxl_hybrid_poll(e->hunet);
 if(e->job.state!=VXL_RUNNING)return 0;
 ev.kind=VXL_DONE;ev.id=e->job.id;rc=e->ops->poll(e->context,&ev);e->snapshot=ev;
 if(rc>0&&e->s8){rc=vxl_s8_complete(e,&ev);if(!rc)rc=1;}
 if(rc<0){ev.kind=VXL_ERROR;vxl_job_step(&e->job,&ev);}
 else if(rc>0){vxl_job_step(&e->job,&ev);}
 if(e->job.state==VXL_RUNNING && (e->hunet?e->hunet->io.now_ns(e->hunet->io.context)>=e->hunet->evidence.execution_deadline_ns:e->ops->now(e->context)>=e->job.deadline)){ev.kind=VXL_DEADLINE;vxl_job_step(&e->job,&ev);}
 if(e->job.state==VXL_QUARANTINED)e->quarantined=1;
 if(e->s8&&e->quarantined&&!e->s8->error_class)e->s8->error_class=VXL_S8_RUNTIME_FAULT;
 if(e->s8)vxl_s8_trace_sync(e->s8);
 if(e->hunet&&e->quarantined){e->hunet->evidence.software_state=VXL_QUARANTINED;e->hunet->evidence.result=e->job.result;}
 if(!e->opened&&!e->job.pinned&&!e->quarantined)cleanup(e);
 return 0;
}
/* Byte adapter over strictly aligned hardware transfers; padding belongs to object. */
static int bytes(struct vxl_engine *e,struct vxl_object *o,vxl_u64 off,void *buf,vxl_u64 len,int write) {
 vxl_u8 tmp[16],*p=buf;vxl_u64 n,aligned;int rc;
 if(e->hunet&&o->purpose==VXL_ARGS){if(vxl_range_check(128,off,len))return -ERANGE;if(write)memcpy(e->hunet->args_shadow+off,buf,len);else memcpy(buf,e->hunet->args_shadow+off,len);return 0;}
 while(len) {
  if((off&15)||len<16) {
   aligned=off&~15ULL;n=16-(off&15);if(n>len)n=len;
   rc=e->ops->transfer(e->context,o,aligned,tmp,16,0);if(rc)goto fail;
   if(write){memcpy(tmp+(off&15),p,(unsigned)n);rc=e->ops->transfer(e->context,o,aligned,tmp,16,1);if(rc)goto fail;}
   else memcpy(p,tmp+(off&15),(unsigned)n);
  } else {
   n=len&~15ULL;if(n>4096)n=4096;
   rc=e->ops->transfer(e->context,o,off,p,n,write);if(rc)goto fail;
  }
  off+=n;p+=n;len-=n;
 }
 return 0;
fail:
 if(rc==-EIO||rc==-ETIMEDOUT){e->quarantined=1;e->job.state=VXL_QUARANTINED;e->job.pinned=1;e->job.result=rc;}
 return rc;
}
static int allocate(struct vxl_engine *e,struct vxl_request *r,int reserve) {
 struct vxl_object *o=0;struct vxl_pool *pool;unsigned i;vxl_u64 n,off;int rc;
 if(r->domain!=VXL_PS&&r->domain!=VXL_PL)return -EINVAL;
 if(e->s8){rc=vxl_s8_reserve_check(r,reserve);if(rc)return rc;}
 if(e->hunet){rc=vxl_hunet_allocate_check(e->hunet,r,reserve);if(rc)return rc;}
 else if(r->purpose<VXL_CODE||r->purpose>VXL_DATA)return -EINVAL;
 if(!r->length)return -EINVAL;
 if(r->length>~(vxl_u64)0-63)return -EOVERFLOW;
 n=(r->length+63)&~63ULL;pool=&e->pools[r->domain-1];
 for(i=0;i<VXL_MAX_OBJECTS;i++)if(!e->objects[i].handle){o=&e->objects[i];break;}
 if(!o)return -ENOMEM;
 if(e->serial==0xffffffffULL)return -EOVERFLOW;
 if(e->hunet&&r->purpose==VXL_ARGS){o->domain=VXL_PS;o->purpose=VXL_ARGS;o->length=128;o->handle=((vxl_u64)e->generation<<32)|++e->serial;r->handle=o->handle;r->gpu_address=0;return 0;}
 if(reserve) {
  vxl_u64 base=r->domain==VXL_PS?VXL_PS_BASE:0x80000000ULL;
  if(r->address<base)return -ERANGE;
  off=r->address-base;rc=vxl_pool_reserve(pool,off,n);
 } else rc=vxl_pool_alloc(pool,r->length,r->alignment?r->alignment:64,&off);
 if(rc)return rc;
 o->domain=r->domain;o->purpose=r->purpose;o->offset=off;o->length=r->length;o->allocated=n;
 o->bus=(r->domain==VXL_PS?VXL_PS_BASE:VXL_PL_BASE)+off;
 rc=vxl_addr_encode(&e->profile,o->domain,o->bus,n,&o->gpu);
 if(!rc)rc=e->ops->alloc(e->context,o);
 if(rc){vxl_pool_free(pool,off,n);memset(o,0,sizeof(*o));return rc;}
 o->handle=((vxl_u64)e->generation<<32)|++e->serial;r->handle=o->handle;r->gpu_address=o->gpu;return 0;
}
int vxl_engine_call(struct vxl_engine *e,vxl_u64 owner,vxl_u32 cmd,struct vxl_request *r) {
 if(cmd==VXL_SELECT_PROFILE)return vxl_select_request(e,owner,r);
 if(e->mosaic)return mosaic_engine_call(e,owner,cmd,r);
 struct vxl_object *a,*b=0;struct vxl_event ev={0};vxl_u64 done,n;unsigned i;int rc;
 rc=vxl_uapi_validate(cmd,r,sizeof(*r),0);if(rc)return rc;
 if(e->profile_id==MOSAIC_PROFILE&&!e->mosaic)return -ENODEV;
 if(e->profile_id==4&&!e->hunet)return -ENODEV;
 if(e->profile_id==VXL_S8_PROFILE&&!e->s8)return -ENODEV;
 if(owner!=e->owner||!e->opened)return -EBADF;
 vxl_engine_poll(e);
 if(_IOC_NR(cmd)>=12){
  int readonly=cmd==VXL_HUNET_GET_BINDING||cmd==VXL_HUNET_GET_EVIDENCE||cmd==VXL_HYBRID_GET_BINDING||cmd==VXL_HYBRID_GET_CPU_EVIDENCE;
  if(!e->hunet)return -EOPNOTSUPP;
  if(e->job.state==VXL_RUNNING&&!readonly)return -EBUSY;
  if(e->quarantined&&!readonly)return -EIO;
  if(_IOC_NR(cmd)>=17)return vxl_hybrid_command(e->hunet,cmd,r);
  if(vxl_hybrid_busy(e->hunet)&&!readonly)return -EBUSY;
  return vxl_hunet_command(e->hunet,cmd,r);
 }
 if(cmd==VXL_GET_INFO||cmd==VXL_GET_STATUS){
  r->state=e->quarantined?VXL_QUARANTINED:e->job.state;r->generation=e->generation;r->profile_id=e->s8?VXL_S8_PROFILE:(e->hunet?4:0);r->alignment=64;
  r->capacity=VXL_PL_SIZE;r->used=0;r->capability=0x10001;
  for(i=0;i<VXL_MAX_OBJECTS;i++)if(e->objects[i].domain==VXL_PL)r->used+=e->objects[i].allocated;
  r->hwstatus=e->snapshot.status;r->errors=e->snapshot.errors;r->pending_w=e->snapshot.pending_w;r->pending_r=e->snapshot.pending_r;r->jobs=e->snapshot.jobs;r->result=e->job.result;r->job_id=e->job.id;return 0;
 }
 if(cmd==VXL_WAIT||cmd==VXL_REAP){
  if(!r->job_id||r->job_id!=e->job.id)return -ESTALE;
  r->state=e->job.state;r->result=e->job.result;
  if(cmd==VXL_WAIT)return e->job.state==VXL_RUNNING?-ETIMEDOUT:0;
  if(e->job.pinned||e->quarantined)return -EBUSY;
  if(e->hunet&&!e->hunet->evidence_delivered)return -EBUSY;
  if(e->s8&&vxl_s8_can_reap(e->s8))return -EBUSY;
  if(e->job.state!=VXL_COMPLETED&&e->job.state!=VXL_FAILED)return -EINVAL;
  memset(&e->job,0,sizeof(e->job));e->job.state=VXL_READY;if(e->s8)vxl_s8_reaped(e->s8);return 0;
 }
 if(e->quarantined)return -EIO;
 if(e->s8&&e->s8->capture_pending&&cmd!=VXL_COPY_FROM)return -EBUSY;
 if(e->hunet&&vxl_hybrid_busy(e->hunet))return -EBUSY;
 if(e->job.pinned)return -EBUSY;
 if(cmd==VXL_ALLOC||cmd==VXL_RESERVE)return allocate(e,r,cmd==VXL_RESERVE);
 a=object(e,r->handle);if(!a)return -EBADF;
 if(cmd==VXL_FREE){
  if(e->hunet&&(e->hunet->sealed||e->hunet->active)){
   if(!e->profile_factory||!e->profile_factory->enabled)return -EBUSY;
   rc=vxl_demo_hunet_release(e,owner);if(rc)return rc;
  }
  destroy(e,a);return 0;
 }
 if(cmd==VXL_SUBMIT){
  if(e->job.state!=VXL_READY)return -EBUSY;
  b=object(e,r->handle2);if(!b)return -EBADF;
  if(a->purpose!=VXL_CODE||(b->purpose!=VXL_ARGS&&b->purpose!=VXL_DATA))return -EINVAL;
  if(vxl_range_check(a->length,r->offset,4)||vxl_range_check(b->length,r->offset2,4)||(r->offset&3)||(r->offset2&3))return -EINVAL;
  if(e->hunet){if(r->trust_id!=6&&r->trust_id!=7&&r->trust_id!=8&&r->trust_id!=9&&r->trust_id!=10)return -EACCES;if(r->offset||r->offset2||b->purpose!=VXL_ARGS||r->deadline_ms!=60000||a->gpu!=VXL_HUNET_CODE)return -EINVAL;if((r->trust_id==6||r->trust_id==8||r->trust_id==9||r->trust_id==10)&&(a->handle!=e->hunet->handles[0]||b->handle!=e->hunet->handles[3]))return -EBADF;}
  else if(e->s8) {
   if(r->trust_id!=VXL_S8_PROGRAM)return -EACCES;
   if(r->offset || r->offset2!=64 || b->purpose!=VXL_ARGS || a->gpu!=0x80000000u || b->gpu!=0x80100000u || r->deadline_ms!=60000)return -EINVAL;
  }
  else if(r->trust_id<1||r->trust_id>5)return -EACCES;
  if(!r->deadline_ms||r->deadline_ms>60000)return -EINVAL;
  if(e->next_job==~(vxl_u64)0)return -EOVERFLOW;
  if(e->ops->now(e->context)>~(vxl_u64)0-r->deadline_ms)return -EOVERFLOW;
  rc=e->ops->prepare(e->context,e,a,r->offset,b,r->offset2,r->trust_id,&e->prepared);
  if(rc){
   if(rc==-EIO||rc==-ETIMEDOUT){e->quarantined=1;e->job.state=VXL_QUARANTINED;e->job.pinned=1;e->job.result=rc;}
   return rc;
  }
  e->job.id=++e->next_job;ev.kind=VXL_START;ev.id=e->job.id;
  rc=vxl_job_step(&e->job,&ev);if(rc)return rc;
  e->job.deadline=e->ops->now(e->context)+r->deadline_ms;
  rc=e->ops->start(e->context,&e->prepared,&e->job.jobs_before);
  if(rc){e->job.state=VXL_QUARANTINED;e->job.result=rc;e->quarantined=1;return rc;}
  r->job_id=e->job.id;return 0;
 }
 rc=vxl_range_check(a->length,r->offset,r->length);if(rc)return rc;
 if(cmd==VXL_COPY_DEVICE){
  b=object(e,r->handle2);if(!b)return -EBADF;
  rc=vxl_range_check(b->length,r->offset2,r->length);if(rc)return rc;
  if(a==b){if(r->offset==r->offset2)return 0;if(r->offset<r->offset2+r->length&&r->offset2<r->offset+r->length)return -EINVAL;}
 } else if(cmd!=VXL_COPY_TO&&cmd!=VXL_COPY_FROM)return -ENOTTY;
 else if(!r->user_ptr||r->length>~(vxl_u64)0-r->user_ptr)return -EFAULT;
 if(e->hunet){
  if(cmd!=VXL_COPY_FROM&&a->purpose==VXL_CODE)e->hunet->program_sealed=0;
  if(a->purpose!=VXL_ARGS&&!(a->purpose==VXL_DATA&&!e->hunet->sealed)){
   rc=vxl_hunet_check_access(e->hunet,(vxl_u64)a->gpu+r->offset,r->length,cmd!=VXL_COPY_FROM);if(rc)return rc;
  }
  if(b&&b->purpose!=VXL_ARGS&&!(b->purpose==VXL_DATA&&!e->hunet->sealed)){
   rc=vxl_hunet_check_access(e->hunet,(vxl_u64)b->gpu+r->offset2,r->length,0);if(rc)return rc;
  }
 }
 for(done=0;done<r->length;done+=n){
  n=r->length-done;if(n>4096)n=4096;
  if(cmd==VXL_COPY_TO){rc=e->ops->read_user(e->context,e->scratch,r->user_ptr+done,n);if(!rc)rc=bytes(e,a,r->offset+done,e->scratch,n,1);}
  else if(cmd==VXL_COPY_FROM){rc=bytes(e,a,r->offset+done,e->scratch,n,0);if(!rc)rc=e->ops->write_user(e->context,r->user_ptr+done,e->scratch,n);}
  else {rc=bytes(e,b,r->offset2+done,e->scratch,n,0);if(!rc)rc=bytes(e,a,r->offset+done,e->scratch,n,1);}
  if(!rc&&cmd==VXL_COPY_FROM&&e->s8&&e->s8->capture_pending)rc=vxl_s8_capture_range(e->s8,(vxl_u64)a->gpu+r->offset+done,n);
  if(rc&&cmd==VXL_COPY_FROM&&e->s8&&e->s8->capture_pending)vxl_s8_delivery_fault(e->s8,rc);
  if(rc){r->used=done;return rc;}
 }
 r->used=done;return 0;
}

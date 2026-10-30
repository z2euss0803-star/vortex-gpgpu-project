#include <vxl/mosaic_runtime.h>
static struct vxl_object *object(struct vxl_engine *e,vxl_u64 handle){for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(handle&&e->objects[i].handle==handle)return &e->objects[i];return NULL;}
static int owner(struct vxl_engine *e,vxl_u64 o){return e&&e->mosaic&&e->profile_id==6&&e->mosaic->host&&e->mosaic->host->engine==e&&e->opened&&o&&e->owner==o?0:-EBADF;}
static void destroy(struct vxl_engine *e,struct vxl_object *o){if(o->allocated){e->ops->free(e->context,o);vxl_pool_free(&e->pools[o->domain-1],o->offset,o->allocated);}memset(o,0,sizeof(*o));}
static int allocate(struct vxl_engine *e,struct vxl_request *r,int reserve){
 struct mosaic_host_state *h=e->mosaic->host;if(h->loaded||h->frame_active)return -EBUSY;
 int args=!reserve&&r->domain==VXL_PS&&r->purpose==VXL_ARGS&&r->length==128&&!r->address;
 if(!args){
  if(!reserve||r->domain!=VXL_PL)return -EACCES;
  int allowed=r->address==0x80000000ULL&&r->length==0x800000&&r->purpose==VXL_CODE;
  allowed|=r->address==0x80800000ULL&&r->length==0x1000000&&r->purpose==VXL_DATA;
  allowed|=r->address==0xfff00000ULL&&r->length==0x100000&&r->purpose==VXL_DATA;
  if(r->address==0x82000000ULL&&r->purpose==VXL_DATA)for(unsigned i=0;i<h->program->layout_count;++i)allowed|=r->length==h->program->layouts[i].layout.end-0x82000000ULL;
  if(!allowed)return -EACCES;
 }
 if(r->alignment&&r->alignment!=64)return -EINVAL;
 struct vxl_object *o=NULL;for(unsigned i=0;i<VXL_MAX_OBJECTS;++i){if(args&&e->objects[i].handle&&e->objects[i].purpose==VXL_ARGS)return -EEXIST;if(!e->objects[i].handle&&!o)o=&e->objects[i];}
 if(!o)return -ENOMEM;
 if(e->serial==0xffffffffULL)return -EOVERFLOW;
 if(args){*o=(struct vxl_object){.domain=VXL_PS,.purpose=VXL_ARGS,.length=128};}
 else{
  vxl_u64 off=r->address-0x80000000ULL;int rc=vxl_pool_reserve(&e->pools[1],off,r->length);if(rc)return rc;
  *o=(struct vxl_object){.domain=VXL_PL,.purpose=r->purpose,.offset=off,.length=r->length,.allocated=r->length,.gpu=r->address,.bus=VXL_PL_BASE+off};
  rc=e->ops->alloc(e->context,o);if(rc){vxl_pool_free(&e->pools[1],off,r->length);memset(o,0,sizeof(*o));return rc;}
 }
 o->handle=((vxl_u64)e->generation<<32)|++e->serial;r->handle=o->handle;r->gpu_address=o->gpu;return 0;
}
static int stage(struct mosaic_ctx *c,vxl_u32 kind,vxl_u32 offset,vxl_u32 bytes){
 struct mosaic_host_state *h=c->host;if(h->pending_delivery.serial)return -EBUSY;
 if(h->delivery_serial==~(vxl_u64)0)return -EOVERFLOW;
 h->pending_delivery=(struct mosaic_delivery){.serial=++h->delivery_serial,.owner=h->engine->owner,.job_id=h->evidence.job_id,.generation=h->engine->generation,.kind=kind,.offset=offset,.bytes=bytes};return 0;
}
void mosaic_delivery_capture(struct mosaic_ctx *c,struct mosaic_delivery *ticket){if(!ticket)return;memset(ticket,0,sizeof(*ticket));if(c&&c->host)*ticket=c->host->pending_delivery;}
int mosaic_delivery_finish(struct mosaic_ctx *c,const struct mosaic_delivery *ticket,int ok){
 if(!c||!c->host||!ticket)return -EINVAL;
 if(!ticket->serial)return 0;
 struct mosaic_host_state *h=c->host;const struct mosaic_delivery *p=&h->pending_delivery;
 if(ticket->serial!=p->serial||ticket->owner!=p->owner||ticket->job_id!=p->job_id||ticket->generation!=p->generation||ticket->kind!=p->kind||ticket->offset!=p->offset||ticket->bytes!=p->bytes||h->engine->generation!=p->generation||h->engine->owner!=p->owner)return -ESTALE;
 struct mosaic_delivery saved=*p;memset(&h->pending_delivery,0,sizeof(h->pending_delivery));if(!ok)return mosaic_state_fault(c,-EFAULT);
 return saved.kind==1?mosaic_evidence_delivered(c,saved.owner,saved.job_id,saved.offset,saved.bytes,1):saved.kind==2?mosaic_result_delivered(c,saved.owner,saved.offset,saved.bytes,1):-EINVAL;
}
static int control(struct vxl_engine *e,vxl_u32 cmd,struct vxl_request *r){
 struct mosaic_ctx *c=e->mosaic;struct mosaic_host_state *h=c->host;int rc;
 if(cmd==VXL_MOSAIC_BINDING)return e->ops->write_user(e->context,r->user_ptr,&h->program->binding,512);
 if(cmd==VXL_MOSAIC_EVIDENCE){
  if(r->job_id!=h->evidence.job_id)return -ESTALE;
  vxl_u8 wire[512];rc=mosaic_evidence_encode(c,wire);if(rc)return rc;
  if(r->offset!=h->evidence_bytes)return -EINVAL;
  rc=stage(c,1,r->offset,r->length);if(rc)return rc;
  rc=e->ops->write_user(e->context,r->user_ptr,wire+r->offset,r->length);
  if(rc){struct mosaic_delivery ticket; mosaic_delivery_capture(c,&ticket);(void)mosaic_delivery_finish(c,&ticket,0);}return rc;
 }
 if(e->quarantined)return h->first_error?h->first_error:-EIO;
 if(cmd==VXL_MOSAIC_SEAL){struct vxl_mosaic_seal_v1 s;rc=e->ops->read_user(e->context,&s,r->user_ptr,128);return rc?rc:mosaic_runtime_seal(c,&s);}
 struct vxl_mosaic_frame_v1 f;rc=e->ops->read_user(e->context,&f,r->user_ptr,128);if(rc)return rc;
 return cmd==VXL_MOSAIC_BEGIN?mosaic_runtime_begin(c,&f):cmd==VXL_MOSAIC_END?mosaic_runtime_end(c,&f):-ENOTTY;
}
int mosaic_engine_call(struct vxl_engine *e,vxl_u64 o,vxl_u32 cmd,struct vxl_request *r){
 int rc=owner(e,o);if(rc)return rc;
 struct mosaic_ctx *c=e->mosaic;struct mosaic_host_state *h=c->host;
 if(!h->program||!e->ops||!e->ops->read_user||!e->ops->write_user)return -ENODEV;
 int management=_IOC_NR(cmd)>=65;
 rc=management?mosaic_uapi_validate(cmd,r,sizeof(*r),0):vxl_uapi_validate(cmd,r,sizeof(*r),0);if(rc)return rc;
 if(!management&&_IOC_NR(cmd)>11)return -ENOTTY;
 if(r->wait_ms>60000)return -EINVAL;
 if((r->generation&&r->generation!=e->generation)||(r->profile_id&&r->profile_id!=6))return -ESTALE;
 if(h->pending_delivery.serial&&cmd!=VXL_GET_INFO&&cmd!=VXL_GET_STATUS)return -EBUSY;
 (void)mosaic_runtime_poll(e);
 if(management)return control(e,cmd,r);
 if(cmd==VXL_GET_INFO||cmd==VXL_GET_STATUS){
  r->state=e->quarantined?VXL_QUARANTINED:e->job.state;r->profile_id=6;r->generation=e->generation;r->alignment=64;r->capacity=VXL_PL_SIZE;r->used=0;r->capability=0x10001;
  for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(e->objects[i].domain==VXL_PL)r->used+=e->objects[i].allocated;
  r->result=e->job.result;r->job_id=e->job.id;r->hwstatus=e->snapshot.status;r->errors=e->snapshot.errors;r->pending_w=e->snapshot.pending_w;r->pending_r=e->snapshot.pending_r;r->jobs=e->snapshot.jobs;return 0;
 }
 if(e->quarantined)return h->first_error?h->first_error:-EIO;
 if(cmd==VXL_WAIT||cmd==VXL_REAP){
  if(!r->job_id||r->job_id!=e->job.id)return -ESTALE;
  r->state=e->job.state;r->result=e->job.result;
  return cmd==VXL_WAIT?(h->job_active?-ETIMEDOUT:0):mosaic_reap(c);
 }
 if(h->job_active||h->evidence_pending||e->job.pinned)return -EBUSY;
 if(cmd==VXL_ALLOC||cmd==VXL_RESERVE)return allocate(e,r,cmd==VXL_RESERVE);
 struct vxl_object *a=object(e,r->handle);if(!a)return -EBADF;
 if(cmd==VXL_FREE){if(h->frame_active)return -EBUSY;h->loaded=h->nonce_locked=h->input_written=0;memset(h->handles,0,sizeof(h->handles));destroy(e,a);return 0;}
 if(cmd==VXL_SUBMIT){
  if(r->trust_id!=12)return -EACCES;
  if(!h->loaded||r->handle!=h->handles[0]||r->handle2!=h->handles[4]||r->offset||r->offset2||r->deadline_ms!=60000)return -EINVAL;
  rc=mosaic_prepare(e,&e->prepared);if(!rc)rc=mosaic_runtime_start(c);if(!rc)r->job_id=e->job.id;return rc;
 }
 if(cmd!=VXL_COPY_TO&&cmd!=VXL_COPY_FROM)return -ENOTTY;
 if(!r->user_ptr||r->length>~(vxl_u64)0-r->user_ptr)return -EFAULT;
 if(vxl_range_check(a->length,r->offset,r->length))return -ERANGE;
 int write=cmd==VXL_COPY_TO,args=a->purpose==VXL_ARGS;vxl_u64 base=(vxl_u64)a->gpu+r->offset;vxl_u32 input_offset=0;
 if(args){if(!write||!h->loaded||!h->frame_active||r->handle!=h->handles[4])return -EACCES;}
 else if(write){
  if(h->loaded){
   vxl_u64 input=c->layout->region[MOSAIC_FP_A].address,total=(vxl_u64)c->layout->width*c->layout->height*12;
   if(h->frame_active||r->handle!=h->handles[2]||base<input||base-input>total||r->length>total-(base-input)||base-input!=h->input_written)return -EACCES;
   input_offset=base-input;
  }else if(!((a->purpose==VXL_CODE&&a->gpu==0x80000000u&&mosaic_program_code_span(h->program,r->offset,r->length))||(a->purpose==VXL_DATA&&a->gpu==0x80800000u&&r->offset<=h->program->assets_bytes&&r->length<=h->program->assets_bytes-r->offset)))return -EACCES;
 }else{
  vxl_u64 output=c->layout->region[MOSAIC_FP_A].address,total=(vxl_u64)c->layout->width*c->layout->height*12;
  if(!h->loaded||!h->frame_active||h->phase!=77||r->handle!=h->handles[2]||base<output||base-output>total||r->length>total-(base-output)||base-output!=h->result_bytes)return -EACCES;
  rc=stage(c,2,base-output,r->length);if(rc)return rc;
 }
 vxl_u64 done=0;
 while(done<r->length){vxl_u32 n=r->length-done;if(n>4096)n=4096;
  if(write){rc=e->ops->read_user(e->context,e->scratch,r->user_ptr+done,n);if(!rc){if(args)memcpy(h->args_shadow+r->offset+done,e->scratch,n);else rc=mosaic_io_bytes(c,base+done,e->scratch,n,1);}}
  else{rc=mosaic_io_bytes(c,base+done,e->scratch,n,0);if(!rc)rc=e->ops->write_user(e->context,r->user_ptr+done,e->scratch,n);}
  if(rc){r->used=done;if(h->pending_delivery.serial){struct mosaic_delivery ticket;mosaic_delivery_capture(c,&ticket);(void)mosaic_delivery_finish(c,&ticket,0);}else if(rc==-EIO||rc==-ETIMEDOUT)(void)mosaic_state_fault(c,rc);return rc;}
  done+=n;
 }
 if(write&&!args&&h->loaded)h->input_written=input_offset+r->length;
 r->used=done;return 0;
}
void mosaic_engine_close(struct vxl_engine *e,vxl_u64 o){
 if(owner(e,o))return;
 struct mosaic_ctx *c=e->mosaic;struct mosaic_host_state *h=c->host;
 if(h->frame_active||h->job_active||h->evidence_pending||h->pending_delivery.serial||e->quarantined||e->job.pinned){if(!h->first_error)(void)mosaic_state_fault(c,-ECANCELED);e->opened=0;e->job.closed=1;return;}
 int rc=mosaic_runtime_idle(c);if(rc){(void)mosaic_state_fault(c,rc);e->opened=0;e->job.closed=1;return;}
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(e->objects[i].handle)destroy(e,&e->objects[i]);
 e->owner=e->opened=0;h->owner=0;h->loaded=h->nonce_locked=h->input_written=h->frame_active=h->phase=h->first=h->sequence=h->epoch=0;
 memset(h->handles,0,sizeof(h->handles));memset(h->args_shadow,0,128);memset(h->nonce,0,16);memset(h->run_id,0,16);e->job=(struct vxl_job){.state=VXL_READY};
}

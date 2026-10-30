#include <vxl/v2/control.h>
static int zero(const void *p,vxl_u32 n){const vxl_u8 *b=p;for(vxl_u32 i=0;i<n;i++)if(b[i])return 0;return 1;}
static int object(struct vxl_v2_ctx *c,vxl_u64 owner,struct vxl_compute_v2_request *r,struct vxl_v2_object **out){
 for(unsigned int i=0;i<64;i++)if(c->objects[i].handle==r->handle){
  struct vxl_v2_object *o=&c->objects[i];struct vxl_v2_ref ref={o->handle,r->object_generation,0,o->length,o->access,o->domain,r->device_generation};
  return vxl_v2_ref_resolve(c,owner,&ref,0,out);
 }
 return -EBADF;
}
static int unpinned(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *ref,vxl_u32 access){
 struct vxl_v2_object *o;int rc=vxl_v2_ref_resolve(c,owner,ref,access,&o);if(rc)return rc;
 for(unsigned int i=0;i<5;i++)if(o->pins[i])return -EBUSY;
 return 0;
}
static int copy_buffer(struct vxl_v2_port *p,vxl_u64 owner,const struct vxl_v2_copy_desc *d){
 struct vxl_v2_ref source=d->source,dest=d->destination;vxl_u8 *tmp;vxl_u64 done=0,n;int rc;
 if(!d->bytes || d->bytes>67108864 || d->direction<1 || d->direction>3 || !zero(d->reserved,sizeof(d->reserved)))return -EINVAL;
 if(d->direction==1){if(!zero(&source,48) || dest.length!=d->bytes || !d->user_ptr)return -EINVAL;}
 else if(d->direction==2){if(!zero(&dest,48) || source.length!=d->bytes || !d->user_ptr)return -EINVAL;}
 else {if(d->user_ptr || source.length!=d->bytes || dest.length!=d->bytes)return -EINVAL;
  if(source.handle==dest.handle && source.offset<dest.offset+d->bytes && dest.offset<source.offset+d->bytes)return -EINVAL;
 }
 if(d->user_ptr && d->bytes>~(vxl_u64)0-d->user_ptr)return -EOVERFLOW;
 /* Admission before reading userspace prevents a pinned/sealed object from
  * turning an invalid source pointer into a transfer or partial overwrite. */
 if(d->direction!=1){rc=unpinned(p->ctx,owner,&source,1);if(rc)return rc;}
 if(d->direction!=2){rc=unpinned(p->ctx,owner,&dest,2);if(rc)return rc;}
 tmp=p->ops->allocate(p->os,4096);if(!tmp)return -ENOMEM;
 while(done<d->bytes){
  n=d->bytes-done;if(n>4096)n=4096;source.length=dest.length=n;
  if(d->direction==1)rc=p->ops->input(p->os,tmp,d->user_ptr+done,(vxl_u32)n);
  else rc=vxl_v2_buffer_copy(p->ctx,owner,&source,tmp,0);
  if(rc)break;
  if(d->direction==2)rc=p->ops->output(p->os,d->user_ptr+done,tmp,(vxl_u32)n);
  else rc=vxl_v2_buffer_copy(p->ctx,owner,&dest,tmp,1);
  if(rc)break;
  done+=n;source.offset+=n;dest.offset+=n;
 }
 p->ops->release(p->os,tmp);return rc;
}
int vxl_v2_control(struct vxl_v2_port *p,vxl_u64 owner,unsigned int cmd,struct vxl_compute_v2_request *r){
 struct vxl_v2_controls *control=p->control_context;struct vxl_v2_object *o;
 struct vxl_v2_create_desc create;struct vxl_v2_buffer_info info;struct vxl_v2_copy_desc copy;struct vxl_v2_program_desc desc;
 struct vxl_v2_program_ref program;unsigned int i;int rc;
 if(!control)return -ENODEV;
 if(r->flags || r->offset || r->length || r->sequence || r->cookie)return -EINVAL;
 if(cmd==VXL_V2_CAPS){struct vxl_v2_caps caps=control->caps;
  if(r->handle || r->object_generation || r->payload_bytes!=256 || !r->user_ptr)return -EINVAL;
  caps.device_generation=p->ctx->epoch;return p->ops->output(p->os,r->user_ptr,&caps,sizeof(caps));
 }
 if(cmd==VXL_V2_BUFFER_CREATE){
  if(r->handle || r->object_generation || r->payload_bytes!=64 || !r->user_ptr)return -EINVAL;
  rc=p->ops->input(p->os,&create,r->user_ptr,sizeof(create));if(rc)return rc;
  if(create.flags || !zero(create.reserved,sizeof(create.reserved)))return -EINVAL;
  if(create.purpose!=VXL_V2_ARGS && create.purpose!=VXL_V2_DATA)return -EACCES;
  memset(&info,0,sizeof(info));rc=vxl_v2_buffer_create(p->ctx,owner,r->device_generation,create.domain,create.purpose,create.access,create.bytes,&info.ref);if(rc)return rc;
  r->handle=info.ref.handle;r->object_generation=info.ref.object_generation;
  rc=object(p->ctx,owner,r,&o);if(rc)return rc;info.purpose=o->purpose;info.sealed=o->sealed;info.allocated=o->allocated;
  return p->ops->output(p->os,r->user_ptr,&info,sizeof(info));
 }
 if(cmd==VXL_V2_BUFFER_INFO || cmd==VXL_V2_BUFFER_RELEASE){
  if(cmd==VXL_V2_BUFFER_INFO?(r->payload_bytes!=64 || !r->user_ptr):(r->payload_bytes || r->user_ptr))return -EINVAL;
  rc=object(p->ctx,owner,r,&o);if(rc)return rc;memset(&info,0,sizeof(info));info.ref=(struct vxl_v2_ref){o->handle,o->generation,0,o->length,o->access,o->domain,o->epoch};info.purpose=o->purpose;info.sealed=o->sealed;info.allocated=o->allocated;
  return cmd==VXL_V2_BUFFER_RELEASE?vxl_v2_buffer_release(p->ctx,owner,&info.ref):p->ops->output(p->os,r->user_ptr,&info,sizeof(info));
 }
 if(cmd==VXL_V2_BUFFER_COPY){
  if(r->handle || r->object_generation || r->payload_bytes!=128 || !r->user_ptr)return -EINVAL;
  rc=p->ops->input(p->os,&copy,r->user_ptr,sizeof(copy));return rc?rc:copy_buffer(p,owner,&copy);
 }
 if(cmd==VXL_V2_PROGRAM_LOAD){
  if(r->handle || r->object_generation || r->payload_bytes!=64 || !r->user_ptr)return -EINVAL;
  rc=p->ops->input(p->os,&desc,r->user_ptr,sizeof(desc));if(rc)return rc;
  if(desc.handle || desc.object_generation || desc.device_generation || !zero(desc.reserved,sizeof(desc.reserved)))return -EINVAL;
  rc=vxl_v2_load_entry_file(p->ctx,owner,desc.entry_id,control->files,&program);if(rc)return rc;
 }else if(cmd==VXL_V2_PROGRAM_INFO || cmd==VXL_V2_PROGRAM_RELEASE){
  if(cmd==VXL_V2_PROGRAM_INFO?(r->payload_bytes!=64 || !r->user_ptr):(r->payload_bytes || r->user_ptr))return -EINVAL;
  if(!p->ctx->programs)return -ENODEV;
  for(i=0;i<16;i++)if(p->ctx->programs->programs[i].ref.handle==r->handle && r->handle)break;
  if(i==16)return -EBADF;
  program=p->ctx->programs->programs[i].ref;
  if(program.generation!=r->object_generation || program.device_generation!=r->device_generation)return -ESTALE;
  if(cmd==VXL_V2_PROGRAM_RELEASE)return vxl_v2_program_release(p->ctx,owner,&program);
 }else return -ENOTTY;
 memset(&desc,0,sizeof(desc));memcpy(desc.entry_id,program.entry_id,32);desc.handle=program.handle;desc.object_generation=program.generation;desc.device_generation=program.device_generation;
 r->handle=program.handle;r->object_generation=program.generation;return p->ops->output(p->os,r->user_ptr,&desc,sizeof(desc));
}

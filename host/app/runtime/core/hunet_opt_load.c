#include <vxl/engine.h>
#include <vxl/hunet.h>
int vxl_opt_load_fastwait_allowed(const struct vxl_hunet_ctx *c,vxl_u32 cmd,vxl_u64 handle,vxl_u64 offset,vxl_u64 bytes){
 const struct vxl_engine *e;const struct vxl_hunet_binding_v1 *b;unsigned i;
 if(!c||!c->engine||!c->opt.release||!c->opt.release->catalog||!(c->opt.release->catalog->runtime_flags&1u)||!c->opt_load_locked)return 0;
 e=c->engine;b=&c->opt.release->binding.v3.v2.v1;
 if(b->magic!=VXL_HUNET_BINDING_MAGIC||b->version!=4||b->bytes!=512||b->profile_id!=4||b->program_id!=10||b->features!=31)return 0;
 if(e->profile_id!=4||!e->opened||!e->owner||e->owner!=c->opt_load_owner||e->generation!=c->opt_load_generation)return 0;
 if(e->quarantined||e->job.pinned||e->job.state!=VXL_READY||c->sealed||c->active||c->end_pending||c->program_sealed||c->hybrid.end_pending||vxl_hybrid_busy(c))return 0;
 if(cmd==VXL_HYBRID_SEAL)return !handle&&!offset&&!bytes;
 if(cmd!=VXL_COPY_TO||!handle||!bytes||(vxl_u32)(handle>>32)!=e->generation)return 0;
 for(i=0;i<VXL_MAX_OBJECTS;i++){
  const struct vxl_object *o=&e->objects[i];vxl_u64 size;
  if(o->handle!=handle)continue;
  if(o->domain!=VXL_PL||vxl_range_check(o->length,offset,bytes))return 0;
  if(o->purpose==VXL_CODE&&o->gpu==VXL_HUNET_CODE&&o->length==8388608)size=c->opt.release->wire_bytes;
  else if(o->purpose==VXL_WEIGHTS&&o->gpu==VXL_HUNET_WEIGHTS&&o->length==67108864)size=23685056;
  else return 0;
  return !vxl_range_check(size,offset,bytes);
 }
 return 0;
}

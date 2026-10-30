#include <vxl/v2/objects.h>
int vxl_v2_ref_resolve(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r,vxl_u32 access,struct vxl_v2_object **out){
 unsigned int i;int rc;
 if(!r || !out)return -EINVAL;
 rc=vxl_v2_owner_check(c,owner,r->device_generation);if(rc)return rc;
 if(!r->handle || !r->object_generation)return -EBADF;
 if((r->domain!=VXL_PS && r->domain!=VXL_PL) || !r->access || (r->access & ~3u) || (access & ~3u))return -EINVAL;
 if(r->length>~(vxl_u64)0-r->offset)return -EOVERFLOW;
 for(i=0;i<64;i++){
  struct vxl_v2_object *o=&c->objects[i];
  if(o->handle!=r->handle)continue;
  if(o->generation!=r->object_generation || o->epoch!=r->device_generation)return -ESTALE;
  if(o->owner!=owner)return -EPERM;
  if(o->domain!=r->domain)return -EINVAL;
  if((r->access & ~o->access) || (access & ~r->access) || (o->sealed && (access & 2u)))return -EACCES;
  rc=vxl_range_check(o->length,r->offset,r->length);if(rc)return rc;
  *out=o;return 0;
 }
 return -EBADF;
}

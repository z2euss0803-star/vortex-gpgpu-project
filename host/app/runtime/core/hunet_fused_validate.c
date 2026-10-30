#include <vxl/hunet_fused.h>
#include <vxl/hunet.h>
#include <vxl/engine.h>
#include "../generated/hunet_fused/catalog.h"
static int all_zero(const vxl_u8 *p,size_t n){
 size_t i;for(i=0;i<n;i++)if(p[i])return 0;return 1;
}
int vxl_fused_payload_validate(vxl_u32 command,const void *payload,size_t bytes){
 const struct vxl_fused_binding_v3 *b=payload;
 const struct vxl_hunet_binding_v1 *v;
 size_t i;
 if(command!=VXL_HYBRID_GET_BINDING)return -ENOTTY;
 if(!payload)return -EFAULT;
 if(bytes!=sizeof(*b))return -EINVAL;
 v=&b->v2.v1;
 if(v->magic!=VXL_HUNET_BINDING_MAGIC||v->version!=3||v->bytes!=512||
    v->profile_id!=4||v->program_id!=9||v->features!=15)return -EINVAL;
 if(!all_zero(b->v2.reserved,32)||!all_zero(b->reserved,32))return -EINVAL;
 for(i=0;i<7;i++)if(all_zero((const vxl_u8 *)v+32+i*32,32))return -EINVAL;
 if(all_zero(b->v2.cpu_binary_sha256,32)||all_zero(b->v2.cpu_contract_sha256,32)||
    all_zero(b->v2.cpu_stages_sha256,32)||all_zero(b->fusion_contract_sha256,32)||
    all_zero(b->descriptors_sha256,32)||all_zero(b->layout_sha256,32))return -EINVAL;
 return 0;
}
const vxl_u32 *vxl_fused_catalog_job(vxl_u32 first){
 unsigned i;for(i=0;i<103;i++)if(vxl_fused_metadata[i].first==first)return vxl_fused_jobs[i];return NULL;
}
int vxl_fused_decode_meta(const void *args,vxl_u32 bytes,struct vxl_fused_meta *out){
 unsigned i;if(!args||!out||bytes!=128)return -EINVAL;
 for(i=0;i<103;i++)if(!memcmp(args,vxl_fused_jobs[i],128)){*out=vxl_fused_metadata[i];return 0;}
 return -EACCES;
}
int vxl_fused_validate_job(const struct vxl_hunet_ctx *c,const void *args,vxl_u32 next,struct vxl_fused_meta *out){
 struct vxl_fused_meta m;int rc;if(!c||c->program_id!=9||!next||!out)return -EINVAL;
 rc=vxl_fused_decode_meta(args,128,&m);if(rc)return rc;
 if(m.first!=next)return -EACCES;
 /* Exact canonical words bind all spans, descriptor offsets and reserved words;
    the generated layout was independently checked for bounds and aliases. */
 if(m.op!=15){struct vxl_hunet_job_v1 j;memcpy(&j,args,128);
  if(j.op<=3||j.op==6)return -EACCES;
  rc=vxl_hunet_validate_spans(&j,NULL);if(rc)return rc;
 }
 *out=m;return 0;
}

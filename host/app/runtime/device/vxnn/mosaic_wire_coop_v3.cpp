#include "mosaic_wire_coop_v3.h"
namespace {
bool span(uint32_t p,uint32_t n){return p>=0x80800000u&&n&&!(p%64)&&uint64_t(p)+n<=0xfff00000ULL;}
bool overlap(uint32_t p,uint32_t pn,uint32_t q,uint32_t qn){return uint64_t(p)<uint64_t(q)+qn&&uint64_t(q)<uint64_t(p)+pn;}
}
int vxnn_mosaic_validate_coop_v3(const VxnnMosaicArgs *a,bool relocated,VxnnMosaicShape *out){
 if(!a||!out||a->abi!=3||a->bytes!=128||(a->reserved&~uint32_t(MOSAIC_OPTION_CYCLES_V1)))return -EINVAL;
 uint32_t tree=0;
 if(a->opcode==3){
  /* Bound all factors before deriving the old norm extent. */
  if(a->h<2||a->h>960||a->w<2||a->w>1280||(a->stride!=1&&a->stride!=2)||(a->upsample!=1&&a->upsample!=2))return -EINVAL;
  const uint64_t oh=(uint64_t(a->h)*a->upsample+a->stride-1)/a->stride;
  const uint64_t ow=(uint64_t(a->w)*a->upsample+a->stride-1)/a->stride;
  if(oh>960||ow>1280)return -EINVAL;
  tree=1;while(tree<oh*ow)tree*=2;
 }
 MosaicWorkLayoutV1 layout{};
 int rc=mosaic_work_layout_v1(a->opcode,a->kpad,tree,&layout);if(rc)return rc;
 if(a->extent[VXM_WORK]!=layout.total_work_bytes)return -EINVAL;
 VxnnMosaicArgs legacy=*a;
 legacy.abi=a->opcode>=7?2:1;legacy.reserved=0;
 legacy.extent[VXM_WORK]=MOSAIC_WORK_PREFIX_BYTES+4*layout.compute_bytes;
 VxnnMosaicShape shape{};
 rc=vxnn_mosaic_validate_fused(&legacy,relocated,&shape);if(rc)return rc;
 if(shape.core_work_bytes!=layout.compute_bytes||shape.norm_elements!=tree)return -EINVAL;
 /* The legacy extent is deliberately smaller. Independently audit the actual
  * expanded WORK interval, including all added per-core raw/profile tails.
  * Never rely on the shadow packet to protect the true writable interval. */
 if(relocated){
  if(!span(a->address[VXM_WORK],a->extent[VXM_WORK]))return -EINVAL;
  const unsigned access[8]={1,2,3,1,1,3,3,3};
  for(unsigned i=0;i<8;i++)if(shape.used_mask&(1u<<i)){
   for(unsigned j=0;j<i;j++)if((shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
  }
  for(unsigned i=0;i<8;i++)if(access[i]==1){
   for(unsigned j=0;j<8;j++){
    if((access[j]&2)&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
    if(!(shape.used_mask&(1u<<i))&&(shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
   }
  }
 }
 shape.required[VXM_WORK]=layout.total_work_bytes;shape.core_work_bytes=layout.core_stride;
 *out=shape;return 0;
}

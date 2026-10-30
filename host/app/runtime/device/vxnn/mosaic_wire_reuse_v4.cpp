#include "mosaic_wire_reuse_v4.h"
namespace {
bool span(uint32_t p,uint32_t n,uint32_t alignment){return p>=0x80800000u&&n&&!(p%alignment)&&uint64_t(p)+n<=0xfff00000ULL;}
bool overlap(uint32_t p,uint32_t pn,uint32_t q,uint32_t qn){return uint64_t(p)<uint64_t(q)+qn&&uint64_t(q)<uint64_t(p)+pn;}
}
int vxnn_mosaic_validate_reuse_v4(const VxnnMosaicArgs*a,bool relocated,VxnnMosaicShape*out){
 if(!a||!out||a->abi!=MOSAIC_PRIVATE_ABI_REUSE_V4||a->bytes!=128||a->opcode<1||a->opcode>8||(a->reserved&~uint32_t(MOSAIC_OPTION_CYCLES_V1)))return -EINVAL;
 VxnnMosaicArgs shadow=*a;shadow.abi=3;
 if(a->opcode==2){
  if(!a->co||a->co>128||a->kpad<32||a->kpad>2592||a->kpad%32)return -EINVAL;
  const uint64_t padded=uint64_t((a->co+7)/8)*a->kpad*16;
  if(padded>UINT32_MAX||a->extent[VXM_WEIGHTS]!=padded)return -EINVAL;
  // Only the shadow's old geometry/role validation uses the old raw extent.
  // The real padded source is checked in full below, never inferred from size.
  shadow.extent[VXM_WEIGHTS]=a->co*a->kpad*2;
 }
 VxnnMosaicShape shape{};int rc=vxnn_mosaic_validate_coop_v3(&shadow,relocated,&shape);if(rc)return rc;
 if(relocated){
  if(!span(a->address[VXM_WORK],a->extent[VXM_WORK],64))return -EINVAL;
  if(a->opcode==2&&!span(a->address[VXM_WEIGHTS],a->extent[VXM_WEIGHTS],2))return -EINVAL;
  const unsigned access[8]={1,2,3,1,1,3,3,3};
  // Re-audit every true interval. Both expanded weights and expanded WORK,
  // including inactive readonly dummies, participate with their real extents.
  for(unsigned i=0;i<8;i++)if(shape.used_mask&(1u<<i))
   for(unsigned j=0;j<i;j++)if((shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
  for(unsigned i=0;i<8;i++)if(access[i]==1)for(unsigned j=0;j<8;j++){
   if((access[j]&2)&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
   if(!(shape.used_mask&(1u<<i))&&(shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
  }
 }
 if(a->opcode==2)shape.required[VXM_WEIGHTS]=a->extent[VXM_WEIGHTS];
 *out=shape;return 0;
}

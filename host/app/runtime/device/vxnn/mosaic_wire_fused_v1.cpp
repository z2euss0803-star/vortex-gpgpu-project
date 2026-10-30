// Independent fused ABI2 validator. Original ABI1 validator remains unchanged.
#include "mosaic_wire.h"
#include <errno.h>
namespace {
bool span(uint32_t p,uint32_t n,uint32_t alignment){return p>=0x80800000u&&n&&!(p%alignment)&&uint64_t(p)+n<=0xfff00000ULL;}
bool overlap(uint32_t p,uint32_t pn,uint32_t q,uint32_t qn){return uint64_t(p)<uint64_t(q)+qn&&uint64_t(q)<uint64_t(p)+pn;}
}
int vxnn_mosaic_validate_fused(const VxnnMosaicArgs*a,bool relocated,VxnnMosaicShape*out){
 if(a&&a->opcode>=1&&a->opcode<=6)return vxnn_mosaic_validate(a,relocated,out);
 if(!a||!out||a->abi!=2||a->bytes!=128||a->opcode<7||a->opcode>8||a->reserved||!a->count||a->count>4096)return -EINVAL;
 if((a->flags&~1u)||(a->scale_bits&0x80000000u)||!(a->scale_bits&0x7fffffffu)||(a->scale_bits&0x7f800000u)==0x7f800000u)return -EINVAL;
 if(!a->ci||a->ci>128||!a->co||a->co>128||a->h<2||a->h>960||a->w<2||a->w>1280||(a->k!=3&&a->k!=9)||(a->stride!=1&&a->stride!=2)||(a->upsample!=1&&a->upsample!=2))return -EINVAL;
 const uint64_t inner=uint64_t(a->ci)*a->k*a->k,oh=(uint64_t(a->h)*a->upsample+a->stride-1)/a->stride,ow=(uint64_t(a->w)*a->upsample+a->stride-1)/a->stride;
 if(inner>2592||a->kpad!=(inner+31)/32*32||uint64_t(a->h)*a->upsample<=a->k/2||uint64_t(a->w)*a->upsample<=a->k/2||oh>960||ow>1280)return -EINVAL;
 const uint64_t spatial=oh*ow,output=uint64_t(a->co)*spatial;
 VxnnMosaicShape shape{};uint64_t required[8];for(auto&n:required)n=64;shape.used_mask=1u<<VXM_WORK;
 auto use=[&](unsigned role,uint64_t bytes){required[role]=bytes;shape.used_mask|=1u<<role;};
 uint64_t items=0,core_work=0,tree=0;
 use(VXM_INPUT,output*4);use(VXM_QUANT,output*2);use(VXM_COEFF,a->co*16);use(VXM_STATS,a->co*12);
 if(a->opcode==8)use(VXM_SKIP,output*4);
 if(a->flags&1)use(VXM_OUTPUT,output*4);
 items=(output+255)/256;
 if(core_work)required[VXM_WORK]=64+core_work*4;
 if(!items||items>0xffffffffULL||a->first>=items||a->count>items-a->first)return -ERANGE;
 const unsigned access[8]={1,2,3,1,1,3,3,3};uint32_t readonly_dummy=0;
 for(unsigned i=0;i<8;i++){
  if(!required[i]||required[i]>0xffffffffULL||a->extent[i]!=required[i])return -EINVAL;
  shape.required[i]=uint32_t(required[i]);
  if(!relocated){if(a->address[i])return -EINVAL;}
  else if(!(shape.used_mask&(1u<<i))){
   if(a->extent[i]!=64)return -EINVAL;
   if(access[i]==1){if(!span(a->address[i],64,64)||(readonly_dummy&&readonly_dummy!=a->address[i]))return -EINVAL;readonly_dummy=a->address[i];}
   else if(a->address[i]!=a->address[VXM_WORK])return -EINVAL;
  }
  else if(!span(a->address[i],a->extent[i],i==VXM_WORK?64:i==VXM_QUANT||i==VXM_WEIGHTS?2:4))return -EINVAL;
 }
 // Unused writable roles retain the canonical WORK prefix. Unused readonly
 // roles use one independent aligned64 slice. Neither active nor inactive RO
 // may intersect ANY writable role, matching production driver protection.
 if(relocated)for(unsigned i=0;i<8;i++)if(shape.used_mask&(1u<<i))for(unsigned j=0;j<i;j++)if((shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
 if(relocated)for(unsigned i=0;i<8;i++)if(access[i]==1)for(unsigned j=0;j<8;j++){
  if((access[j]&2)&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
  if(!(shape.used_mask&(1u<<i))&&(shape.used_mask&(1u<<j))&&overlap(a->address[i],a->extent[i],a->address[j],a->extent[j]))return -EINVAL;
 }
 shape.items=uint32_t(items);shape.core_work_bytes=uint32_t(core_work);shape.norm_elements=uint32_t(tree);*out=shape;return 0;
}

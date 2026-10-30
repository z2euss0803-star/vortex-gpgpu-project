#include "hunet_wire.h"
#include "ranges.h"
#include "vendor/hunet/v5/include/hunet_job.h"
#include <errno.h>
#include <math.h>
#include <string.h>
namespace {
uint64_t align(uint64_t n,uint64_t a){return (n+a-1)/a*a;}
bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;}
}
int vxnn_hunet_validate(const VxnnHunetArgs*a,const HunetFusedDescriptorV1*ds,size_t nd,bool relocated,VxnnHunetShape*out){
 if(!a||!out||a->abi!=1||a->bytes!=128||!a->ci||a->ci>16384||!a->co||a->co>16384||!a->ih||a->ih>4096||!a->iw||a->iw>4096||!a->oh||a->oh>4096||!a->ow||a->ow>4096)return -EINVAL;
 const uint64_t hw=uint64_t(a->ih)*a->iw,ohw=uint64_t(a->oh)*a->ow,nx=uint64_t(a->ci)*hw,ny=uint64_t(a->co)*ohw;
 if(nx>16777216||ny>16777216)return -ERANGE;
 VxnnHunetShape shape{};uint64_t required[8]={nx*4,0,0,0,0,0,ny*4,16};
 if(a->op==15){
  if((a->flags&~18u)||((a->flags&2u)!=0&&a->flags!=2&&a->flags!=18)||a->groups||a->n_div||a->epsilon_bits||!a->k||a->k>7||!a->stride||a->stride>8||a->pad>7||!nd||nd>32||!vxnn_device::span(ds,nd*64,4))return -EINVAL;
  uint64_t m=a->co,n=ohw,k=uint64_t(a->ci)*a->k*a->k;
  if(a->flags&2){if(a->ci!=1||a->ih!=1||a->co!=a->iw||a->oh!=1||a->ow!=1||a->stride!=1||2*a->pad+1!=a->k)return -EINVAL;m=1;n=a->iw;k=a->k;}
  else if(uint64_t(a->ih)+2*a->pad<a->k||uint64_t(a->iw)+2*a->pad<a->k||(a->ih+2*a->pad-a->k)/a->stride+1!=a->oh||(a->iw+2*a->pad-a->k)/a->stride+1!=a->ow)return -EINVAL;
  if(k>16384)return -ERANGE;
  uint64_t next=0,packed=0,pack_rows=0,tiles=0;
  for(size_t i=0;i<nd;i++){const auto&d=ds[i];
   if(d.m!=m||d.n!=n||d.k!=k||d.mp!=align(m,8)||d.kp!=align(k,8)||!d.count||d.count>65536||d.np!=align(d.count,4)||d.offset!=next||uint64_t(d.offset)+d.count>n||d.packed_b_offset!=packed||d.pack_grid_base!=pack_rows||d.gemm_grid_base!=tiles||d.reserved[0]||d.reserved[1]||d.reserved[2]||d.original_first_sequence>d.original_last_sequence)return -EINVAL;
   next+=d.count;packed+=uint64_t(d.kp)*d.np*2;pack_rows+=d.kp;tiles+=uint64_t(d.mp/8)*(d.np/4);
  }
  if(next!=n||packed>67108864||pack_rows>UINT32_MAX||tiles>16777216)return -ERANGE;
  required[VXH_W]=align(m,8)*align(k,8)*2;required[VXH_BIAS]=(a->flags&16)?m*4:0;required[VXH_STATS]=nd*64;required[VXH_Y]=m*n*4;
  const uint64_t trace=align(packed,64);required[VXH_WORK]=trace+tiles*4;
  shape.tasks=uint32_t(tiles);shape.pack_rows=uint32_t(pack_rows);shape.gemm_tiles=uint32_t(tiles);shape.packed_bytes=uint32_t(packed);shape.trace_offset=uint32_t(trace);shape.descriptor_count=uint32_t(nd);
 }else{
  if(a->flags||ds||nd||a->k||a->stride||a->pad||(a->op!=HUNET_SLICE&&a->groups)||(a->op!=HUNET_SHIFT&&a->n_div)||(a->op!=HUNET_GN_STATS&&a->epsilon_bits))return -EINVAL;
  const bool same=a->ci==a->co&&a->ih==a->oh&&a->iw==a->ow;shape.tasks=uint32_t(ny);
  switch(a->op){
  case HUNET_GN_STATS:{float eps;memcpy(&eps,&a->epsilon_bits,4);if(!same||!isfinite(eps)||eps<=0)return -EINVAL;required[VXH_Y]=8;shape.tasks=1;break;}
  case HUNET_GN_APPLY:if(!same)return -EINVAL;required[VXH_GAMMA]=required[VXH_BETA]=a->ci*4;required[VXH_STATS]=8;break;
  case HUNET_ADD:if(!same)return -EINVAL;required[VXH_W]=ny*4;break;
  case HUNET_MUL_CHANNEL:if(!same)return -EINVAL;required[VXH_W]=a->co*4;break;
  case HUNET_COPY:case HUNET_SIGMOID:if(!same)return -EINVAL;break;
  case HUNET_SHIFT:if(!same||!a->n_div||a->n_div>a->ci)return -EINVAL;break;
  case HUNET_SLICE:if(a->ih!=a->oh||a->iw!=a->ow||uint64_t(a->groups)+a->co>a->ci)return -EINVAL;break;
  case HUNET_CONCAT:if(a->ih!=a->oh||a->iw!=a->ow||a->co<=a->ci)return -EINVAL;required[VXH_W]=(ny-nx)*4;break;
  case HUNET_GAP:if(a->ci!=a->co||ohw!=1)return -EINVAL;break;
  default:return -EINVAL; // CPU GELU and standalone old matrix opcodes excluded.
  }
 }
 bool alias[8]={};
 for(unsigned i=0;i<8;i++){
  if(!required[i]){alias[i]=true;required[i]=required[0];}
  if(required[i]>67108864||a->extent[i]!=required[i])return -ERANGE;
  shape.required[i]=uint32_t(required[i]);
  if(!relocated){if(a->address[i])return -EINVAL;}
  else if(a->address[i]<0x80800000u||uint64_t(a->address[i])+required[i]>0xfff00000u||a->address[i]%4||(alias[i]&&a->address[i]!=a->address[0]))return -ERANGE;
 }
 if(relocated){for(unsigned w=6;w<8;w++)for(unsigned r=0;r<w;r++)if(overlap(a->address[w],a->extent[w],a->address[r],a->extent[r]))return -EINVAL;}
 *out=shape;return 0;
}

#include "hunet_ops.h"
#include "ranges.h"
#include "device/hunet_fused_ops.h"
#include "vendor/hunet/v5/include/hunet_ordered_reductions.h"
#include <errno.h>
#include "hunet_scalar_math.h"
namespace {
bool descriptor(const HunetFusedDescriptorV1*d){return d&&d->m&&d->m<=16384&&d->n&&d->n<=16777216&&d->k&&d->kp>=d->k&&d->kp<=16384&&!(d->kp%8)&&d->mp>=d->m&&d->mp<=16384&&!(d->mp%8)&&d->count&&d->np>=d->count&&d->np<=65536&&!(d->np%4)&&uint64_t(d->offset)+d->count<=d->n;}
bool floats(const void*p,uint64_t n){return n&&n<=SIZE_MAX/4&&vxnn_device::span(p,size_t(n)*4,4);}
}
int vxnn_hunet_pack_row(const HunetFusedStageV1*s,const HunetFusedDescriptorV1*d,const float*x,size_t xc,uint16_t*y,size_t yc,uint32_t row,uint32_t lane,uint32_t lanes){
 if(!s||!descriptor(d)||lanes!=4||lane>=lanes||row>=d->kp||!s->ci||s->ci>16384||!s->co||s->co>16384||!s->ih||s->ih>4096||!s->iw||s->iw>4096||!s->oh||s->oh>4096||!s->ow||s->ow>4096||!s->kernel_k||s->kernel_k>7||!s->stride||s->stride>8||s->pad>7||(s->flags!=0&&s->flags!=2))return -EINVAL;
 const uint64_t input=uint64_t(s->ci)*s->ih*s->iw,output=uint64_t(d->kp)*d->np;
 if(input>UINT32_MAX||xc<input||output>SIZE_MAX/2||yc<output||!floats(x,input)||!vxnn_device::span(y,size_t(output)*2,2)||vxnn_device::overlap(x,size_t(input)*4,y,size_t(output)*2))return -EINVAL;
 if(s->flags==2){if(d->m!=1||d->n!=s->iw||d->k!=s->kernel_k)return -EINVAL;}
 else if(d->m!=s->co||d->n!=uint64_t(s->oh)*s->ow||d->k!=uint64_t(s->ci)*s->kernel_k*s->kernel_k)return -EINVAL;
 hunet_fused_pack_row(*s,*d,x,y,row,lane,lanes);return 0;
}
int vxnn_hunet_store_fragment(const HunetFusedDescriptorV1*d,float*out,size_t oc,const float*bias,size_t bc,uint32_t row,uint32_t col,uint32_t lane,const float*acc,size_t ac){
 if(!descriptor(d)||lane>=4||row%8||row>=d->mp||col%4||col>=d->np||ac<8||!floats(acc,8))return -EINVAL;
 const uint64_t size=uint64_t(d->m)*d->n;if(oc<size||!floats(out,size)||vxnn_device::overlap(out,size_t(size)*4,acc,32))return -EINVAL;
 if(bias){if(bc<d->m||!floats(bias,d->m)||vxnn_device::overlap(out,size_t(size)*4,bias,size_t(d->m)*4))return -EINVAL;}else if(bc)return -EINVAL;
 for(uint32_t reg=0;reg<8;reg++){hunet_fused_store(*d,out,bias,row,col,lane,reg,acc[reg]);}
 return 0;
}
int vxnn_hunet_gn_stats(const float*x,size_t n,float epsilon,float*out,size_t oc){
 if(!n||n>UINT32_MAX||!floats(x,n)||oc<2||!floats(out,2)||vxnn_device::overlap(x,n*4,out,8)||!isfinite(epsilon)||epsilon<=0)return -EINVAL;
 // Exactly the frozen one-chain order; no parallel merge, FMA or GPU fallback.
 const float mean=hunet_kahan_sum_ordered4(x,uint32_t(n))/float(n);
 const float invstd=1.0f/sqrtf(hunet_variance_sum_ordered4(x,uint32_t(n),mean)/float(n)+epsilon);
 if(!isfinite(mean)||!isfinite(invstd))return -ERANGE;
 out[0]=mean;out[1]=invstd;return 0;
}
int vxnn_hunet_scalar(const HunetJobV1*j,const VxnnHunetScalarBuffers*b,uint32_t first,uint32_t count){
 if(!j||!b||!hunet_valid_job(j)||j->flags||j->dtype!=1||j->activation||j->x||j->w||j->bias||j->gamma||j->beta||j->y||j->scratch||j->aux||!count||first>=j->tasks||count>j->tasks-first)return -EINVAL;
 if(!j->ci||j->ci>16384||!j->co||j->co>16384||!j->ih||j->ih>4096||!j->iw||j->iw>4096||!j->oh||j->oh>4096||!j->ow||j->ow>4096)return -EINVAL;
 const uint64_t hw=uint64_t(j->ih)*j->iw,ohw=uint64_t(j->oh)*j->ow,nx=uint64_t(j->ci)*hw,ny0=uint64_t(j->co)*ohw;
 if(nx>16777216||ny0>16777216)return -EINVAL;
 uint64_t ny=ny0,nw=0,ng=0,ns=0,tasks=ny;
 const bool same=j->ci==j->co&&j->ih==j->oh&&j->iw==j->ow;
 switch(j->op){
 case HUNET_GN_STATS:
  if(!same||!isfinite(hunet_float(j->epsilon_bits))||hunet_float(j->epsilon_bits)<=0)return -EINVAL;
  ny=2;tasks=1;break;
 case HUNET_GN_APPLY:if(!same)return -EINVAL;ng=j->ci;ns=2;break;
 case HUNET_ADD:if(!same)return -EINVAL;nw=ny;break;
 case HUNET_MUL_CHANNEL:if(!same)return -EINVAL;nw=j->co;break;
 case HUNET_COPY:case HUNET_SIGMOID:if(!same)return -EINVAL;break;
 case HUNET_SHIFT:if(!same||!j->n_div||j->n_div>j->ci)return -EINVAL;break;
 case HUNET_SLICE:if(j->ih!=j->oh||j->iw!=j->ow||uint64_t(j->groups)+j->co>j->ci)return -EINVAL;break;
 case HUNET_CONCAT:if(j->ih!=j->oh||j->iw!=j->ow||j->co<=j->ci)return -EINVAL;nw=ny-nx;break;
 case HUNET_GAP:if(j->ci!=j->co||ohw!=1)return -EINVAL;break;
 default:return -EINVAL; // No CPU GELU or matrix fallback in this boundary.
 }
 if(j->tasks!=tasks||j->nitems!=(j->op==HUNET_GN_STATS?nx:ny)||b->y.count<ny||!floats(b->y.data,ny))return -EINVAL;
 const VxnnHunetRead reads[]={b->x,b->w,b->gamma,b->beta,b->stats};const uint64_t sizes[]={nx,nw,ng,ng,ns};
 for(size_t i=0;i<5;i++){
  if(!sizes[i]){if(reads[i].data||reads[i].count)return -EINVAL;continue;}
  if(reads[i].count<sizes[i]||!floats(reads[i].data,sizes[i])||vxnn_device::overlap(reads[i].data,size_t(sizes[i])*4,b->y.data,size_t(ny)*4))return -EINVAL;
 }
 for(uint32_t t=first;t<first+count;t++){
  vxnn_hunet_scalar_math(j,t,*b);
  if(j->op==HUNET_GN_STATS){if(!isfinite(b->y.data[0])||!isfinite(b->y.data[1]))return -ERANGE;}
  else if(!isfinite(b->y.data[t]))return -ERANGE;
 }
 return 0;
}

#pragma once
#include "../vendor/hunet/v5/include/hunet_job.h"
#include "../vendor/hunet/v5/include/hunet_math.h"
struct OptScalarRange {uint32_t first,end;bool valid;};
static inline bool opt_scalar_supported(uint32_t op){return op>=5&&op<=14&&op!=6;}
static inline uint32_t opt_scalar_grid(uint32_t tasks,uint32_t chunk){if(!tasks||(chunk!=1&&chunk!=4&&chunk!=16))return 0;return uint32_t((uint64_t(tasks)+4*chunk-1)/(4*chunk));}
static inline OptScalarRange opt_scalar_range(uint32_t tasks,uint32_t block,uint32_t lane,uint32_t chunk){
 if(lane>=4||!opt_scalar_grid(tasks,chunk))return {0,0,false};
 uint64_t first=uint64_t(block)*4*chunk+lane,end=uint64_t(block+uint64_t(1))*4*chunk;
 if(first>=tasks)return {0,0,false};
 if(end>tasks)end=tasks;
 return {uint32_t(first),uint32_t(end),true};
}
static inline bool opt_scalar_job_valid(const HunetJobV1&j){
 if(!opt_scalar_supported(j.op)||j.dtype!=1||!j.tasks||!j.ci||!j.co||!j.ih||!j.iw||!j.oh||!j.ow||j.tasks>8494080)return false;
 if(uint64_t(j.ci)*j.ih>8494080||uint64_t(j.co)*j.oh>8494080)return false;
 if(uint64_t(j.ci)*j.ih*j.iw>8494080||uint64_t(j.co)*j.oh*j.ow>8494080)return false;
 return j.op!=7||j.n_div;
}
static inline void opt_scalar_write(float*y,uint32_t t,float value,int&error){if(!isfinite(value))error|=2;else y[t]=value;}
static inline int opt_scalar_span(const HunetJobV1&j,uint32_t t,uint32_t end,uint32_t step){
 if(!opt_scalar_job_valid(j)||!step||t>end||end>j.tasks)return 1;
 const auto*x=reinterpret_cast<const float*>(uintptr_t(j.x));const auto*w=reinterpret_cast<const float*>(uintptr_t(j.w));auto*y=reinterpret_cast<float*>(uintptr_t(j.y));
 const auto*g=reinterpret_cast<const float*>(uintptr_t(j.gamma));const auto*b=reinterpret_cast<const float*>(uintptr_t(j.beta));const auto*stats=reinterpret_cast<const float*>(uintptr_t(j.scratch));
 uint32_t hw=j.ih*j.iw;int error=0;
 switch(j.op){
 case 5:{float mean=stats[0],inv=stats[1];while(t<end){uint32_t c=t/hw,stop=(c+1)*hw;if(stop>end)stop=end;float gamma=g[c],beta=b[c];for(;t<stop;t+=step){opt_scalar_write(y,t,((x[t]-mean)*inv)*gamma+beta,error);}}break;}
 case 7:{uint32_t group=j.ci/j.n_div;while(t<end){uint32_t c=t/hw,channel_end=(c+1)*hw;if(channel_end>end)channel_end=end;int dx=0,dy=0;if(c<group)dx=1;else if(c<2*group)dx=-1;else if(c<3*group)dy=1;else if(c<4*group)dy=-1;
  while(t<channel_end){uint32_t row=(t-c*hw)/j.iw,col=t-c*hw-row*j.iw,stop=c*hw+(row+1)*j.iw;if(stop>channel_end)stop=channel_end;int iy=int(row)+dy;
   for(;t<stop;t+=step,col+=step){int ix=int(col)+dx;float value=iy>=0&&ix>=0&&iy<int(j.ih)&&ix<int(j.iw)?x[c*hw+uint32_t(iy)*j.iw+uint32_t(ix)]:0.0f;opt_scalar_write(y,t,value,error);}
  }}break;}
 case 8:for(;t<end;t+=step){opt_scalar_write(y,t,x[t]+w[t],error);}break;
 case 9:for(;t<end;t+=step){float sum=0,carry=0;for(uint32_t i=0;i<hw;i++){hunet_kahan_add(x[t*hw+i],&sum,&carry);}opt_scalar_write(y,t,sum/float(hw),error);}break;
 case 10:for(;t<end;t+=step){opt_scalar_write(y,t,1.0f/(1.0f+expf(-x[t])),error);}break;
 case 11:{uint32_t spatial=j.oh*j.ow;while(t<end){uint32_t c=t/spatial,stop=(c+1)*spatial;if(stop>end)stop=end;float scale=w[c];for(;t<stop;t+=step){opt_scalar_write(y,t,x[t]*scale,error);}}break;}
 case 12:x+=j.groups*hw;for(;t<end;t+=step){opt_scalar_write(y,t,x[t],error);}break;
 case 13:{uint32_t cut=j.ci*hw;for(;t<end&&t<cut;t+=step){opt_scalar_write(y,t,x[t],error);}for(;t<end;t+=step){opt_scalar_write(y,t,w[t-cut],error);}break;}
 case 14:for(;t<end;t+=step){opt_scalar_write(y,t,x[t],error);}break;
 default:return 1;
 }
 return error;
}

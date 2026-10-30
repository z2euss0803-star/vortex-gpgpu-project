#include "ops.h"
#include "ranges.h"
#include <errno.h>
int vxnn_i48_bias(const int64_t*in,size_t ic,const int32_t*bias,size_t bc,uint32_t m,uint32_t n,int64_t*out,size_t oc,uint32_t core){
 using namespace vxnn_device;const size_t count=size_t(m)*n;
 if(!matrix(m,n)||core>3||ic<count||bc<n||oc<count)return -EINVAL;
 if(!span(in,count*8,8)||!span(bias,size_t(n)*4,4)||!span(out,count*8,8)||overlap(in,count*8,out,count*8)||overlap(bias,size_t(n)*4,out,count*8))return -EINVAL;
 for(size_t i=0;i<count;i++)if(!signed48(in[i])||!signed48(in[i]+int64_t(bias[i%n])))return -ERANGE;
 for(size_t i=core;i<count;i+=4)out[i]=in[i]+int64_t(bias[i%n]);
 return 0;
}
int vxnn_i48_relu(const int64_t*in,size_t count,int64_t*out,size_t oc,uint32_t core){
 using namespace vxnn_device;
 if(!count||count>4096||core>3||oc<count||!span(in,count*8,8)||!span(out,count*8,8)||overlap(in,count*8,out,count*8))return -EINVAL;
 for(size_t i=0;i<count;i++)if(!signed48(in[i]))return -ERANGE;
 for(size_t i=core;i<count;i+=4)out[i]=in[i]>0?in[i]:0;
 return 0;
}

#include "tile.h"
#include <errno.h>
int mosaic_unpack48_range(const uint32_t raw[96],int64_t*out,uint32_t first,uint32_t count){
 if(!raw||!out||first>64||count>64-first)return -EINVAL;
 for(uint32_t i=0;i<count;++i){
  const uint32_t bit=(first+i)*48,word=bit/32,shift=bit%32;
  const uint64_t pair=uint64_t(raw[word])|(uint64_t(raw[word+1])<<32);
  const uint64_t value=(pair>>shift)&0xffffffffffffULL;
  out[i]=(value&(UINT64_C(1)<<47))?int64_t(value)-(INT64_C(1)<<48):int64_t(value);
 }
 return 0;
}
int mosaic_unpack48(const uint32_t raw[96],int64_t out[64]){return mosaic_unpack48_range(raw,out,0,64);}

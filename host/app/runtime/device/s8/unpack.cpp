#include "unpack.h"
#include <errno.h>
int s8_device_unpack(const uint32_t raw[96],int64_t out[64]){
 if(!raw||!out)return -EINVAL;
 for(unsigned i=0;i<64;++i){unsigned bit=i*48,word=bit/32,shift=bit%32;uint64_t pair=uint64_t(raw[word])|(uint64_t(raw[word+1])<<32);uint64_t value=(pair>>shift)&UINT64_C(0xffffffffffff);out[i]=(value&(UINT64_C(1)<<47))?int64_t(value)-(INT64_C(1)<<48):int64_t(value);}
 return 0;
}

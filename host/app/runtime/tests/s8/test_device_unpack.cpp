#include "../../device/s8/unpack.h"
#include "../../tools/s8/reference.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
int main(){
 uint32_t raw[96]={0};int64_t got[64],expected[64];uint8_t bytes[384];
 const int64_t values[]={0,1,-1,(INT64_C(1)<<47)-1,-(INT64_C(1)<<47),INT64_C(1)<<42};
 for(unsigned i=0;i<64;++i){uint64_t v=uint64_t(values[i%6])&UINT64_C(0xffffffffffff);for(unsigned b=0;b<6;++b)bytes[i*6+b]=uint8_t(v>>(8*b));}
 memcpy(raw,bytes,384);assert(!s8_device_unpack(raw,got));assert(!s8_unpack48(bytes,expected));assert(!memcmp(got,expected,sizeof(got)));
 assert(s8_device_unpack(nullptr,got)<0 && s8_device_unpack(raw,nullptr)<0);
 puts("S8_DEVICE_UNPACK_EXACT_CROSSWORD_AND_SIGN48_PASS SOFTWARE_ONLY");
}

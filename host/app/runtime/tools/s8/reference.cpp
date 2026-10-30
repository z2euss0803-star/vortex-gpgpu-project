#include "reference.h"
#include <cerrno>
#include <cstring>
int s8_reference(const int16_t *a,const int16_t *b,uint32_t k,int64_t out[64]) {
 if(!a || !b || !out || k<1 || k>4096)return -EINVAL;
 for(uint32_t i=0;i<8;++i) {
  for(uint32_t j=0;j<8;++j) {
   int64_t value=0;
   for(uint32_t t=0;t<k;++t)value+=int64_t(a[t*8+i])*int64_t(b[t*8+j]);
   out[i*8+j]=value;
  }
 }
 return 0;
}
int s8_unpack48_checked(const uint8_t *raw,size_t bytes,int64_t out[64]) {
 if(!raw || !out || bytes!=384)return -EINVAL;
 for(uint32_t i=0;i<64;++i) {
  uint64_t word=0;
  for(uint32_t j=0;j<6;++j)word|=uint64_t(raw[6*i+j])<<(8*j);
  out[i]=(word&(uint64_t(1)<<47))?int64_t(word)-281474976710656LL:int64_t(word);
 }
 return 0;
}
int s8_unpack48(const uint8_t raw[384],int64_t out[64]) {
 return s8_unpack48_checked(raw,384,out);
}
static uint16_t small_half(int x){return x==0?0:x==1?0x3c00:0xbc00;}
int s8_compat_inputs(uint32_t id,uint8_t *a,uint32_t capacity_a,uint32_t *ab,uint8_t *b,uint32_t capacity_b,uint32_t *bb){
 if(!a||!b||!ab||!bb||id<2||id>5)return -EINVAL;
 uint32_t na=id==2?256:id==3?128:16,nb=id==2?256:id==3?64:id==4?40:32;
 if(capacity_a<na||capacity_b<nb)return -ENOSPC;
 *ab=na;*bb=nb;
 if(id==2){for(uint32_t i=0;i<64;++i){uint32_t x=i,y=63-i;memcpy(a+i*4,&x,4);memcpy(b+i*4,&y,4);}}
 else if(id==3){for(int i=0;i<8;++i)for(int k=0;k<8;++k){uint16_t x=small_half((i+k)%3-1);memcpy(a+(i*8+k)*2,&x,2);}for(int k=0;k<8;++k)for(int j=0;j<4;++j){uint16_t x=small_half((k+j)%3-1);memcpy(b+(k*4+j)*2,&x,2);}}
 else{const uint32_t rgba[]={0,0x04040404,0x08080808,0x0c0c0c0c},uv[]={0,0,65536,0,0,65536,65536,65536,32768,32768};memcpy(a,rgba,16);memcpy(b,uv,nb);}
 return 0;
}
int s8_compare_compat(uint32_t id,const uint8_t *raw,uint32_t bytes){
 uint32_t n=id==2?256:id==3?128:id==4?20:id==5?16:0;
 if(!raw||!n||bytes!=n)return -EINVAL;
 for(unsigned i=0;i<n/4;++i){uint32_t actual;memcpy(&actual,raw+i*4,4);if(id==3){int sum=0;for(int k=0;k<8;++k)sum+=((int(i/4)+k)%3-1)*((k+int(i%4))%3-1);float value=float(sum);uint32_t expected;memcpy(&expected,&value,4);if(actual!=expected)return -EDOM;}else if(actual!=(id==2?63u:(i==4?6u:i*4u)*0x01010101u))return -EDOM;}
 return 0;
}

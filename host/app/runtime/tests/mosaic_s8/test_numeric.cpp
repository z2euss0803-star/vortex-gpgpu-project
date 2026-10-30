#include <vxl/mosaic_numeric.h>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <vector>
#include <cstdio>
int main(){
 float input[]={.5f,1.5f,-.5f,-1.5f,1.e30f,-1.e30f};int16_t q[6]={};
 const int16_t expected[]={0,2,0,-2,32767,-32767};
 assert(!mosaic_quantize_rne(input,6,1,q));assert(!memcmp(q,expected,sizeof(q)));
 float bad=NAN;assert(mosaic_quantize_rne(&bad,1,1,q)<0);
 assert(mosaic_quantize_rne(input,6,0,q)<0);
 for(size_t k:{size_t(1),size_t(2592),size_t(4096)}){
  std::vector<int16_t>a(k,-32768),b(k,-32768);int64_t out=0;
  assert(!mosaic_dot_i64(a.data(),b.data(),k,&out));assert(out==static_cast<int64_t>(k)*1073741824);
 }
 {int16_t a=0;int64_t out=0;assert(mosaic_dot_i64(&a,&a,0,&out)<0);assert(mosaic_dot_i64(&a,&a,4097,&out)<0);}
 float out=0;uint32_t bits=0;
 assert(!mosaic_restore_f32(16777217,0x1p-24f,1.0f+0x1p-23f,-1.0f,&out));memcpy(&bits,&out,4);assert(bits==0x34000000);
 assert(mosaic_restore_f32(INT64_C(1)<<47,1,1,0,&out)<0);
 assert(!fesetround(FE_DOWNWARD));assert(mosaic_quantize_rne(input,6,1,q)<0);assert(!fesetround(FE_TONEAREST));
 puts("MOSAIC_NATIVE_NUMERIC_PRIMITIVES_HOST_PASS_NOT_NETWORK_NOT_GPU");
}

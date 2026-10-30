#include "device/vxnn/ops.h"
#include "tests/mosaic_s8/fake_s8.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void need(bool b,const char*why){if(!b){std::fprintf(stderr,"U11_INT16_ASSERT %s\n",why);std::exit(61);}}
static void matmul(uint32_t m,uint32_t n,uint32_t k,bool zero){
 std::vector<int16_t>a(m*k),b(k*n);std::vector<int64_t>out(m*n,123456),expected(m*n);
 uint32_t seed=0x16534899;
 for(auto&v:a){seed=seed*1664525u+1013904223u;v=zero?0:int16_t(seed>>16);}
 for(auto&v:b){seed=seed*1664525u+1013904223u;v=zero?0:int16_t(seed>>16);}
 if(!zero){a[0]=INT16_MIN;b[0]=INT16_MIN;a.back()=INT16_MAX;b.back()=INT16_MIN;}
 // Independent scalar reference over row-major matrices, not tile production.
 for(uint32_t r=0;r<m;r++)for(uint32_t c=0;c<n;c++)for(uint32_t t=0;t<k;t++)expected[r*n+c]+=int64_t(a[r*k+t])*int64_t(b[t*n+c]);
 unsigned starts=0,steps=0;
 for(uint32_t core=0;core<4;core++){fake_s8 f;auto io=f.ops();io.core=core;need(!vxnn_i16_matmul(a.data(),a.size(),b.data(),b.size(),m,n,k,out.data(),out.size(),&io),"native signed INT16 PE matmul must run");starts+=f.begins;steps+=f.steps;need(f.begins==f.releases,"every successful tile drains and retires");}
 need(out==expected,"all signed INT16 values equal independent i64 reference");need(starts==(m/8)*(n/8)&&steps==starts*k,"bounded tile count uses original native PE");
}
static void rejects(){
 int16_t a[512]={},b[512]={};int64_t out[512];for(auto&v:out)v=98765;fake_s8 f;auto io=f.ops();
 for(auto dims:std::array<std::array<uint32_t,3>,7>{{{{0,8,1}},{{7,8,1}},{{8,9,1}},{{72,8,1}},{{8,8,0}},{{8,8,65}},{{8,72,1}}}})need(vxnn_i16_matmul(a,512,b,512,dims[0],dims[1],dims[2],out,512,&io)==-EINVAL,"reject unsupported shape before PE");
 need(vxnn_i16_matmul(a,7,b,8,8,8,1,out,64,&io)==-EINVAL,"short input rejected");need(vxnn_i16_matmul(a,8,b,8,8,8,1,out,63,&io)==-EINVAL,"short output rejected");need(!f.begins,"invalid input never starts PE");
 f.fail_step=0;need(vxnn_i16_matmul(a,8,b,8,8,8,1,out,64,&io)<0,"original PE error propagates");need(f.releases==0,"failed tile never guessed released");for(auto v:out)need(v==98765,"first failed tile is not published");
}
static void post(){
 const int64_t hi=(INT64_C(1)<<47)-1,lo=-(INT64_C(1)<<47);
 std::vector<int64_t>input(128),biased(128,111),rectified(128,222);int32_t bias[8]={INT32_MIN,INT32_MAX,-1,1,0,27,-31,6};
 for(size_t i=0;i<input.size();i++)input[i]=int64_t(i)*17-1000;
 for(uint32_t core=0;core<4;core++)need(!vxnn_i48_bias(input.data(),input.size(),bias,8,16,8,biased.data(),biased.size(),core),"bounded signed i32 bias broadcast");
 for(size_t i=0;i<input.size();i++)need(biased[i]==input[i]+bias[i%8],"bias equals independent i64 reference");
 for(uint32_t core=0;core<4;core++)need(!vxnn_i48_relu(biased.data(),biased.size(),rectified.data(),rectified.size(),core),"relu signed48 input");
 for(size_t i=0;i<input.size();i++)need(rectified[i]==(biased[i]>0?biased[i]:0),"relu preserves exact integer values");
 int32_t ones[8]={1,1,1,1,1,1,1,1};input[0]=hi;biased.assign(128,111);need(vxnn_i48_bias(input.data(),128,ones,8,16,8,biased.data(),128,0)==-ERANGE,"reject positive i48 overflow");for(auto v:biased)need(v==111,"overflow rejects before output write");
 input[0]=lo;ones[0]=-1;need(vxnn_i48_bias(input.data(),128,ones,8,16,8,biased.data(),128,0)==-ERANGE,"reject negative i48 overflow");input[0]=hi+1;need(vxnn_i48_relu(input.data(),128,rectified.data(),128,0)==-ERANGE,"reject non i48 input");
 input[0]=0;need(vxnn_i48_relu(input.data(),128,input.data(),128,0)==-EINVAL,"no unplanned in-place alias");need(vxnn_i48_bias(input.data(),128,ones,8,16,8,input.data(),128,0)==-EINVAL,"bias read/write overlap rejected");need(vxnn_i48_relu(input.data(),128,rectified.data(),128,4)==-EINVAL,"reject nonexistent fifth core");
}
int main(){for(uint32_t k:{1u,8u,64u}){matmul(8,8,k,false);matmul(16,8,k,false);matmul(64,64,k,false);}matmul(8,8,8,true);rejects();post();puts("U11_REAL_MATH_SOURCE_HOST_NATIVE_PE_PROTOCOL_MODEL_NOT_RTL_NOT_BOARD gpu_raw_cycles=null");}

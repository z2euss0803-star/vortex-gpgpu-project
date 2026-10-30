#include <vxnn/cpu.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cfenv>
#include <vector>
#include <fstream>
#include <string>
using namespace vxnn;
static float reference(float x){return (x*.5f)*(1.0f+erff(x/1.4142135623730951f));}
static std::vector<float> read_values(const std::string&p,size_t n){std::vector<float> out(n);std::ifstream f(p,std::ios::binary);assert(f);f.read(reinterpret_cast<char*>(out.data()),n*4);assert(f.gcount()==std::streamsize(n*4)&&f.peek()==EOF);for(float v:out)assert(std::isfinite(v));return out;}
static void captured(CpuPool&pool,const std::string&input,const std::string&output){
 const unsigned ids[]={8,17,28,37,49,55,61,67,73,79,87,93,98};const size_t sizes[]={393216,393216,196608,196608,98304,98304,98304,98304,98304,98304,49152,49152,1280};
 for(size_t k=0;k<13;k++){
  char name[40];std::snprintf(name,sizeof(name),"%03u",ids[k]);size_t n=sizes[k];auto x=read_values(input+"/data/"+name+"-input.f32",n);auto gpu=read_values(input+"/data/"+name+"-gpu_reference.f32",n);auto old=read_values(input+"/a53-erf-reference/"+name+"-output.f32",n);std::vector<float> y(n);auto result=gelu_erf_pool(pool,x.data(),y.data(),n);assert(result);size_t gpu_diff=0,old_diff=0;double gpu_max=0,old_max=0;
  for(size_t i=0;i<n;i++){float scalar=reference(x[i]);assert(!std::memcmp(&scalar,&y[i],4));gpu_diff+=std::memcmp(&gpu[i],&y[i],4)!=0;old_diff+=std::memcmp(&old[i],&y[i],4)!=0;gpu_max=std::max(gpu_max,std::fabs(double(gpu[i])-y[i]));old_max=std::max(old_max,std::fabs(double(old[i])-y[i]));}
  std::ofstream f(output+"/"+name+"-host-output.f32",std::ios::binary);assert(f);f.write(reinterpret_cast<char*>(y.data()),n*4);assert(f);auto&t=*result.value;
  std::printf("{\"scope\":\"UBUNTU_REAL_INPUT_HOST_REPLAY_NOT_A53_OR_NETWORK\",\"stage\":%u,\"elements\":%zu,\"bit_diff_scalar\":0,\"bit_diff_old_a53\":%zu,\"bit_diff_gpu_reference\":%zu,\"max_abs_old_a53\":%.17g,\"max_abs_gpu_reference\":%.17g,\"total_ns\":%llu,\"queue_wait_ns\":%llu,\"compute_ns\":%llu,\"validate_ns\":%llu,\"copy_ns\":%llu,\"gpu_raw_cycles\":null}\n",ids[k],n,old_diff,gpu_diff,old_max,gpu_max,(unsigned long long)t.total_ns,(unsigned long long)t.queue_wait_ns,(unsigned long long)t.compute_ns,(unsigned long long)t.validate_ns,(unsigned long long)t.copy_ns);
 }
 puts("U08_REAL13_INPUT_HOST_SCALAR_BITWISE_REPLAY_PASS_NOT_A53");
}
int main(int argc,char**argv){CpuPool pool(4,32);for(size_t n:{size_t(1),size_t(3),size_t(4),size_t(11),size_t(1280),size_t(49152),size_t(393216)}){
 std::vector<float> x(n),y(n+16);for(size_t i=0;i<n;i++)x[i]=(int(i%2001)-1000)/79.f;std::memset(y.data(),0xa5,y.size()*sizeof(float));auto result=gelu_erf_pool(pool,x.data(),y.data(),n);assert(result&&result.value->workers==4&&result.value->end_ns>=result.value->begin_ns);
 for(size_t i=0;i<n;i++){float r=reference(x[i]);assert(!std::memcmp(&r,&y[i],4));}for(size_t i=n*4;i<y.size()*4;i++)assert(reinterpret_cast<unsigned char*>(y.data())[i]==0xa5);
 }
 if(argc==3)captured(pool,argv[1],argv[2]);else assert(argc==1);
 float x[8]={1,2,3,4,5,6,7,8},y[8];std::memset(y,0xa5,sizeof(y));auto cancel=std::make_shared<CpuCancel>();cancel->cancel();assert(!gelu_erf_pool(pool,x,y,8,cancel));for(auto b:std::vector<unsigned char>(reinterpret_cast<unsigned char*>(y),reinterpret_cast<unsigned char*>(y)+sizeof(y)))assert(b==0xa5);
 assert(!gelu_erf_pool(pool,x,x,8));assert(!gelu_erf_pool(pool,x,y,0));assert(!gelu_erf_pool(pool,x,y,393217));x[0]=NAN;assert(!gelu_erf_pool(pool,x,y,8));x[0]=1;assert(!fesetround(FE_DOWNWARD));assert(!gelu_erf_pool(pool,x,y,8));assert(!fesetround(FE_TONEAREST));assert(pool.shutdown());CpuPool short_pool(2,32);assert(short_pool.ready());assert(!gelu_erf_pool(short_pool,x,y,8));puts("U08_GELU_SCALAR_BITWISE_BOUNDARY_CANCEL_PASS_HOST_NOT_A53");}

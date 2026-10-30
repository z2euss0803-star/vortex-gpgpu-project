#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include "../../device/hunet_opt_gn.h"
#include "../../vendor/hunet/v5/include/hunet_ordered_reductions.h"
static void check(const std::vector<float>&x,const char *kind){
 const uint32_t n=uint32_t(x.size()),w=opt_gn_workers(n);float p[64]={},v[64]={};uint64_t count=0;uint32_t end=0;
 double mean64=0,var64=0;
 for(float f:x)mean64+=double(f);
 mean64/=n;
 for(float f:x){double d=double(f)-mean64;var64+=d*d;}var64/=n;
 for(uint32_t i=0;i<w;i++){auto r=opt_gn_range(n,i,w);assert(r.valid&&r.begin==end);end=r.end;count+=r.end-r.begin;p[i]=opt_gn_partial(x.data(),r,0,false);}
 assert(end==n&&count==n);float mean=opt_gn_merge(p,w)/float(n);
 for(uint32_t i=0;i<w;i++)v[i]=opt_gn_partial(x.data(),opt_gn_range(n,i,w),mean,true);
 float variance=opt_gn_merge(v,w)/float(n);
 assert(std::isfinite(mean)&&std::isfinite(variance)&&variance>=0);
 assert(std::abs(double(mean)-mean64)<=2e-4+2e-6*std::abs(mean64));
 assert(std::abs(double(variance)-var64)<=1e-8+2e-5*std::abs(var64));
 float old_mean=hunet_kahan_sum_ordered4(x.data(),n)/float(n);
 float old_var=hunet_variance_sum_ordered4(x.data(),n,old_mean)/float(n);
 if(w==1){assert(mean==old_mean&&variance==old_var);}
 double mean_err=std::abs(double(mean)-mean64),var_err=std::abs(double(variance)-var64);
 std::printf("{\"scope\":\"HOST_GN_MATH_NOT_GPU\",\"kind\":\"%s\",\"n\":%u,\"workers\":%u,\"mean_abs\":%.12g,\"variance_abs\":%.12g,\"stats_rmse\":%.12g,\"stats_nrmse\":%.12g,\"old_mean_difference\":%.12g,\"old_variance_difference\":%.12g}\n",kind,n,w,mean_err,var_err,std::sqrt((mean_err*mean_err+var_err*var_err)/2),std::sqrt((mean_err*mean_err+var_err*var_err)/(mean64*mean64+var64*var64+1e-30)),double(mean-old_mean),double(variance-old_var));
}
int main(int argc,char **argv){
 if(argc==2){std::ifstream f(argv[1],std::ios::binary|std::ios::ate);assert(f);auto bytes=f.tellg();assert(bytes>0&&bytes%4==0);std::vector<float>x(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(x.data()),bytes);assert(f);check(x,"real_GELU_capture_values_not_GN_layer");return 0;}
 const uint32_t sizes[]={1,4095,4096,4097,65535,65536,65539};
 for(auto n:sizes){assert(opt_gn_workers(n)==(n<4096?1:n<65536?16:64));for(unsigned kind=0;kind<4;kind++){
  std::vector<float>x(n);for(uint32_t i=0;i<n;i++)x[i]=kind==0?3.0f:kind==1?1.0f+float(i%3)*1e-6f:kind==2?(i%2?-10000.0f:10000.0f)+0.125f:float(std::sin(double(i)*0.17));check(x,"synthetic_boundary");
 }}
 assert(!opt_gn_workers(0));assert(!opt_gn_range(0,0,1).valid);assert(!opt_gn_range(100,0,2).valid);assert(!opt_gn_range(100,64,64).valid);
 assert(std::isnan(opt_gn_partial(nullptr,{0,1,false},0,false)));assert(std::isnan(opt_gn_merge(nullptr,16)));
 uint32_t n;assert(!opt_gn_count(0,1,1,&n));assert(!opt_gn_count(UINT32_MAX,2,2,&n));assert(opt_gn_count(32,16,16,&n)&&n==8192);
 auto r=opt_gn_range(UINT32_MAX,63,64);assert(r.valid&&r.end==UINT32_MAX);
 puts("GN_BOUNDARY_PARTITION_KAHAN_FP64_COMPARISON_PASS_NOT_GPU");return 0;
}

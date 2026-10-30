#include <vxnn/cpu.hpp>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <time.h>
namespace vxnn {namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
}
static Result<CpuTiming> gelu_impl(CpuPool&pool,const float*x,float*y,size_t n,std::shared_ptr<CpuCancel>cancel,uint64_t token){
 if(!x||!y||!n||n>393216||pool.workers()!=4||fegetround()!=FE_TONEAREST)return Result<CpuTiming>::failure({EINVAL,"GELU","requires four CPUs, finite bounded input and RNE"});
 uintptr_t a=reinterpret_cast<uintptr_t>(x),b=reinterpret_cast<uintptr_t>(y);size_t bytes=n*sizeof(float);
 if((a%alignof(float))||(b%alignof(float))||a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-bytes||(a<b+bytes&&b<a+bytes))return Result<CpuTiming>::failure({EINVAL,"GELU","input/output alignment or overlap"});
 uint64_t begin=now();if(!begin)return Result<CpuTiming>::failure({EIO,"GELU","clock"});
 for(size_t i=0;i<n;i++)if(!std::isfinite(x[i]))return Result<CpuTiming>::failure({EDOM,"GELU","nonfinite input"});
 uint64_t input_end=now();
 try{
  std::vector<float> staging(n);
  RangeFn compute=[&](size_t first,size_t last){for(size_t i=first;i<last;i++){float v=x[i];staging[i]=(v*.5f)*(1.0f+erff(v/1.4142135623730951f));}return Result<void>::success();};
  auto result=token?pool.run_ranges_reserved(n,compute,cancel,token):pool.run_ranges(n,compute,cancel);
  if(!result)return Result<CpuTiming>::failure(*result.error);
  uint64_t validate_begin=now();for(float v:staging)if(!std::isfinite(v))return Result<CpuTiming>::failure({EDOM,"GELU","nonfinite output"});uint64_t validate_end=now();
  if(cancel&&cancel->cancelled.load())return Result<CpuTiming>::failure({ECANCELED,"GELU","cancelled before publication"});
  uint64_t copy_begin=now();std::memcpy(y,staging.data(),bytes);uint64_t end=now();
  if(input_end<begin||validate_begin<input_end||validate_end<validate_begin||copy_begin<validate_end||end<copy_begin)return Result<CpuTiming>::failure({EIO,"GELU","clock order"});
  CpuTiming timing=*result.value;timing.begin_ns=begin;timing.end_ns=end;timing.total_ns=end-begin;timing.validate_ns=(input_end-begin)+(validate_end-validate_begin);timing.copy_ns=end-copy_begin;return Result<CpuTiming>::success(timing);
 }catch(const std::bad_alloc&){return Result<CpuTiming>::failure({ENOMEM,"GELU","staging allocation"});}
}
Result<CpuTiming> gelu_erf_pool(CpuPool&pool,const float*x,float*y,size_t n,std::shared_ptr<CpuCancel>cancel){return gelu_impl(pool,x,y,n,std::move(cancel),0);}
Result<CpuTiming> gelu_erf_pool_reserved(CpuPool&pool,const float*x,float*y,size_t n,std::shared_ptr<CpuCancel>cancel,uint64_t token){if(!token)return Result<CpuTiming>::failure({EINVAL,"GELU","missing borrowed reservation"});return gelu_impl(pool,x,y,n,std::move(cancel),token);}
}

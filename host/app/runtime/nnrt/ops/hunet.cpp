#include <vxnn/ops_hunet.hpp>
#include <vxl/hunet.h>
#include <cerrno>
#include <time.h>
#include <cfenv>
#include <cmath>
#include <cstring>
#include "vendor/hunet/v5/include/hunet_ordered_reductions.h"
namespace vxnn {namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
struct Finish {HunetGeluReceipt&r;bool close(){r.end_ns=now();if(!r.end_ns||r.end_ns<r.begin_ns)return false;r.total_ns=r.end_ns-r.begin_ns;return true;}~Finish(){if(!r.end_ns)close();}};
struct Interval {
 uint64_t&duration;uint64_t start;
 explicit Interval(uint64_t&v):duration(v),start(now()){}
 bool close(){uint64_t end=now(),begin=start;start=0;if(!begin||end<begin)return false;duration+=end-begin;return true;}
 ~Interval(){if(start)close();} // Unwinding preserves the throwing phase duration.
};
bool valid(const BufferRef&b,uint64_t size,uint32_t access){return b.handle&&b.object_generation&&b.device_generation&&b.bytes==size&&(b.access&access)==access&&!(b.access&~3u)&&b.offset<=UINT64_MAX-b.bytes;}
}
Result<void> execute_hunet_gelu(const HunetGeluTask&t,IGpuBackend&backend,CpuPool&pool,std::shared_ptr<CpuCancel>cancel,uint64_t token,HunetGeluReceipt&r){
 if(r.begin_ns||r.end_ns||r.phase!=HunetGeluPhase::NONE)return Result<void>::failure({ESTALE,"HUNET_GELU","fresh receipt"});
 r.begin_ns=now();r.phase=HunetGeluPhase::VALIDATE;Finish finish{r};
 auto fail=[&](Error error){r.error=error.code;return Result<void>::failure(std::move(error));};
 auto stopped=[&](){return cancel&&cancel->cancelled.load();};
 if(!r.begin_ns)return fail({EIO,"HUNET_GELU","clock"});
 if(!t.node||!token||!t.elements||t.elements>393216||pool.workers()!=4||!valid(t.input,t.elements*4,1)||!valid(t.output,t.elements*4,3)||t.input.device_generation!=t.output.device_generation)return fail({EINVAL,"HUNET_GELU","task binding"});
 if(t.input.handle==t.output.handle&&(t.input.object_generation!=t.output.object_generation||(t.input.offset<t.output.offset+t.output.bytes&&t.output.offset<t.input.offset+t.input.bytes)))return fail({EINVAL,"HUNET_GELU","overlap"});
 if(stopped())return fail({ECANCELED,"HUNET_GELU","cancelled"});
 try{
  const bool fast=backend.fast_runtime();r.content_checks_omitted=fast;
  std::vector<float>input(t.elements),output(t.elements),readback(fast?0:t.elements);
  auto hash=[&](const std::vector<float>&v,Digest&d){Interval timer(r.hash_ns);vxl_hunet_hash(v.data(),v.size()*4,d.data());return timer.close();};
  auto copy=[&](BufferRef ref,auto bytes,uint64_t&duration){Interval timer(duration);auto result=backend.copy(ref,bytes);if(!timer.close())return Result<void>::failure({EIO,"HUNET_GELU","clock"});return result;};
  r.phase=HunetGeluPhase::DOWNLOAD;auto download=copy(t.input,MutableBytes{reinterpret_cast<uint8_t*>(input.data()),t.elements*4},r.download_ns);if(!download)return fail(*download.error);r.downloaded=true;
  if(!fast&&!hash(input,r.input_sha))return fail({EIO,"HUNET_GELU","clock"});
  r.phase=HunetGeluPhase::COMPUTE;auto compute=[&](){Interval timer(r.cpu_call_ns);auto result=gelu_erf_pool_reserved(pool,input.data(),output.data(),t.elements,cancel,token);if(!timer.close())return Result<CpuTiming>::failure({EIO,"HUNET_GELU","clock"});return result;};auto cpu=compute();if(!cpu)return fail(*cpu.error);r.cpu=*cpu.value;r.computed=true;
  if(!fast&&!hash(output,r.output_sha))return fail({EIO,"HUNET_GELU","clock"});
  if(stopped())return fail({ECANCELED,"HUNET_GELU","cancelled"});
  r.phase=HunetGeluPhase::UPLOAD;auto upload=copy(t.output,Bytes{reinterpret_cast<const uint8_t*>(output.data()),t.elements*4},r.upload_ns);if(!upload)return fail(*upload.error);r.uploaded=true;
  if(!fast){
  r.phase=HunetGeluPhase::READBACK;auto verify=copy(t.output,MutableBytes{reinterpret_cast<uint8_t*>(readback.data()),t.elements*4},r.readback_ns);if(!verify)return fail(*verify.error);
  r.phase=HunetGeluPhase::VERIFY;if(!hash(readback,r.readback_sha))return fail({EIO,"HUNET_GELU","clock"});
  if(r.output_sha!=r.readback_sha)return fail({EBADMSG,"HUNET_GELU","readback hash"});
  r.verified=true;}
  if(stopped())return fail({ECANCELED,"HUNET_GELU","cancelled"});
  // Include release of all staging storage in the handoff interval. Empty
  // vector destructors after return do not defer any heap cleanup past end_ns.
  std::vector<float>().swap(input);
  std::vector<float>().swap(output);
  std::vector<float>().swap(readback);
  if(!finish.close())return fail({EIO,"HUNET_GELU","clock"});
  r.committed=true;r.phase=HunetGeluPhase::COMMITTED;return Result<void>::success();
 }catch(const std::bad_alloc&){return fail({ENOMEM,"HUNET_GELU","allocation"});}
 catch(const std::exception&e){return fail({EIO,"HUNET_GELU",e.what()});}
 catch(...){return fail({EIO,"HUNET_GELU","copy exception"});}
}
// A single nonempty range preserves the one sum/carry chain. The four shared
// workers still borrow the scheduler reservation; this is not reassociation.
static Result<CpuTiming> stats_ordered_pool(CpuPool&pool,const float*x,float*y,size_t n,uint32_t bits,std::shared_ptr<CpuCancel>cancel,uint64_t token){
 float eps;std::memcpy(&eps,&bits,4);
 if(!token||!n||n>393216||pool.workers()!=4||fegetround()!=FE_TONEAREST||!std::isfinite(eps)||eps<=0)return Result<CpuTiming>::failure({EINVAL,"GN_STATS_CPU","bounded input, positive epsilon, borrowed CPU4 and RNE required"});
 uint64_t begin=now();for(size_t i=0;i<n;i++)if(!std::isfinite(x[i]))return Result<CpuTiming>::failure({EDOM,"GN_STATS_CPU","nonfinite input"});uint64_t validated=now();float staging[2]{};
 RangeFn fn=[&](size_t first,size_t last){if(first==last)return Result<void>::success();if(first!=0||last!=1)return Result<void>::failure({EPROTO,"GN_STATS_CPU","one ordered range required"});const float mean=hunet_kahan_sum_ordered4(x,uint32_t(n))/float(n);const float variance=hunet_variance_sum_ordered4(x,uint32_t(n),mean);staging[0]=mean;staging[1]=1.f/sqrtf(variance/float(n)+eps);return Result<void>::success();};
 auto result=pool.run_ranges_reserved(1,fn,cancel,token);if(!result)return result;uint64_t checked=now();for(float v:staging)if(!std::isfinite(v))return Result<CpuTiming>::failure({EDOM,"GN_STATS_CPU","nonfinite statistics"});if(cancel&&cancel->cancelled.load())return Result<CpuTiming>::failure({ECANCELED,"GN_STATS_CPU","cancelled before statistics commit"});uint64_t copy_begin=now();std::memcpy(y,staging,8);uint64_t end=now();if(!begin||validated<begin||checked<validated||copy_begin<checked||end<copy_begin)return Result<CpuTiming>::failure({EIO,"GN_STATS_CPU","clock order"});CpuTiming timing=*result.value;timing.begin_ns=begin;timing.end_ns=end;timing.total_ns=end-begin;timing.validate_ns=validated-begin+copy_begin-checked;timing.copy_ns=end-copy_begin;return Result<CpuTiming>::success(timing);
}
Result<void> execute_hunet_stats(const HunetStatsTask&t,IGpuBackend&backend,CpuPool&pool,std::shared_ptr<CpuCancel>cancel,uint64_t token,HunetStatsReceipt&r){
 if(r.begin_ns||r.end_ns||r.phase!=HunetGeluPhase::NONE)return Result<void>::failure({ESTALE,"HUNET_GN_STATS","fresh receipt"});
 r.begin_ns=now();r.phase=HunetGeluPhase::VALIDATE;Finish finish{r};
 auto fail=[&](Error error){r.error=error.code;return Result<void>::failure(std::move(error));};
 auto stopped=[&](){return cancel&&cancel->cancelled.load();};
 if(!r.begin_ns)return fail({EIO,"HUNET_GN_STATS","clock"});
 if(!t.node||!token||!t.elements||t.elements>393216||pool.workers()!=4||!valid(t.input,t.elements*4,1)||!valid(t.output,8,3)||t.input.device_generation!=t.output.device_generation)return fail({EINVAL,"HUNET_GN_STATS","task binding"});
 if(t.input.handle==t.output.handle&&(t.input.object_generation!=t.output.object_generation||(t.input.offset<t.output.offset+t.output.bytes&&t.output.offset<t.input.offset+t.input.bytes)))return fail({EINVAL,"HUNET_GN_STATS","overlap"});
 if(stopped())return fail({ECANCELED,"HUNET_GN_STATS","cancelled"});
 try{
  const bool fast=backend.fast_runtime();r.content_checks_omitted=fast;
  std::vector<float>input(t.elements),output(2),readback(fast?0:2);
  auto hash=[&](const std::vector<float>&v,Digest&d){Interval timer(r.hash_ns);vxl_hunet_hash(v.data(),v.size()*4,d.data());return timer.close();};
  auto copy=[&](BufferRef ref,auto bytes,uint64_t&duration){Interval timer(duration);auto result=backend.copy(ref,bytes);if(!timer.close())return Result<void>::failure({EIO,"HUNET_GN_STATS","clock"});return result;};
  r.phase=HunetGeluPhase::DOWNLOAD;auto download=copy(t.input,MutableBytes{reinterpret_cast<uint8_t*>(input.data()),t.elements*4},r.download_ns);if(!download)return fail(*download.error);r.downloaded=true;
  if(!fast&&!hash(input,r.input_sha))return fail({EIO,"HUNET_GN_STATS","clock"});
  r.phase=HunetGeluPhase::COMPUTE;auto compute=[&](){Interval timer(r.cpu_call_ns);auto result=stats_ordered_pool(pool,input.data(),output.data(),t.elements,t.epsilon_bits,cancel,token);if(!timer.close())return Result<CpuTiming>::failure({EIO,"HUNET_GN_STATS","clock"});return result;};auto cpu=compute();if(!cpu)return fail(*cpu.error);r.cpu=*cpu.value;r.computed=true;std::memcpy(r.value_bits.data(),output.data(),8);
  if(!fast&&!hash(output,r.output_sha))return fail({EIO,"HUNET_GN_STATS","clock"});
  if(stopped())return fail({ECANCELED,"HUNET_GN_STATS","cancelled"});
  r.phase=HunetGeluPhase::UPLOAD;auto upload=copy(t.output,Bytes{reinterpret_cast<const uint8_t*>(output.data()),8},r.upload_ns);if(!upload)return fail(*upload.error);r.uploaded=true;
  if(!fast){
  r.phase=HunetGeluPhase::READBACK;auto verify=copy(t.output,MutableBytes{reinterpret_cast<uint8_t*>(readback.data()),8},r.readback_ns);if(!verify)return fail(*verify.error);
  r.phase=HunetGeluPhase::VERIFY;if(!hash(readback,r.readback_sha))return fail({EIO,"HUNET_GN_STATS","clock"});
  if(r.output_sha!=r.readback_sha)return fail({EBADMSG,"HUNET_GN_STATS","readback hash"});
  r.verified=true;}
  if(stopped())return fail({ECANCELED,"HUNET_GN_STATS","cancelled"});
  // Include release of all staging storage in the handoff interval. Empty
  // vector destructors after return do not defer any heap cleanup past end_ns.
  std::vector<float>().swap(input);
  std::vector<float>().swap(output);
  std::vector<float>().swap(readback);
  if(!finish.close())return fail({EIO,"HUNET_GN_STATS","clock"});
  r.committed=true;r.phase=HunetGeluPhase::COMMITTED;return Result<void>::success();
 }catch(const std::bad_alloc&){return fail({ENOMEM,"HUNET_GN_STATS","allocation"});}
 catch(const std::exception&e){return fail({EIO,"HUNET_GN_STATS",e.what()});}
 catch(...){return fail({EIO,"HUNET_GN_STATS","copy exception"});}
}
}

#include <vxnn/ops_hunet.hpp>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cfenv>
#include <array>
#include <limits>
using namespace vxnn;
struct Backend final:IGpuBackend{
 std::vector<float> input;std::array<float,2> output{1234,5678};bool fast=false,corrupt=false;int downloads=0,uploads=0,fail=0,forbidden=0;std::shared_ptr<CpuCancel>cancel_upload;
 explicit Backend(size_t n):input(n){for(size_t i=0;i<n;i++)input[i]=float(int(i%137)-68)/19.f;}
 bool fast_runtime()const override{return fast;}
 Result<DeviceCaps>capabilities()override{++forbidden;return Result<DeviceCaps>::failure({ENOSYS});}
 Result<BufferRef>allocate(uint64_t,AddressDomain,uint32_t)override{++forbidden;return Result<BufferRef>::failure({ENOSYS});}
 Result<void>copy(BufferRef r,Bytes b)override{++uploads;assert(r.handle==2&&r.bytes==8&&b.size==8&&r.access==3);if(fail==2)return Result<void>::failure({EIO});std::memcpy(output.data(),b.data,8);if(corrupt)output[0]+=1;if(cancel_upload)cancel_upload->cancel();return Result<void>::success();}
 Result<void>copy(BufferRef r,MutableBytes b)override{++downloads;if((fail==1&&r.handle==1)||(fail==3&&r.handle==2))return Result<void>::failure({EIO});if(r.handle==1){assert(b.size==input.size()*4&&r.access==1);std::memcpy(b.data,input.data(),b.size);}else{assert(r.handle==2&&b.size==8);std::memcpy(b.data,output.data(),8);}return Result<void>::success();}
 Result<void>copy(BufferRef,BufferRef)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<void>release_buffer(BufferRef)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<ProgramRef>program_load(const Digest&)override{++forbidden;return Result<ProgramRef>::failure({ENOSYS});}
 Result<void>release_program(ProgramRef)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<uint64_t>submit(const JobDesc&)override{++forbidden;return Result<uint64_t>::failure({ENOSYS});}
 Result<void>wait(uint64_t,uint64_t)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<Completion>read_completion(uint64_t)override{++forbidden;return Result<Completion>::failure({ENOSYS});}
 Result<void>ack_reap(uint64_t)override{++forbidden;return Result<void>::failure({ENOSYS});}
};
static HunetStatsTask task(size_t n){HunetStatsTask t;t.node=9;t.elements=n;t.epsilon_bits=925353388;t.input={1,11,0,n*4,1,8};t.output={2,12,0,8,3,8};return t;}
static std::array<float,2> reference(const std::vector<float>&x,uint32_t eb){auto add=[](float v,float&s,float&c){float y=v-c,t=s+y;c=(t-s)-y;s=t;};float sum=0,carry=0;for(float v:x)add(v,sum,carry);float mean=sum/float(x.size());sum=0;carry=0;for(float v:x){float d=v-mean;add(d*d,sum,carry);}float eps;std::memcpy(&eps,&eb,4);return {mean,1.f/sqrtf(sum/float(x.size())+eps)};}
int main(){auto budget=std::make_shared<vxunified::PendingBudget>();CpuPool pool(4,32,budget);assert(pool.ready());auto reserve=budget->reserve(4);assert(reserve);uint64_t token=*reserve.value;auto other=budget->reserve(28);assert(other);
 for(bool fast:{false,true})for(size_t n:{size_t(1),size_t(3),size_t(4),size_t(5),size_t(1280),size_t(49152),size_t(393216)})for(int pattern=0;pattern<2;pattern++){
  Backend b(n);b.fast=fast;if(pattern)for(size_t i=0;i<n;i++)b.input[i]=(i%4==0?1e10f:i%4==1?1.f:i%4==2?-1e10f:3.f);auto expected=reference(b.input,925353388);HunetStatsReceipt r;auto out=execute_hunet_stats(task(n),b,pool,{},token,r);assert(out&&r.committed&&r.computed&&r.uploaded&&!r.gpu_raw_cycles&&r.cpu.workers==4&&r.total_ns>=r.cpu.total_ns);assert(std::memcmp(expected.data(),b.output.data(),8)==0);assert(b.uploads==1&&b.downloads==(fast?1:2)&&!b.forbidden);assert(budget->used()==32);if(fast)assert(r.content_checks_omitted&&!r.verified&&!r.hash_ns&&!r.readback_ns&&r.input_sha==Digest{});else assert(r.verified&&r.output_sha==r.readback_sha);assert(!execute_hunet_stats(task(n),b,pool,{},token,r));
 }
 for(int mode=0;mode<13;mode++){
  Backend b(11);auto t=task(11);HunetStatsReceipt r;auto cancel=std::make_shared<CpuCancel>();uint64_t use=token;
  if(mode<3){b.fail=mode+1;}
  if(mode==3)b.corrupt=true;
  if(mode==4)cancel->cancel();
  if(mode==5)t.output.device_generation++;
  if(mode==6){t.output=t.input;t.output.access=3;}if(mode==7)b.input[0]=INFINITY;
  if(mode==8)use=0;
  if(mode==9)t.epsilon_bits=0;
  if(mode==10)b.cancel_upload=cancel;
  if(mode==11)t.elements=0;
  if(mode==12){t.elements=393217;t.input.bytes=t.elements*4;}
  auto out=execute_hunet_stats(t,b,pool,cancel,use,r);assert(!out&&!r.committed&&!r.gpu_raw_cycles&&!b.forbidden);assert(r.error==out.error->code);
  if(mode==3){assert(r.uploaded&&!r.verified&&out.error->code==EBADMSG);}
  assert(budget->used()==32);
 }
 assert(!fesetround(FE_DOWNWARD));{Backend b(11);HunetStatsReceipt r;assert(!execute_hunet_stats(task(11),b,pool,{},token,r)&&!r.committed);}assert(!fesetround(FE_TONEAREST));assert(budget->release(token)&&budget->release(*other.value)&&!budget->used());assert(pool.shutdown());puts("HUNET_CPU_STATS_ORDERED_TWO_PASS_KAHAN_EXACT_TAILS_BOUNDARIES_TRANSFER8B_TOKEN32_CANCEL_FAULT_STRICT_FAST_HOST_NOT_A53");}

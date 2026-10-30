#include "scheduler_fixture.hpp"
#include <vxnn/ops_hunet.hpp>
#include <cmath>
#include <cstring>
#include <future>
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace vxnn;
namespace {
std::atomic<void*> watched_storage[3];std::atomic<uint64_t> last_storage_release{0};
bool watched(void*p){bool found=false;for(auto&slot:watched_storage){void*expected=p;if(p&&slot.compare_exchange_strong(expected,nullptr))found=true;}return found;}
void released(bool found){if(found){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);last_storage_release.store(uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec);}}
}
// Preserve the real allocator/ASan family; observe both Itanium delete forms.
extern "C" void __real__ZdlPv(void*);
extern "C" void __real__ZdlPvm(void*,size_t);
extern "C" void __wrap__ZdlPv(void*p){bool found=watched(p);__real__ZdlPv(p);released(found);}
extern "C" void __wrap__ZdlPvm(void*p,size_t n){bool found=watched(p);__real__ZdlPvm(p,n);released(found);}
namespace {
struct Backend:IGpuBackend {
 std::vector<float>input,output;int downloads=0,uploads=0;bool fail_download=false,fail_upload=false,fail_readback=false,throw_download=false,throw_upload=false,corrupt=false,block=false,entered=false,resume=false;
 bool watch_storage=false;std::mutex mutex;std::condition_variable cv;
 explicit Backend(size_t n=11):input(n),output(n,1234){for(size_t i=0;i<n;i++)input[i]=float(int(i%131)-65)/17;}
 Result<DeviceCaps>capabilities()override{return Result<DeviceCaps>::failure({ENOSYS});}
 Result<BufferRef>allocate(uint64_t,AddressDomain,uint32_t)override{return Result<BufferRef>::failure({ENOSYS});}
 Result<void>copy(BufferRef ref,Bytes b)override{
  ++uploads;if(throw_upload)throw std::runtime_error("original upload exception");if(fail_upload)return Result<void>::failure({EIO,"TEST","upload failure"});
  if(ref.handle!=2||ref.object_generation!=12||ref.device_generation!=8||b.size!=output.size()*4)return Result<void>::failure({ESTALE});
  if(watch_storage)watched_storage[1].store(const_cast<uint8_t*>(b.data));
  {std::unique_lock<std::mutex>lock(mutex);entered=true;cv.notify_all();if(block)cv.wait(lock,[&]{return resume;});}
  std::memcpy(output.data(),b.data,b.size);if(corrupt)output[0]+=1;return Result<void>::success();
 }
 Result<void>copy(BufferRef ref,MutableBytes b)override{
  ++downloads;if(throw_download)throw std::runtime_error("original download exception");if(fail_download||(ref.handle==2&&fail_readback))return Result<void>::failure({EIO,"TEST","download failure"});
  if(ref.device_generation!=8||b.size!=input.size()*4)return Result<void>::failure({ESTALE});
  if(watch_storage)watched_storage[ref.handle==1?0:2].store(b.data);
  if(ref.handle==1&&ref.object_generation==11)std::memcpy(b.data,input.data(),b.size);
  else if(ref.handle==2&&ref.object_generation==12)std::memcpy(b.data,output.data(),b.size);
  else return Result<void>::failure({ESTALE});
  return Result<void>::success();
 }
 Result<void>copy(BufferRef,BufferRef)override{return Result<void>::failure({ENOSYS});}
 Result<void>release_buffer(BufferRef)override{return Result<void>::failure({ENOSYS});}
 Result<ProgramRef>program_load(const Digest&)override{return Result<ProgramRef>::failure({ENOSYS});}
 Result<void>release_program(ProgramRef)override{return Result<void>::failure({ENOSYS});}
 Result<uint64_t>submit(const JobDesc&)override{return Result<uint64_t>::failure({ENOSYS});}
 Result<void>wait(uint64_t,uint64_t)override{return Result<void>::failure({ENOSYS});}
 Result<Completion>read_completion(uint64_t)override{return Result<Completion>::failure({ENOSYS});}
 Result<void>ack_reap(uint64_t)override{return Result<void>::failure({ENOSYS});}
};
HunetGeluTask task(size_t n=11){HunetGeluTask t;t.node=296;t.elements=n;t.input={1,11,0,n*4,1,8};t.output={2,12,0,n*4,3,8};return t;}
struct Chain:Source {
 Chain():Source(3){}
 Result<WorkItem>get(uint64_t i)const override{auto result=Source::get(i);WorkItem w=*result.value;if(i==1){w.kind=WorkKind::CPU;w.operation=CpuTaskRef{"GeluErfF32.v1",296};}return Result<WorkItem>::success(std::move(w));}
};
void chain(){
 auto budget=std::make_shared<PendingBudget>();CpuPool pool(4,32,budget);Scheduler scheduler(budget);auto r=request("HUNET-prefix-gelu-successor",3);r.source=std::make_shared<Chain>();value(scheduler.submit(r),"chain submit");auto prefix=value(scheduler.next_ready(),"GPU predecessor");ok(scheduler.complete(outcome(prefix)),"predecessor ACK and retirement");auto cpu=value(scheduler.next_ready(),"CPU after retired predecessor");require(cpu.kind==WorkKind::CPU&&budget->used()==4,"shared scheduler reserves four CPU workers");
 auto other=value(budget->reserve(28),"global pending32 is full");Backend backend(393216);backend.block=true;backend.watch_storage=true;last_storage_release.store(0);HunetGeluReceipt receipt;std::optional<Error>error;
 std::thread worker([&]{auto result=execute_hunet_gelu(task(393216),backend,pool,cpu.cancel_control,cpu.pending_token,receipt);if(!result)error=*result.error;});
 {std::unique_lock<std::mutex>lock(backend.mutex);require(backend.cv.wait_for(lock,std::chrono::seconds(10),[&]{return backend.entered;}),"CPU reaches upload using borrowed reservation at pending32");}
 require(!scheduler.next_ready(),"dependent GPU cannot dispatch during blocked commit");auto incomplete=outcome(cpu);incomplete.retired=false;require(!scheduler.complete(incomplete),"CPU without retirement cannot unblock graph");
 {std::lock_guard<std::mutex>lock(backend.mutex);backend.resume=true;backend.cv.notify_all();}worker.join();require(!error&&receipt.committed&&receipt.verified,"CPU result committed only after upload/readback hash verification");
 for(auto&slot:watched_storage){require(!slot.load(),"all three staging arrays were released");}
 require(last_storage_release.load()&&receipt.end_ns>=last_storage_release.load(),"full handoff total includes staging memory release");
 require(receipt.input_sha!=Digest{}&&receipt.output_sha==receipt.readback_sha&&!receipt.gpu_raw_cycles,"checksums present and CPU has no GPU cycles");
 require(receipt.cpu.workers==4&&receipt.cpu.total_ns&&receipt.total_ns>=receipt.cpu.total_ns&&receipt.end_ns>=receipt.begin_ns,"nested CPU and total Linux timings recorded");
 for(size_t i=0;i<backend.input.size();i++){float x=backend.input[i],expected=(x*.5f)*(1.0f+erff(x/1.4142135623730951f));require(!std::memcmp(&expected,&backend.output[i],4),"real shared four-worker output equals independent original scalar expression");}
 require(budget->used()==32,"adapter does not release or add caller tokens");ok(budget->release(other),"retire unrelated reservation");ok(scheduler.complete(outcome(cpu)),"publish CPU commit and retirement");auto successor=value(scheduler.next_ready(),"dependent GPU only after commit");require(successor.work_index==2,"correct successor");ok(scheduler.complete(outcome(successor)),"GPU successor fixture retirement");require(budget->used()==0,"all resources retired");
}
void failures(){
 auto budget=std::make_shared<PendingBudget>();CpuPool pool(4,32,budget);auto token=value(budget->reserve(4),"failure reservation");
 for(int mode=0;mode<10;mode++){
  Backend b;HunetGeluReceipt receipt;auto t=task();auto cancel=std::make_shared<CpuCancel>();
  if(mode==0){b.fail_download=true;}
  if(mode==1){b.fail_upload=true;}
  if(mode==2){b.corrupt=true;}
  if(mode==3){cancel->cancel();}
  if(mode==4){t.output.device_generation++;}
  if(mode==5){t.output=t.input;t.output.access=3;}if(mode==6)b.input[0]=INFINITY;
  if(mode==7){b.throw_download=true;}
  if(mode==8){b.throw_upload=true;}
  if(mode==9){b.fail_readback=true;}
  auto result=execute_hunet_gelu(t,b,pool,cancel,token,receipt);require(!result&&!receipt.committed&&!receipt.gpu_raw_cycles,"first error preserved without commit/cycles");
  require(receipt.error==result.error->code&&receipt.end_ns>=receipt.begin_ns,"partial failure receipt keeps original error and timing");
  if(mode==2)require(result.error->code==EBADMSG&&receipt.uploaded&&!receipt.verified&&receipt.output_sha!=receipt.readback_sha,"corrupt output remains failure evidence");
  if(mode==7)require(receipt.download_ns&&receipt.phase==HunetGeluPhase::DOWNLOAD,"throwing download still records time at original failing phase");
  if(mode==8)require(receipt.upload_ns&&receipt.phase==HunetGeluPhase::UPLOAD,"throwing upload still records time at original failing phase");
  if(mode==7||mode==8)require(result.error->detail.find("original ")!=std::string::npos,"original exception detail retained");
  if(mode==9)require(receipt.readback_ns&&receipt.uploaded&&!receipt.verified,"failed readback never commits uploaded data");
  if(mode==0||mode==3||mode==4||mode==5||mode==6)require(b.uploads==0,"no output publication on precommit failures");
  require(budget->used()==4,"no borrowed token leak/release on failure");
  int before=b.downloads+b.uploads;auto repeated=execute_hunet_gelu(t,b,pool,cancel,token,receipt);require(!repeated&&repeated.error->code==ESTALE&&b.downloads+b.uploads==before,"a used evidence receipt cannot trigger automatic retry");
 }
 ok(budget->release(token),"failure owner retires");
}
std::vector<float>read_floats(const std::string&path,size_t n){std::vector<float>v(n);std::ifstream f(path,std::ios::binary);require(bool(f),"captured source open");f.read(reinterpret_cast<char*>(v.data()),n*4);require(f.gcount()==std::streamsize(n*4)&&f.peek()==EOF,"captured source exact extent");return v;}
void captured(const std::string&source,const std::string&dest){
 const unsigned ids[]={8,17,28,37,49,55,61,67,73,79,87,93,98};const size_t sizes[]={393216,393216,196608,196608,98304,98304,98304,98304,98304,98304,49152,49152,1280};
 auto budget=std::make_shared<PendingBudget>();CpuPool pool(4,32,budget);auto token=value(budget->reserve(4),"captured borrowed CPU token");auto other=value(budget->reserve(28),"captured full global budget");
 for(size_t k=0;k<13;k++){
  char name[8];std::snprintf(name,sizeof(name),"%03u",ids[k]);auto n=sizes[k];Backend b(n);b.input=read_floats(source+"/data/"+name+"-input.f32",n);auto a53=read_floats(source+"/a53-erf-reference/"+name+"-output.f32",n);auto oldgpu=read_floats(source+"/data/"+name+"-gpu_reference.f32",n);HunetGeluReceipt receipt;ok(execute_hunet_gelu(task(n),b,pool,{},token,receipt),"captured source uses actual typed transfer/pool/commit path");
  size_t old_diff=0,gpu_diff=0;double old_max=0,gpu_max=0;
  for(size_t i=0;i<n;i++){float x=b.input[i],expected=(x*.5f)*(1.0f+erff(x/1.4142135623730951f));require(!std::memcmp(&expected,&b.output[i],4),"captured four-worker math bitwise host scalar");old_diff+=std::memcmp(&a53[i],&b.output[i],4)!=0;gpu_diff+=std::memcmp(&oldgpu[i],&b.output[i],4)!=0;old_max=std::max(old_max,std::fabs(double(a53[i])-b.output[i]));gpu_max=std::max(gpu_max,std::fabs(double(oldgpu[i])-b.output[i]));}
  require(receipt.committed&&receipt.output_sha==receipt.readback_sha&&budget->used()==32&&!receipt.gpu_raw_cycles,"captured commit/hash/global resource invariants");std::ofstream output(dest+"/"+name+"-output.f32",std::ios::binary);require(bool(output),"captured output open");output.write(reinterpret_cast<char*>(b.output.data()),n*4);require(bool(output),"captured output complete");
  std::printf("{\"scope\":\"HOST_COPY_PROTOCOL_AND_REAL_CPU_MATH_NOT_GPU_OR_DMA\",\"stage\":%u,\"elements\":%zu,\"bit_diff_scalar\":0,\"bit_diff_old_a53\":%zu,\"bit_diff_gpu_reference\":%zu,\"max_abs_old_a53\":%.17g,\"max_abs_gpu_reference\":%.17g,\"total_ns\":%llu,\"download_ns\":%llu,\"cpu_call_ns\":%llu,\"cpu_compute_ns\":%llu,\"upload_ns\":%llu,\"readback_ns\":%llu,\"hash_ns\":%llu,\"gpu_raw_cycles\":null}\n",ids[k],n,old_diff,gpu_diff,old_max,gpu_max,(unsigned long long)receipt.total_ns,(unsigned long long)receipt.download_ns,(unsigned long long)receipt.cpu_call_ns,(unsigned long long)receipt.cpu.compute_ns,(unsigned long long)receipt.upload_ns,(unsigned long long)receipt.readback_ns,(unsigned long long)receipt.hash_ns);
 }
 ok(budget->release(other),"captured unrelated tokens retired");ok(budget->release(token),"captured owner retires CPU tokens");
}
}
int main(int argc,char**argv){chain();failures();if(argc==3)captured(argv[1],argv[2]);else require(argc==1,"captured replay arguments");puts("U12_HUNET_SHARED_POOL_COMMIT_DEPENDENCY_AND_FAILURE_PASS_HOST_PROTOCOL_NOT_GPU gpu_raw_cycles=null");return 0;}

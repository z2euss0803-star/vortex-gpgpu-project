#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include <atomic>
#include <thread>
#include <new>
#include <fcntl.h>
#include <unistd.h>
static thread_local int fail_after=-1;
void*operator new(std::size_t n){if(fail_after>=0&&fail_after--==0){fail_after=-1;throw std::bad_alloc();}auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
namespace {
constexpr uint64_t MiB=1ULL<<20,base=128*MiB,limit=1024*MiB;
BackendIdentity hardware(){BackendIdentity h;h.hardware_contract=digest(11);h.bit=digest(12);h.xsa=digest(13);h.compatible=digest(14);h.device_generation=1;h.provider_flags=1;return h;}
struct Port:ExecutionPort {
 unsigned runs=0,retired=0;
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(hardware());}
 Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override{++runs;return Result<void>::failure({EIO,"HOST_MEMORY_TEST","no compute permitted"});}
 Result<void>retire_request(int,RequestKey)override{++retired;return Result<void>::success();}
};
struct Fixture {
 char dir[64]="/tmp/vx-host-memory-XXXXXX";std::shared_ptr<DeviceOwner>owner;std::shared_ptr<Port>port=std::make_shared<Port>();std::shared_ptr<Runtime>rt;
 Fixture(){require(mkdtemp(dir),"private root");ServiceConfig c;c.private_dir=dir;owner=value(DeviceOwner::acquire(c,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner");rt=value(Runtime::start(owner,hardware(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"common Runtime");}
 ~Fixture(){rt.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);}
};
}
int main(int argc,char**argv){
 if(argc==2&&std::string(argv[1])=="metadata-budget"){
  Fixture f;auto pin=value(f.rt->reserve_host_memory(4096),"metadata backing quota");
  struct BudgetSource:Source {std::shared_ptr<HostMemoryLease>pin;mutable std::shared_ptr<ResidentLease>model;bool reject;BudgetSource(std::shared_ptr<HostMemoryLease>p,std::shared_ptr<ResidentLease>m,bool fail):Source(1,WorkKind::CPU),pin(std::move(p)),model(std::move(m)),reject(fail){}Result<WorkItem>get(uint64_t i)const override{require(pin->bytes()==4096,"metadata callback can inspect its already-admitted host lifetime");model.reset();if(reject)return Result<WorkItem>::failure({EBADMSG,"HOST_METADATA","original source refusal"});return Source::get(i);}};
  auto q=request("metadata-budget",1,WorkKind::CPU);q.resident_bytes=1;q.cpu_bytes=4096;q.source=std::make_shared<BudgetSource>(pin,value(f.rt->reserve_resident(q.model,1),"last model pin released inside source callback"),false);
  puts("U20_METADATA_BUDGET_CALLBACK_BEGIN");std::fflush(stdout);alarm(10);
  auto accepted=value(f.rt->submit(q),"admission never holds resident-budget mutex across trusted metadata callback");
  ok(f.rt->cancel(accepted),"cancel metadata-only probe before execution");ok(f.rt->reap_result(accepted),"retire original cancelled request");require(!f.rt->stats().resident_models&&f.rt->stats().cpu_bytes==base+4096,"successful provisional user retires last released model pin exactly");
  auto bad=request("metadata-refused",1,WorkKind::CPU);bad.resident_bytes=1;bad.cpu_bytes=4096;bad.source=std::make_shared<BudgetSource>(pin,value(f.rt->reserve_resident(bad.model,1),"last pin for refused metadata"),true);auto refused=f.rt->submit(bad);alarm(0);
  require(!refused&&refused.error->code==EBADMSG&&!f.rt->stats().resident_models&&!f.rt->stats().scheduler.indexed&&f.rt->stats().cpu_bytes==base+4096,"failed source rolls back provisional CPU/model/queue despite releasing original last pin");
  ok(f.rt->shutdown(),"metadata-only close");require(!f.port->runs,"no GPU or CPU execution in lock-order probe");
  puts("U20_METADATA_BUDGET_CALLBACK_NO_REENTRY_DEADLOCK_PASS_HOST_NOT_GPU gpu_raw_cycles=null");return 0;
 }
 Fixture f;std::shared_ptr<HostMemoryLease>host;{auto initial=f.rt->reserve_host_memory(7*MiB);require(bool(initial),"U20 common host memory must reserve from existing CPU1GiB budget before allocation");host=*initial.value;}require(host->bytes()==7*MiB&&f.rt->owns_host_memory(host)&&!f.rt->owns_host_memory({}),"exact private common host lifetime");
 auto a=value(f.rt->reserve_resident(digest(1),2*MiB),"NN model1"),b=value(f.rt->reserve_resident(digest(2),3*MiB),"NN model2");require(f.rt->stats().resident_models==2&&f.rt->stats().cpu_bytes==base+12*MiB,"host lease does not take an NN slot and shares totalCPU");require(!f.rt->reserve_resident(digest(3),1),"third NN still refused");
 require(!f.rt->reserve_host_memory(0)&&!f.rt->reserve_host_memory(limit+1),"zero/overflow host reservation refused");
 auto remaining=limit-f.rt->stats().cpu_bytes;auto full=value(f.rt->reserve_host_memory(remaining),"exact shared CPU upper bound");require(f.rt->stats().cpu_bytes==limit&&!f.rt->reserve_host_memory(1),"no per-app duplicate memory budget");auto r=request("no-cpu-room",1,WorkKind::CPU);r.model=digest(1);r.resident_bytes=2*MiB;r.input_bytes=1;r.cpu_bytes=1;auto deny=f.rt->submit(r);require(!deny&&deny.error->code==ENOSPC&&!f.port->runs,"host lifetime backs pressure into existing request admission");full.reset();require(f.rt->stats().cpu_bytes==base+12*MiB,"exact no-GPU host release");
 unsigned oom=0;for(int i=0;i<12;++i){auto before=f.rt->stats().cpu_bytes;std::optional<Result<std::shared_ptr<HostMemoryLease>>>attempt;bool escaped=false;fail_after=i;try{attempt.emplace(f.rt->reserve_host_memory(4096));}catch(const std::bad_alloc&){escaped=true;}fail_after=-1;require(!escaped&&attempt.has_value(),"allocation returns explicit failure without recursive destructor");if(!*attempt){++oom;require(attempt->error->code==ENOMEM,"original allocation reason");}attempt.reset();require(f.rt->stats().cpu_bytes==before,"allocation rollback exact");}require(oom>0,"actual host lease allocations exercised");
 std::vector<std::thread>threads;for(unsigned t=0;t<8;++t)threads.emplace_back([&]{for(unsigned i=0;i<100;++i){auto lease=value(f.rt->reserve_host_memory(4096),"concurrent exact budget");require(f.rt->owns_host_memory(lease)&&lease->bytes()==4096,"retained metadata");}});for(auto&t:threads)t.join();require(f.rt->stats().cpu_bytes==base+12*MiB,"800 concurrent reservations return exact bytes");
 std::vector<std::shared_ptr<HostMemoryLease>>holds;for(unsigned n=0;n<63;++n)holds.push_back(value(f.rt->reserve_host_memory(1),"bounded host lifetime metadata"));auto exhausted=f.rt->reserve_host_memory(1);require(!exhausted&&exhausted.error->code==ENOSPC,"host lifetime table bounded64 independently of byte size");holds.clear();
 a.reset();b.reset();require(f.rt->stats().resident_models==0&&f.rt->stats().cpu_bytes==base+7*MiB,"host hold outlives NN model pins");ok(f.rt->shutdown(),"host buffers do not impersonate GPU consumers");require(!f.rt->reserve_host_memory(1),"closing rejects new host memory");host.reset();require(f.rt->stats().cpu_bytes==base&&!f.port->runs,"last host lifetime release after shutdown, zero START");std::printf("U20_COMMON_HOST_LIFETIME_CPU1GIB_NN2_BOUND64_OOM_CONCURRENCY_PASS_HOST_NOT_GPU oom=%u gpu_raw_cycles=null\n",oom);
}

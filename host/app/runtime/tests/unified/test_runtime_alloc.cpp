#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include <atomic>
#include <fcntl.h>
#include <new>
#include <unistd.h>
static std::atomic<int> fail_after{-1};
void*operator new(std::size_t n){int remaining=fail_after.load();if(remaining>=0){if(fail_after.fetch_sub(1)==0){fail_after=-1;throw std::bad_alloc();}}auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
namespace {
BackendIdentity identity(){BackendIdentity i;i.hardware_contract=digest(11);i.bit=digest(12);i.xsa=digest(13);i.compatible=digest(14);i.provider_flags=1;i.device_generation=1;return i;}
struct Port:ExecutionPort{
 int executions=0;
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&w,vxnn::CpuPool*,ExecutionReceipt&r)override{fail_after=-1;++executions;r.outcome=outcome(w);return Result<void>::success();}
 Result<void>retire_request(int,RequestKey)override{return Result<void>::success();}
};
}
int main(){
 int refused=0;
 {
  char dir[]="/tmp/vx-resident-alloc-XXXXXX";require(mkdtemp(dir),"resident allocation fixture");ServiceConfig c;c.private_dir=dir;
  auto owner=value(DeviceOwner::acquire(c,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"resident owner");auto port=std::make_shared<Port>();
  auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"resident runtime");
  int allocation_refusals=0;
  for(int fail=0;fail<20;++fail){
   std::optional<Result<std::shared_ptr<ResidentLease>>>attempt;fail_after=fail;bool escaped=false;
   try{attempt.emplace(rt->reserve_resident(digest(1),132ULL<<20));}catch(const std::bad_alloc&){escaped=true;}fail_after=-1;
   require(!escaped&&attempt.has_value(),"resident allocation must not escape or lock rollback");
   if(!*attempt){++allocation_refusals;require(attempt->error->code==ENOMEM,"actual allocation refusal reason");}
   attempt.reset();auto stats=rt->stats();require(stats.resident_models==0&&stats.resident_leases==0&&stats.cpu_bytes==(128ULL<<20),"resident allocation failure or last release rolls back exact global budget");
  }
  require(allocation_refusals>0,"resident allocation injection exercised");ok(rt->shutdown(),"resident allocation shutdown");rt.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);
  std::printf("U20_RESIDENT_TRANSACTIONAL_20_ALLOCATION_POINTS_PASS_NOT_BOARD refused=%d\n",allocation_refusals);
 }
 for(int fail=0;fail<48;fail++){
  char dir[]="/tmp/vx-runtime-alloc-XXXXXX";require(mkdtemp(dir),"private allocation fixture");ServiceConfig c;c.private_dir=dir;
  auto owner=value(DeviceOwner::acquire(c,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner");auto port=std::make_shared<Port>();
  auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"runtime");
  auto req=request(std::string(128,'r').c_str(),2,WorkKind::TRANSFER);req.request.boot_id=std::string(128,'b');req.request.client_id=std::string(128,'c');req.cpu_bytes=4096;value(rt->submit(req),"admit before allocation injection");
  fail_after=fail;bool escaped=false;std::optional<Result<void>> first;try{first.emplace(rt->run_one());}catch(const std::bad_alloc&){escaped=true;}fail_after=-1;
  require(!escaped,"dispatch allocation must not escape");
  if(!port->executions){++refused;auto s=rt->stats();if(s.scheduler.active_gpu||s.scheduler.active_cpu)std::fprintf(stderr,"allocation fail_index=%d\n",fail);require(!s.scheduler.active_gpu&&!s.scheduler.active_cpu&&!s.scheduler.reserved_completions,"unexecuted dispatch must not retain an active slot");require(s.phase==RuntimePhase::READY,"pre-execution allocation refusal remains retryable");ok(rt->run_one(),"retry unexecuted dispatch exactly once");}
  require(port->executions==1,"one execution after safe handoff");require(value(rt->status(req.request),"request state")==RequestStatus::QUEUED,"first of two work items completed");ok(rt->shutdown(),"cancel undispatched remainder");rt.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);
 }
 require(refused>0,"allocation injection actually exercised");std::printf("U10_RUNTIME_TRANSACTIONAL_DISPATCH_48_ALLOCATION_POINTS_PASS_NOT_BOARD refused=%d\n",refused);return 0;
}

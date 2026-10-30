#include <vxunified/control.hpp>
#include "scheduler_fixture.hpp"
#include <atomic>
#include <fcntl.h>
#include <new>
#include <unistd.h>
static std::atomic<int> fail_after{-1};
void*operator new(std::size_t n){if(fail_after.load()>=0&&fail_after.fetch_sub(1)==0){fail_after=-1;throw std::bad_alloc();}auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
namespace {
BackendIdentity identity(){BackendIdentity i;i.hardware_contract=digest(11);i.bit=digest(12);i.xsa=digest(13);i.compatible=digest(14);i.provider_flags=1;i.device_generation=1;return i;}
struct Port:ExecutionPort{
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override{std::abort();}
 Result<void>retire_request(int,RequestKey)override{return Result<void>::success();}
};
}
int main(){
 unsigned refusals=0,admitted_before_reply_failure=0;
 for(bool rejecting:{false,true})for(int fail=0;fail<128;fail++){
  char dir[]="/tmp/vx-control-alloc-XXXXXX";require(mkdtemp(dir),"allocation private directory");ServiceConfig cfg;cfg.private_dir=dir;
  auto owner=value(DeviceOwner::acquire(cfg,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner");
  auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,std::make_shared<Port>(),[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"runtime");
  int preparations=0;TrustedApplication entry;entry.id="nn-entry";entry.hardware=identity().hardware_contract;entry.model=digest(1);entry.plan=digest(2);entry.inputs={{"input",digest(3),4}};
  entry.prepare=[&](const RpcRequest&q)->Result<WorkRequest>{++preparations;if(q.parameters.find("reject")!=std::string::npos)return Result<WorkRequest>::failure({EINVAL,std::string(160,'c'),std::string(160,'e')});auto r=request("unused",1,WorkKind::TRANSFER);r.request=q.request;r.cpu_bytes=4096;return Result<WorkRequest>::success(r);};
  auto ctl=value(ServiceControl::start(rt,identity(),"boot",{entry}),"control");RpcRequest q;q.command="submit";q.request={"boot",std::string(128,'c'),std::string(128,'r')};q.entry_id=entry.id;q.input_id="input";q.parameters=rejecting?"{\"reject\":true}":"{}";
  bool escaped=false,first_ok=false;fail_after=fail;
  try{auto first=ctl->handle(q);first_ok=bool(first);}catch(...){escaped=true;}fail_after=-1;
  require(!escaped,"one-shot allocation must not escape control API");if(!first_ok)++refusals;
  auto admitted=rt->status(q.request);if(admitted){require(!rejecting&&preparations==1,"only validated factory admitted");if(!first_ok)++admitted_before_reply_failure;}
  int previous=preparations;q.parameters="{}";value(ctl->handle(q),"query/retry admission without duplicate factory side effects");require(preparations==previous+(admitted?0:1),"admitted replay does not call factory again");
  ok(rt->cancel(q.request),"cancel no execution");ok(rt->shutdown(),"shutdown no execution");ctl.reset();rt.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);
 }
 require(refusals&&admitted_before_reply_failure,"allocation injection includes error copy and ambiguous response after admission");std::printf("U10_CONTROL_256_ALLOCATION_CASES_PASS_NOT_BOARD refusals=%u admitted_before_reply_failure=%u\n",refusals,admitted_before_reply_failure);
}

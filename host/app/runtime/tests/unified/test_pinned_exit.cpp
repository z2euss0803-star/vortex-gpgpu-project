// Each mode runs in a separate process: retained device state deliberately
// lives until process exit, and cannot be released by a later fixture.
#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include <atomic>
#include <exception>
#include <fcntl.h>
#include <new>
#include <unistd.h>
static std::atomic<bool> refuse{false};
void*operator new(std::size_t n){if(refuse.load())throw std::bad_alloc();auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
namespace {
BackendIdentity identity(){BackendIdentity i;i.hardware_contract=digest(11);i.bit=digest(12);i.xsa=digest(13);i.compatible=digest(14);i.provider_flags=1;i.device_generation=1;return i;}
struct Port:ExecutionPort{
 int calls=0,retire_calls=0;
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override{++calls;return Result<void>::failure({ETIMEDOUT,"PIN_TEST","unknown physical retirement"});}
 Result<void>retire_request(int,RequestKey)override{++retire_calls;return Result<void>::success();}
};
}
int main(int argc,char**argv){
 require(argc==2,"exit test mode");std::set_terminate([](){const char text[]="U10_PIN_DESTRUCTOR_TERMINATED_UNDER_ENOMEM\n";ssize_t written=write(2,text,sizeof(text)-1);(void)written;_exit(88);});
 std::string mode=argv[1];
 if(mode=="result"||mode=="result-empty"){
  auto backing=std::make_shared<int>(7);std::weak_ptr<int> weak=backing;
  auto store=std::make_unique<ResultStore>([](const PublishedResult&){return Result<void>::success();},[backing](const ResultLease&){return Result<void>::success();});
  if(mode=="result"){
   PublishedResult p;p.execution_complete=true;p.lease.producer={"boot","client","result"};p.lease.hardware=digest(1);p.lease.model=digest(2);p.lease.plan=digest(3);ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.bytes=4;a.shape={1};a.sha=digest(4);p.lease.artifacts={a};ok(store->publish(p),"publish live backing");
  }
  backing.reset();refuse=true;store.reset();refuse=false;
  require(weak.expired()==(mode=="result-empty"),"only unresolved result lifetime retained");
 }else if(mode=="scheduler"||mode=="scheduler-empty"){
  auto source=std::make_shared<Source>(1,WorkKind::GPU);std::weak_ptr<Source> weak=source;auto scheduler=std::make_unique<Scheduler>(std::make_shared<PendingBudget>());
  if(mode=="scheduler"){auto r=request("active",1);r.source=source;value(scheduler->submit(r),"admit");value(scheduler->next_ready(),"active before destruction");}
  source.reset();refuse=true;scheduler.reset();refuse=false;
  require(weak.expired()==(mode=="scheduler-empty"),"unknown active source remains retained");
 }else{
  require(mode=="runtime"||mode=="runtime-empty","known exit mode");char dir[]="/tmp/vx-pinned-exit-XXXXXX";require(mkdtemp(dir),"private directory");ServiceConfig config;config.private_dir=dir;
  int closed=0;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));},[&closed](int fd){++closed;close(fd);}),"owner");std::weak_ptr<DeviceOwner> weak=owner;auto port=std::make_shared<Port>();
  auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"runtime");
  if(mode=="runtime"){auto r=request("fault",1);r.cpu_bytes=4096;value(rt->submit(r),"admit");auto failed=rt->run_one();require(!failed&&failed.error->code==ETIMEDOUT,"original timeout");}
  owner.reset();refuse=true;rt.reset();refuse=false;
  require(weak.expired()==(mode=="runtime-empty"),"only uncertain owner retained");require(closed==(mode=="runtime-empty"?1:0),"no premature device close");require(port->retire_calls==0,"no guessed retirement on destruction");
  auto second=DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));});require(bool(second)==(mode=="runtime-empty"),"exclusive lock retained for uncertain device");unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);
 }
 std::printf("U10_ALLOCATION_FREE_PINNED_EXIT_PASS_NOT_BOARD mode=%s\n",mode.c_str());return 0;
}

#include <vxunified/control.hpp>
#include "scheduler_fixture.hpp"
#include <fcntl.h>
#include <unistd.h>
namespace {
BackendIdentity identity(){BackendIdentity h;h.hardware_contract=digest(11);h.bit=digest(12);h.xsa=digest(13);h.compatible=digest(14);h.provider_flags=1;h.device_generation=1;return h;}
struct Port:ExecutionPort{
 unsigned calls=0,retires=0;Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&w,vxnn::CpuPool*,ExecutionReceipt&r)override{++calls;r.outcome=outcome(w);PublishedResult p;p.execution_complete=true;p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.producer=w.request;p.lease.app=w.app;p.lease.hardware=identity().hardware_contract;p.lease.model=digest(1);p.lease.plan=digest(2);ResultArtifact a;a.path="prepared.bin";a.layout="NCHW";a.bytes=4;a.shape={1};a.sha=digest(4);p.lease.artifacts={a};r.output=p;return Result<void>::success();}
 Result<void>retire_request(int,RequestKey)override{++retires;return Result<void>::success();}
};
}
int main(){
 char dir[]="/tmp/vx-preparation-XXXXXX";require(mkdtemp(dir),"own preparation owner");ServiceConfig cfg;cfg.private_dir=dir;auto owner=value(DeviceOwner::acquire(cfg,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one owner");auto port=std::make_shared<Port>();auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"common Runtime");
 std::vector<std::shared_ptr<PreparationLease>>holds;
 for(unsigned i=0;i<8;++i)holds.push_back(value(rt->reserve_preparation({"boot","client","p"+std::to_string(i)}),"U20 native preparation must reserve the same global queue8"));
 require(rt->stats().preparing_requests==8&&!rt->stats().scheduler.requests&&!port->calls,"preparation reservations do not submit or duplicate work tokens");require(!rt->reserve_preparation({"boot","client","ninth"}),"ninth metadata request refuses");auto r=request("direct",1,WorkKind::TRANSFER);r.cpu_bytes=4096;require(!rt->submit(r)&&!port->calls,"direct ingress shares all eight reserved slots");
 auto wrong=request("other-key",1,WorkKind::TRANSFER);wrong.cpu_bytes=4096;require(!rt->submit(wrong,holds[0])&&rt->stats().preparing_requests==8,"slot cannot substitute another request");r.request.request_id="p0";value(rt->submit(r,holds[0]),"convert exact metadata slot into actual source");require(rt->stats().preparing_requests==7&&rt->stats().scheduler.requests==1,"conversion never creates a second queue");require(!rt->submit(r,holds[0]),"spent preparation slot cannot be reused");require(!rt->reserve_preparation({"boot","client","still-ninth"}),"prepared plus reserved remains eight");
 auto spent=holds[0];holds.clear();require(!rt->stats().preparing_requests,"cancelled metadata releases only its own slot");ok(rt->run_one(),"execute admitted software item");ok(rt->reap_result(r.request),"exact backing retires");require(port->calls==1&&port->retires==1,"metadata itself never executes");ok(rt->shutdown(),"drain metadata owner");
 auto rt2=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"next Runtime after pool join");auto foreign=request("foreign",1,WorkKind::TRANSFER);foreign.cpu_bytes=4096;require(!rt2->submit(foreign,spent)&&!rt2->stats().scheduler.requests,"foreign/spent root never supplies an admission token");require(!rt2->reserve_preparation({"","client","bad"}),"invalid preparation request refuses");ok(rt2->shutdown(),"next Runtime closes without resources");puts("U20_PREPARATION_SHARED_QUEUE8_CONVERSION_CANCEL_AND_OWNER_PASS_HOST_NOT_GPU gpu_raw_cycles=null");
}

#include <vxunified/control.hpp>
#include "scheduler_fixture.hpp"
#include "nnrt/package/json.hpp"
#include <fcntl.h>
#include <unistd.h>
namespace {
BackendIdentity identity(){BackendIdentity h;h.hardware_contract=digest(11);h.bit=digest(12);h.xsa=digest(13);h.compatible=digest(14);h.provider_flags=1;h.device_generation=1;return h;}
struct Port:ExecutionPort {
 Digest model=digest(1);unsigned starts=0,retires=0;
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&w,vxnn::CpuPool*,ExecutionReceipt&r)override{++starts;r.outcome=outcome(w);PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=w.app;p.lease.hardware=identity().hardware_contract;p.lease.model=model;p.lease.plan=digest(2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ResultArtifact a;a.path="metadata";a.layout="HOST_ONLY";a.shape={1};a.bytes=4;a.sha=digest(9);p.lease.artifacts={a};r.output=p;return Result<void>::success();}
 Result<void>retire_request(int,RequestKey)override{++retires;return Result<void>::success();}
};
void diagnostic(const vxnn::Error&e,const char*dimension,uint64_t used,uint64_t limit){
 require(e.code==ENOSPC,"original quota errno remains ENOSPC");
 fprintf(stdout,"QUOTA_DETAIL %s\n",e.detail.c_str());
 require(e.detail.find(std::string("quota=")+dimension)!=std::string::npos,"actual exceeded quota dimension is actionable");
 require(e.detail.find("used="+std::to_string(used))!=std::string::npos&&e.detail.find("limit="+std::to_string(limit))!=std::string::npos,"observed usage and unchanged cap are reported");
 require(e.detail.size()<4096,"quota diagnostic remains bounded for RPC/journal");
}
}
int main(int argc,char**argv){
 require(argc==2,"diagnostic or evidence mode");std::string mode=argv[1];char path[]="/tmp/vx-r12-budget-XXXXXX";require(mkdtemp(path),"own private runtime root");ServiceConfig cfg;cfg.private_dir=path;auto owner=value(DeviceOwner::acquire(cfg,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one owner");auto port=std::make_shared<Port>();auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"actual Runtime");const auto baseline=rt->stats().cpu_bytes;
 if(mode=="nn"||mode=="persist"){
  auto first=value(rt->reserve_resident(digest(20),1024),"first model");auto second=value(rt->reserve_resident(digest(21),1024),"second model");
  if(mode=="persist"){
   TrustedApplication a;a.id="third";a.app=AppKind::NN;a.hardware=identity().hardware_contract;a.model=digest(22);a.plan=digest(2);a.inputs={{"fixed",digest(3),4}};
   a.prepare=[&](const RpcRequest&)->Result<WorkRequest>{auto third=rt->reserve_resident(digest(22),1024);require(!third&&third.error->code==ENOSPC,"actual pure quota refusal");return Result<WorkRequest>::failure(*third.error);};unsigned persisted=0;
   auto control=value(ServiceControl::start(rt,identity(),"boot",{a},{},{},true,[&](const PreparationReceipt&r){++persisted;require(r.error&&r.error->category=="RUNTIME_RESIDENT","original quota error passed to durable journal");return Result<void>::failure({ENOSPC,"EVIDENCE","preparation error log cannot persist"});}),"actual async controller");RpcRequest q;q.command="submit";q.request={"boot","test","quota-evidence-failure"};q.entry_id="third";q.input_id="fixed";q.parameters="{}";value(control->handle(q),"metadata accepted once");auto prepared=control->prepare_one();require(!prepared&&prepared.error->category=="EVIDENCE","evidence failure is fatal even with an original recoverable quota");require(persisted==1&&!rt->stats().preparing_requests&&!port->starts,"failed journal releases metadata, never executes or replays");q.command="status";auto status=value(control->handle(q),"original failure query");require(status.find("RUNTIME_RESIDENT")!=std::string::npos&&status.find("FAILED")!=std::string::npos,"original request keeps its original quota error");
  }else{
   auto a=request("holding-first",1,WorkKind::TRANSFER);a.model=digest(20);a.cpu_bytes=4096;a.resident_bytes=1024;value(rt->submit(a),"real associated first request");auto b=a;b.request.request_id="holding-second";b.model=digest(21);value(rt->submit(b),"real associated second request");auto third=rt->reserve_resident(digest(22),1024);require(!third&&rt->stats().resident_nn_models==2,"third model refusal preserves NN2");diagnostic(*third.error,"resident_nn",2,2);require(third.error->detail.find("holding-first")!=std::string::npos&&third.error->detail.find("holding-second")!=std::string::npos,"bounded holding request identities locate retained sources");require(third.error->detail.find("holder=client/holding-first:model=14")!=std::string::npos&&third.error->detail.find("holder=client/holding-second:model=15")!=std::string::npos,"each sampled holder must identify its own model");ok(rt->cancel(a.request),"cancel first unexecuted metadata");ok(rt->reap_result(a.request),"reap first cancelled");ok(rt->cancel(b.request),"cancel second unexecuted metadata");ok(rt->reap_result(b.request),"reap second cancelled");
  }
  first.reset();second.reset();
 }else if(mode=="cpu"){
  auto host=value(rt->reserve_host_memory((1ULL<<30)-baseline),"fill exact remaining CPU bytes");auto refused=rt->reserve_resident(digest(20),1);require(!refused,"CPU cap enforced before allocation");diagnostic(*refused.error,"cpu_bytes",1ULL<<30,1ULL<<30);host.reset();
 }else if(mode=="resident-lease"){
  std::vector<std::shared_ptr<ResidentLease>>leases;for(unsigned i=0;i<64;i++)leases.push_back(value(rt->reserve_resident(digest(20),1024),"same-model lease within64"));auto refused=rt->reserve_resident(digest(20),1024);require(!refused&&rt->stats().resident_nn_models==1,"lease cap is separate from NN2");diagnostic(*refused.error,"resident_leases",64,64);leases.clear();
 }else if(mode=="host-lease"){
  std::vector<std::shared_ptr<HostMemoryLease>>leases;for(unsigned i=0;i<64;i++)leases.push_back(value(rt->reserve_host_memory(1),"host lease within64"));auto refused=rt->reserve_host_memory(1);require(!refused,"host lease cap");diagnostic(*refused.error,"host_leases",64,64);leases.clear();
 }else if(mode=="groups"){
  for(unsigned i=0;i<64;i++){auto r=request(("group-"+std::to_string(i)).c_str(),1,WorkKind::TRANSFER,AppKind::IMAGE);r.model=digest(30+i);r.cpu_bytes=4096;r.resident_bytes=1024;port->model=r.model;value(rt->submit(r),"image model within common group64");ok(rt->run_one(),"real software-only request");}
  auto refused=rt->reserve_resident(digest(200),1024,AppKind::IMAGE);require(!refused&&rt->stats().resident_models==64,"common group64 cap independent of NN2");diagnostic(*refused.error,"resident_groups",64,64);
  for(unsigned i=0;i<64;i++)ok(rt->reap_result({"boot","client","group-"+std::to_string(i)}),"retire each completed group");
 }else require(false,"known test mode");
 require(rt->stats().cpu_bytes==baseline&&!rt->stats().resident_models&&!rt->stats().preparing_requests,"refusals and cleanup leave exact original common budget");ok(rt->shutdown(),"normal shutdown without pending execution");printf("R12_RUNTIME_QUOTA_AND_ERROR_PERSISTENCE_HOST_ONLY_PASS mode=%s\n",mode.c_str());
}

#include <vxunified/journal.hpp>
#include "scheduler_fixture.hpp"
#include <json-c/json.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fstream>
namespace {
bool partial=false;int writes=0;
BackendIdentity identity(){BackendIdentity i;i.hardware_contract=digest(11);i.bit=digest(12);i.xsa=digest(13);i.compatible=digest(14);i.device_generation=1;i.provider_flags=1;return i;}
struct Port:ExecutionPort{
 int executes=0,retires=0;
 Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(identity());}
 Result<void>execute(int,const WorkItem&w,vxnn::CpuPool*pool,ExecutionReceipt&r)override{++executes;float x[11]={},y[11];auto run=vxnn::gelu_erf_pool_reserved(*pool,x,y,11,w.cancel_control,w.pending_token);if(!run)return Result<void>::failure(*run.error);for(float f:y)require(f==0,"zero independent GELU reference");r.outcome=outcome(w);PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.hardware=identity().hardware_contract;p.lease.model=digest(1);p.lease.plan=digest(2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.lease_tokens={1};ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.shape={11};a.bytes=44;a.sha=digest(7);p.lease.artifacts={a};r.output=p;return Result<void>::success();}
 Result<void>retire_request(int,RequestKey)override{++retires;return Result<void>::success();}
};
}
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){if(partial){if(writes++==0)return __real_write(fd,p,std::min(n,size_t(7)));errno=ENOSPC;return -1;}return __real_write(fd,p,n);}
int main(int argc,char**argv){
 require(argc==2,"journal integration directory");require(mkdir(argv[1],0700)==0,"fresh private integration directory");char dir[PATH_MAX];require(realpath(argv[1],dir),"absolute private directory");ServiceConfig config;config.private_dir=dir;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner");auto journal=value(EvidenceJournal::open(dir),"journal");auto port=std::make_shared<Port>();auto rt=value(Runtime::start(owner,identity(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[journal](const ExecutionReceipt&r,DeliveryStage s){return journal->save_work(r,s);},[journal](const PublishedResult&p){return journal->save_result(p);}),"real journal Runtime");auto req=request("journal",1,WorkKind::CPU);req.cpu_bytes=4096;req.resident_bytes=1024;value(rt->submit(req),"admit");partial=true;auto failed=rt->run_one();partial=false;require(!failed&&failed.error->code==ENOSPC&&port->executes==1&&!rt->take_result(req.request),"real failed write cannot publish or repeat execution");ok(rt->retry_delivery(),"journal resumes without execution");require(port->executes==1&&rt->take_result(req.request),"only one actual CPU execution");auto*d=opendir(dir);require(d,"opendir");bool original_io=false;int finals=0,partials=0;while(auto*e=readdir(d)){std::string n=e->d_name;if(n.find(".partial-")==0){++partials;continue;}if(n.size()<5||n.substr(n.size()-5)!=".json")continue;++finals;std::ifstream f(std::string(dir)+"/"+n);std::string raw(std::istreambuf_iterator<char>(f),{});auto*j=json_tokener_parse(raw.c_str());require(j,"durable JSON");json_object*error=nullptr;if(json_object_object_get_ex(j,"evidence_error",&error)&&error){json_object*code=nullptr;require(json_object_object_get_ex(error,"code",&code),"evidence error code");original_io|=json_object_get_int(code)==ENOSPC;}json_object_put(j);}closedir(d);require(finals==3&&partials>=1,"RAW/state/result plus first partial retained");require(original_io,"durable recovered events must retain original ENOSPC evidence");ok(rt->reap_result(req.request),"retire result");require(port->retires==1,"retire once");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_REAL_JOURNAL_PARTIAL_RECOVERY_ORIGINAL_ERROR_NO_REEXECUTION_PASS_NOT_BOARD");return 0;
}

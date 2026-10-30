// Actual daemon/thread/Unix-RPC/journal integration with CPU metadata nodes.
// No GPU program, arithmetic, DMA or display is executed by this test.
#include <vxunified/daemon.hpp>
#include "scheduler_fixture.hpp"
#include "nnrt/package/json.hpp"
#include <vxl/core.h>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
using namespace vxunified;
namespace {
BackendIdentity hardware(){
 BackendIdentity h;auto hex=[](const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[i*2])<<4)|n(s[i*2+1]);}return d;};
 h.hardware_contract=hex("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");h.bit=hex("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");h.xsa=hex("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");h.compatible=hex("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");h.provider_flags=4;h.device_generation=1;return h;
}
struct MetadataTransport:IoctlTransport {
 vxl_v2_caps caps{};std::atomic<unsigned>caps_calls{0},unexpected_gpu_calls{0};
 MetadataTransport(){DeviceCaps c;c.ps_base=VXL_PS_BASE;c.ps_bytes=VXL_PS_SIZE;c.pl_base=VXL_PL_BASE;c.pl_bytes=VXL_PL_SIZE;c.max_objects=64;c.max_pending=32;c.max_completions=64;c.max_refs_per_job=8;c.max_args_bytes=4096;c.max_programs=16;c.cycle_counter_bits=32;caps=query_caps(hardware(),c);}
 int call(unsigned long command,void*bytes)override{
  if(command!=VXL_V2_CAPS){++unexpected_gpu_calls;return -EOPNOTSUPP;}
  auto*r=static_cast<vxl_compute_v2_request*>(bytes);if(r->payload_bytes!=sizeof(caps)||!r->user_ptr)return -EINVAL;std::memcpy(reinterpret_cast<void*>(r->user_ptr),&caps,sizeof(caps));r->device_generation=1;++caps_calls;return 0;
 }
 uint64_t now_ns()override{return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
};
void exact(int fd,void*buffer,size_t bytes,bool sending){auto*p=static_cast<char*>(buffer);while(bytes){auto n=sending?send(fd,p,bytes,MSG_NOSIGNAL):recv(fd,p,bytes,0);require(n>0,"exact RPC frame");p+=n;bytes-=size_t(n);}}
std::string call(std::shared_ptr<Daemon>d,const std::string&socket,const std::string&raw){
 auto client=std::async(std::launch::async,[socket,raw](){int fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(fd>=0,"RPC client socket");timeval timeout{5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));sockaddr_un address{};address.sun_family=AF_UNIX;require(socket.size()<sizeof(address.sun_path),"socket path");std::memcpy(address.sun_path,socket.c_str(),socket.size()+1);require(!connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"RPC connect");auto n=htonl(uint32_t(raw.size()));exact(fd,&n,4,true);auto copy=raw;exact(fd,&copy[0],copy.size(),true);exact(fd,&n,4,false);auto size=ntohl(n);require(size&&size<=65536,"bounded RPC response");std::string reply(size,'\0');exact(fd,&reply[0],reply.size(),false);close(fd);return reply;});
 while(client.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){auto served=d->serve_one(25);if(!served&&served.error->code!=EAGAIN&&served.error->code!=ETIMEDOUT)ok(served,"serve production Unix RPC");}
 return client.get();
}
bool accepted(const std::string&raw){auto p=vxnn::json::parse(raw);return vxnn::json::boolean(vxnn::json::member(p.get(),"ok"));}
}

namespace {
struct HeldSource:Source {
 std::shared_ptr<ResidentLease>resident;
 HeldSource(std::shared_ptr<ResidentLease>r):Source(1,WorkKind::CPU),resident(std::move(r)){}
};
std::string wire(const std::string&command,const std::string&id,const std::string&entry="nn0"){
 return "{\"version\":1,\"command\":\""+command+"\",\"boot_id\":\"boot\",\"client_id\":\"test\",\"request_id\":\""+id+"\""+(command=="submit"?",\"entry_id\":\""+entry+"\",\"input_id\":\"fixed\",\"parameters\":{}":"")+"}";
}
}
int main(int argc,char**argv){
 require(argc==2,"quota, fatal, identity, or dp mode");std::string mode=argv[1];
 char path[]="/tmp/vx-r12-daemon-XXXXXX";require(mkdtemp(path),"private actual daemon root");std::string root=path;require(!mkdir((root+"/owner").c_str(),0700)&&!mkdir((root+"/events").c_str(),0700),"private owned subdirectories");
 DaemonConfig cfg;cfg.service.private_dir=root+"/owner";cfg.evidence_dir=root+"/events";cfg.boot_id="boot";cfg.identity=hardware();cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;cfg.config_sha=digest(90);
 auto transport=std::make_unique<MetadataTransport>();auto*io=transport.get();std::weak_ptr<Runtime>runtime;unsigned opens=0;std::atomic<unsigned>retires{0},prepares{0},executions{0};
 auto daemon=value(Daemon::start(cfg,[&](const StartupContext&c){runtime=c.runtime;ApplicationSet apps;
  for(unsigned index=0;index<5;index++){
   TrustedApplication e;e.id=index<3?"nn"+std::to_string(index):index==3?"mosaic":"p8";e.app=index==4?AppKind::P8:AppKind::NN;e.hardware=c.identity.hardware_contract;e.model=digest(10+index);e.plan=digest(2);e.inputs={{"fixed",digest(3),4}};
   e.prepare=[&,index,role=e.app](const RpcRequest&q)->Result<WorkRequest>{++prepares;
    if(q.request.request_id=="fatal")return Result<WorkRequest>::failure({ENOSPC,"EVIDENCE","not a safe admission quota"});
    if(q.request.request_id=="handoff-rejected"){auto r=request(q.request.request_id.c_str(),1,WorkKind::CPU,role);r.request=q.request;r.model=digest(10+index);r.plan=digest(2);r.cpu_bytes=4096;r.resident_bytes=1024;return Result<WorkRequest>::success(std::move(r));}
    auto rt=runtime.lock();auto held=rt->reserve_resident(digest(10+index),1024,role);if(!held)return Result<WorkRequest>::failure(*held.error);
    auto r=request(q.request.request_id.c_str(),1,WorkKind::CPU,role);r.request=q.request;r.model=digest(q.request.request_id=="identity"?99:10+index);r.plan=digest(2);r.cpu_bytes=4096;r.resident_bytes=1024;r.source=std::make_shared<HeldSource>(*held.value);return Result<WorkRequest>::success(std::move(r));
   };apps.entries.push_back(std::move(e));
  }
  apps.compute.non_gpu=[&](const WorkItem&w,UnifiedBackend&,vxnn::CpuPool*,ExecutionReceipt&r){++executions;r.outcome=outcome(w);PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=w.app;p.lease.hardware=hardware().hardware_contract;p.lease.model=digest(w.request.request_id=="first"?10:w.request.request_id=="second"?11:w.request.request_id=="mosaic"?13:w.request.request_id=="p8"?14:12);p.lease.plan=digest(2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ResultArtifact a;a.path="metadata.txt";a.layout="HOST_METADATA_ONLY";a.bytes=4;a.shape={4};a.sha=digest(7);p.lease.artifacts={a};r.output=p;return Result<void>::success();};
  apps.compute.finish_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<void>::failure({EPERM,"TEST","no GPU execution"});};apps.compute.retire_request=[&](RequestKey,UnifiedBackend&){++retires;return Result<void>::success();};return Result<ApplicationSet>::success(std::move(apps));
 },[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){return std::move(transport);}),"real daemon starts");
 auto rt=runtime.lock();const auto socket=cfg.service.private_dir+"/control.sock";
 auto send=[&](const std::string&command,const std::string&id,const std::string&entry="nn0"){return call(daemon,socket,wire(command,id,entry));};
 auto wait=[&](const std::string&id,const std::string&want){for(unsigned n=0;n<1000;n++){auto raw=send("status",id);require(accepted(raw),"original status stays queryable");auto tree=vxnn::json::parse(raw);auto s=vxnn::json::str(vxnn::json::member(vxnn::json::member(tree.get(),"result"),"status"));if(s==want)return raw;usleep(1000);}require(false,"bounded worker status");return std::string{};};
 auto run=[&](const std::string&id,const std::string&e){require(accepted(send("submit",id,e)),"new legal submit");wait(id,"COMPLETED");};
 if(mode=="fatal"||mode=="identity"){
  require(accepted(send("submit",mode)),"one failing preparation admitted");wait(mode,"FAILED");for(unsigned n=0;n<1000&&!daemon->first_error();n++)usleep(1000);
  require(bool(daemon->first_error()),"evidence or identity error remains service fatal");require(!accepted(send("submit","after-fatal")),"fatal error cannot reopen admission");require(executions==0&&!rt->stats().resident_models&&!rt->stats().preparing_requests,"failed prepare executes nothing and drops ordinary leases");ok(daemon->shutdown(),"safe fatal preparation shutdown");puts("R12_FATAL_PREPARATION_STILL_CLOSES_ADMISSION_HOST_ONLY_PASS");return 0;
 }
 run("first","nn0");auto front=value(rt->retain_result({"boot","test","first"},Consumer::DP),"actual old DP hold");
 if(mode=="dp"){
  require(accepted(send("reap","first")),"page menu releases first client hold");require(rt->stats().resident_nn_models==1&&!rt->take_result({"boot","test","first"}),"DP alone retains model and client delivery is closed");
  run("second","nn1");auto next=value(rt->retain_result({"boot","test","second"},Consumer::DP),"new DP hold before old front retirement");require(rt->stats().resident_nn_models==2,"old/new complete models remain bounded");require(!rt->release_result(front,false)&&rt->stats().resident_nn_models==2,"unproven DP retirement cannot free old front");ok(rt->release_result(front,true),"proven display replacement retires old front");require(rt->stats().resident_nn_models==1,"old model releases after last consumer only");require(accepted(send("reap","second","nn1")),"second durable client reap");
  run("third","nn2");auto third=value(rt->retain_result({"boot","test","third"},Consumer::DP),"third strategy DP");ok(rt->release_result(next,true),"second display replaced");require(accepted(send("reap","third","nn2")),"third durable client reap");ok(rt->release_result(third,true),"final display retirement");
 }else{
  run("second","nn1");require(rt->stats().resident_nn_models==2,"original two retained results reproduce NN2 pressure");const std::string rejected=mode=="handoff"?"handoff-rejected":"rejected";require(accepted(send("submit",rejected,"nn2")),"third queued once");auto failure=wait(rejected,"FAILED");
  require(accepted(send("reap","first")),"first client reap with DP hold");ok(rt->release_result(front,true),"old DP final retirement");require(accepted(send("reap","second","nn1")),"second result reap");
  require(accepted(send("submit","third","nn2")),"quota refusal must not poison next valid admission");wait("third","COMPLETED");require(!daemon->first_error(),"ordinary quota is local and first service error remains empty");require(failure.find(mode=="handoff"?"RUNTIME":"RUNTIME_RESIDENT")!=std::string::npos&&failure.find("preparation_error_code\":28")!=std::string::npos,"original ENOSPC and category remain queryable");require(accepted(send("reap","third","nn2")),"third completed result reap");
 }
 require(!rt->stats().resident_models&&!rt->stats().preparing_requests,"all terminal CPU/model leases released");run("mosaic","mosaic");require(accepted(send("reap","mosaic","mosaic")),"mosaic metadata result reap");
 auto mode_raw=call(daemon,socket,"{\"version\":1,\"command\":\"mode\",\"boot_id\":\"boot\",\"mode\":\"p8\"}");require(accepted(mode_raw),"P8 mode admission still works");
 bool switched=false;for(unsigned i=0;i<1000;i++){auto raw=call(daemon,socket,"{\"version\":1,\"command\":\"mode-status\",\"boot_id\":\"boot\"}");auto v=vxnn::json::parse(raw);auto*r=vxnn::json::member(v.get(),"result");if(vxnn::json::str(vxnn::json::member(r,"current"))=="p8"&&!vxnn::json::boolean(vxnn::json::member(r,"switching"))){switched=true;break;}usleep(1000);}require(switched,"mode handoff advances after quota/ref retirement");run("p8","p8");require(accepted(send("reap","p8","p8")),"P8 metadata result reap");
 require(opens==1&&!io->unexpected_gpu_calls&&!rt->stats().resident_models&&!rt->stats().preparing_requests&&!daemon->first_error(),"one owner; no GPU/reset or resident leak");ok(daemon->shutdown(),"normal production shutdown");printf("R12_REAL_DAEMON_RPC_QUOTA_ISOLATION_OR_DP_MODEL_LIFETIME_PASS_HOST_ONLY mode=%s prepares=%u executions=%u retired=%u\n",mode.c_str(),prepares.load(),executions.load(),retires.load());
}

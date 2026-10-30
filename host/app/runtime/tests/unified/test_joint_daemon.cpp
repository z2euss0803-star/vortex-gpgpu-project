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
std::string submit(const std::string&id){return "{\"version\":1,\"command\":\"submit\",\"boot_id\":\"boot\",\"client_id\":\"test\",\"request_id\":\""+id+"\",\"entry_id\":\"host-control\",\"input_id\":\"fixed\",\"parameters\":{}}";}
}
int main(){
 char path[]="/tmp/vx-joint-daemon-XXXXXX";require(mkdtemp(path),"private daemon test root");std::string root=path;require(!mkdir((root+"/owner").c_str(),0700)&&!mkdir((root+"/events").c_str(),0700),"private subdirectories");std::printf("JOINT_DAEMON_EVIDENCE_DIR=%s\n",root.c_str());std::fflush(stdout);
 DaemonConfig config;config.service.private_dir=root+"/owner";config.evidence_dir=root+"/events";config.boot_id="boot";config.identity=hardware();config.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;config.config_sha=digest(90);
 auto owned_transport=std::make_unique<MetadataTransport>();auto*transport=owned_transport.get();std::atomic<unsigned>opens{0},retired{0};std::mutex mutex;std::condition_variable condition;bool entered=false,resume=false;std::vector<std::string>order;
 ApplicationBuilder builder=[&](const StartupContext&c)->Result<ApplicationSet>{
  require(c.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&c.runtime.lock()&&c.compute_backend,"actual common daemon roots");ApplicationSet apps;
  TrustedApplication entry;entry.id="host-control";entry.app=AppKind::NN;entry.hardware=c.identity.hardware_contract;entry.model=digest(1);entry.plan=digest(2);entry.inputs={{"fixed",digest(3),4}};
  entry.prepare=[](const RpcRequest&q){auto r=request(q.request.request_id.c_str(),2,WorkKind::CPU);r.request=q.request;r.cpu_bytes=4096;r.resident_bytes=1024;return Result<WorkRequest>::success(std::move(r));};apps.entries.push_back(std::move(entry));
  apps.compute.non_gpu=[&](const WorkItem&w,UnifiedBackend&,vxnn::CpuPool*pool,ExecutionReceipt&r){
   require(pool&&pool->workers()==4,"shared common CPU pool");{std::unique_lock<std::mutex>lock(mutex);order.push_back(w.request.request_id);if(w.request.request_id=="A"&&w.work_index==0){entered=true;condition.notify_all();condition.wait(lock,[&](){return resume;});}}
   r.outcome=outcome(w);r.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
   if(w.work_index==1){PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=AppKind::NN;p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.hardware=hardware().hardware_contract;p.lease.model=digest(1);p.lease.plan=digest(2);ResultArtifact a;a.path="metadata.txt";a.layout="HOST_METADATA_ONLY";a.sha=digest(7);a.bytes=4;a.shape={4};p.lease.artifacts={a};p.raw_legacy_json="{\"scope\":\"HOST_CONTROL_PLUMBING_ONLY_NOT_NN_OR_GPU\"}";r.output=p;}return Result<void>::success();
  };
  apps.compute.finish_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<void>::failure({EPERM,"TEST","GPU execution forbidden in metadata test"});};
  apps.compute.retire_request=[&](RequestKey,UnifiedBackend&){++retired;return Result<void>::success();};return Result<ApplicationSet>::success(std::move(apps));
 };
 auto daemon=value(Daemon::start(config,builder,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int)->std::unique_ptr<IoctlTransport>{require(bool(owned_transport),"one common transport");return std::move(owned_transport);}),"actual joint daemon starts with real durable journal");const auto socket=config.service.private_dir+"/control.sock";
 require(accepted(call(daemon,socket,submit("A"))),"first async request accepted");{std::unique_lock<std::mutex>lock(mutex);require(condition.wait_for(lock,std::chrono::seconds(5),[&](){return entered;}),"first original request enters worker");}
 require(accepted(call(daemon,socket,submit("B"))),"second preclosure async request accepted");require(accepted(call(daemon,socket,"{\"version\":1,\"command\":\"mode\",\"boot_id\":\"boot\",\"mode\":\"p8\"}")),"real mode RPC closes original admission");require(!accepted(call(daemon,socket,submit("late"))),"late original-mode request rejected by control");
 {std::lock_guard<std::mutex>lock(mutex);resume=true;}condition.notify_all();bool done=false;
 for(unsigned i=0;i<500;i++){auto raw=call(daemon,socket,"{\"version\":1,\"command\":\"mode-status\",\"boot_id\":\"boot\"}");require(accepted(raw),"real mode status RPC");auto p=vxnn::json::parse(raw);auto*r=vxnn::json::member(p.get(),"result");if(vxnn::json::str(vxnn::json::member(r,"current"))=="p8"&&!vxnn::json::boolean(vxnn::json::member(r,"switching"))){require(vxnn::json::u64(vxnn::json::member(r,"completed_ns"))>=vxnn::json::u64(vxnn::json::member(r,"requested_ns")),"actual monotonic handoff clocks");done=true;break;}usleep(1000);}
 require(done,"pending async preparations and complete requests must drain without worker fault");{std::lock_guard<std::mutex>lock(mutex);require((order==std::vector<std::string>{"A","A","B","B"}),"production daemon preserves queue8 and whole requests");}
 require(opens==1&&transport->unexpected_gpu_calls==0,"same one owner, no GPU work or reset");require(!daemon->first_error(),"no original service error");
 for(const auto*id:{"A","B"})require(accepted(call(daemon,socket,std::string("{\"version\":1,\"command\":\"reap\",\"boot_id\":\"boot\",\"client_id\":\"test\",\"request_id\":\"")+id+"\"}")),"explicit terminal result reap");
 ok(daemon->shutdown(),"production shutdown");require(retired==2,"both actual adapter retirement hooks");puts("JOINT_REAL_DAEMON_RPC_JOURNAL_ASYNC_QUEUE_WHOLE_REQUEST_MODE_HANDOFF_PASS_HOST_PROTOCOL_ONLY");
}

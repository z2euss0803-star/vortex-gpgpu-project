#include <vxunified/installation.hpp>
#include <vxnn/ops_int16.hpp>
#include "nnrt/package/json.hpp"
#include "scheduler_fixture.hpp"
#include "s8_owner_bridge.h"
#include "tests/mosaic_s8/fake_s8.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>
#include <time.h>
#include <mutex>
#include <condition_variable>
namespace {
namespace j=vxnn::json;
uint64_t now(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"host monotonic");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
std::string read(const std::string&path){std::ifstream f(path,std::ios::binary);require(bool(f),"frozen file read");return {std::istreambuf_iterator<char>(f),{}};}
std::string absolute(const char*path){char value[4096];require(realpath(path,value),"canonical frozen package path");return value;}
void seal(const std::string&path,const std::string&bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());f.close();require(bool(f)&&!chmod(path.c_str(),0400),"private immutable installed bytes");}
void text(json_object*o,const char*k,const std::string&v){require(!json_object_object_add(o,k,json_object_new_string_len(v.data(),int(v.size()))),"metadata text");}
void number(json_object*o,const char*k,uint64_t n){require(!json_object_object_add(o,k,json_object_new_uint64(n)),"metadata integer");}
// Explicit HOST PE mathematical model, not a second execution backend. Actual
// C object handles/relocation/pinning/ACK still surround each original operation.
int integer_compute(const void*raw,const void*a,const void*b,void*out){
 const auto*j=static_cast<const vxnn_i16_job_v1*>(raw);auto rc=vxnn_i16_validate_job(j);if(rc)return rc;
 for(uint32_t core=0;core<4&&!rc;++core){
  if(j->op==VXNN_MATMUL_I16){fake_s8 pe;auto io=pe.ops();io.core=core;rc=vxnn_i16_matmul(static_cast<const int16_t*>(a),j->a_bytes/2,static_cast<const int16_t*>(b),j->b_bytes/2,j->m,j->n,j->k,static_cast<int64_t*>(out),j->out_bytes/8,&io);require(pe.begins==pe.releases,"original HOST PE tiles retired");}
  else if(j->op==VXNN_BIAS_I48)rc=vxnn_i48_bias(static_cast<const int64_t*>(a),j->a_bytes/8,static_cast<const int32_t*>(b),j->b_bytes/4,j->m,j->n,static_cast<int64_t*>(out),j->out_bytes/8,core);
  else rc=vxnn_i48_relu(static_cast<const int64_t*>(a),j->a_bytes/8,static_cast<int64_t*>(out),j->out_bytes/8,core);
 }
 return rc;
}
struct Transport:IoctlTransport {
 void*core;unsigned loads=0;
 Transport(const char*c,const char*p,Digest compatible):core(s8_owner_bridge_create(c,p,compatible.data())){s8_owner_bridge_integer_compute(core,integer_compute);}
 ~Transport(){s8_owner_bridge_destroy(core);}
 int call(unsigned long command,void*p)override{auto result=s8_owner_bridge_call(core,command,p);if(!result&&command==VXL_V2_PROGRAM_LOAD)++loads;return result;}
 uint64_t now_ns()override{return now();}
};
j::Ptr rpc(const std::shared_ptr<Daemon>&daemon,const std::string&root,const std::string&request){
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(fd>=0,"private actual common RPC peer");timeval timeout{5,0};require(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),"bounded client read");sockaddr_un address{};address.sun_family=AF_UNIX;const auto path=root+"/control.sock";require(path.size()<sizeof(address.sun_path),"bounded socket path");memcpy(address.sun_path,path.c_str(),path.size()+1);require(!connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"connect common service");
 auto exact=[&](void*p,size_t n,bool writing){size_t done=0;while(done<n){auto count=writing?::send(fd,static_cast<char*>(p)+done,n-done,MSG_NOSIGNAL): ::recv(fd,static_cast<char*>(p)+done,n-done,0);if(count<0&&errno==EINTR)continue;require(count>0,"exact actual request/response");done+=size_t(count);}};
 uint32_t size=htonl(uint32_t(request.size()));exact(&size,4,true);exact(const_cast<char*>(request.data()),request.size(),true);int served=-1;std::thread server([&]{auto result=daemon->serve_one(2000);served=result?0:result.error->code;});exact(&size,4,false);size=ntohl(size);require(size&&size<=65536,"bounded response");std::string response(size,'\0');exact(&response[0],response.size(),false);close(fd);server.join();require(!served,"actual common service request");return j::parse(response);
}
}
int main(int argc,char**argv){
 const bool interleave=argc==8&&std::string(argv[7])=="interleave";require(argc==7||interleave,"modelA modelB catalog programs family-json output [interleave]");alarm(180);auto family_json=j::parse(read(argv[5]));auto*f=family_json.get();vxnn::Int16Family family{j::digest(j::member(f,"code_sha256")),j::digest(j::member(f,"implementation_sha256"))};const auto schema=j::digest(j::member(f,"schema_sha256"));auto*h=j::member(f,"hardware");BackendIdentity hardware;hardware.hardware_contract=j::digest(j::member(h,"hardware_contract"));hardware.bit=j::digest(j::member(h,"bit_sha256"));hardware.xsa=j::digest(j::member(h,"xsa_sha256"));hardware.compatible=vxnn::content_digest(j::str(j::member(h,"compatible")));hardware.provider_flags=1;hardware.device_generation=1;
 const auto catalog_before=vxnn::content_digest(read(argv[3]));const auto code_before=vxnn::content_digest(read("data/nnrt/programs/int16-family-v1/code.bin"));
 for(bool extended:{false,true}){
  if(interleave&&!extended)continue;
  std::mutex gate_mutex;std::condition_variable gate;bool waiting=false,released=false,gated=false;
  struct Pair{uint64_t logical,wire,sequence;};std::vector<Pair>pairs;
  char root[]="/tmp/vx-native-daemon-XXXXXX";require(mkdtemp(root),"independent private extension run");printf("NATIVE_DAEMON_OWNER=%s\n",root);fflush(stdout);DaemonConfig config;config.service.private_dir=root;config.identity=hardware;config.boot_id=extended?"extension-AB":"extension-A";config.evidence_dir=std::string(root)+"/evidence";config.config_sha=digest(71);config.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;require(!mkdir(config.evidence_dir.c_str(),0700),"private evidence");
  const auto catalog=std::string(root)+"/catalog.bin",manifest=std::string(root)+"/installation.json",seal_path=manifest+".sha256";seal(catalog,read(argv[3]));Transport*io=nullptr;unsigned opens=0,contexts=0;uint64_t expected_jobs=0;
  auto builder=[&](const StartupContext&context)->Result<ApplicationSet>{
   auto tree=j::parse("{}");number(tree.get(),"version",1);auto*hw=json_object_new_object();text(hw,"hardware_contract",vxnn::digest_hex(hardware.hardware_contract));text(hw,"bit",vxnn::digest_hex(hardware.bit));text(hw,"xsa",vxnn::digest_hex(hardware.xsa));text(hw,"compatible",vxnn::digest_hex(hardware.compatible));number(hw,"provider_flags",1);json_object_object_add(tree.get(),"hardware",hw);auto*c=json_object_new_object();text(c,"path",catalog);text(c,"sha256",vxnn::digest_hex(catalog_before));json_object_object_add(tree.get(),"catalog",c);auto*models=json_object_new_array();
   for(unsigned i=0;i<(extended?2u:1u);++i){auto package=value(vxnn::load_package(vxnn::Path(argv[1+i])),"independent frozen integer package");vxnn::OperatorRegistry registry;ok(vxnn::register_int16_operators(registry,family),"existing registered integer operators");auto caps=value(context.compute_backend->capabilities(),"same cached capabilities");auto plan=value(vxnn::prepare_int16_model(package,registry,caps,context.identity),"original integer preparation");require(plan.total_jobs==(i?3u:2u),"original A two ops/B three existing ops");expected_jobs+=plan.total_jobs;
    auto*n=json_object_new_object();text(n,"id",i?"model-B":"model-A");text(n,"input_id","sealed");text(n,"kind","int16");text(n,"package",absolute(argv[1+i]));text(n,"model",vxnn::digest_hex(package.model_id()));text(n,"plan",vxnn::digest_hex(plan.plan_id));text(n,"input",vxnn::digest_hex(package.assets().at("input.bin").sha));number(n,"resident_bytes",package.resident_bytes());number(n,"input_bytes",package.assets().at("input.bin").content->size());number(n,"job_budget_ns",60000000000ULL);auto*family_row=json_object_new_object();text(family_row,"code",vxnn::digest_hex(family.program_sha));text(family_row,"implementation",vxnn::digest_hex(family.implementation_sha));text(family_row,"schema",vxnn::digest_hex(schema));json_object_object_add(n,"family",family_row);json_object_array_add(models,n);
   }
   json_object_object_add(tree.get(),"nn",models);const auto bytes=j::dump(tree.get());seal(manifest,bytes);seal(seal_path,vxnn::digest_hex(vxnn::content_digest(bytes))+"\n");auto built=installed_applications_from(manifest,seal_path)(context);if(!built||!interleave)return built;auto apps=*built.value;
   auto prepare=apps.prepare_native;apps.prepare_native=[&,prepare](){
    if(!gated){gated=true;std::unique_lock<std::mutex>lock(gate_mutex);waiting=true;gate.notify_all();require(gate.wait_for(lock,std::chrono::seconds(10),[&]{return released;}),"bounded test-only ingress gate");}
    return prepare?prepare():Result<bool>::success(false);
   };
   auto finish=apps.compute.finish_gpu;apps.compute.finish_gpu=[&,finish](const WorkItem&w,UnifiedBackend&backend,ExecutionReceipt&receipt){
    auto done=finish(w,backend,receipt);if(done){require(matching_submission(receipt,gpu_work_cookie(w)),"native retirement has exact logical/wire/sequence/epoch binding");pairs.push_back({receipt.submitted_cookie,receipt.submitted_wire_cookie,receipt.submitted_sequence});}return done;
   };return Result<ApplicationSet>::success(std::move(apps));
  };
  auto created=Daemon::start(config,builder,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){++contexts;auto transport=std::make_unique<Transport>(argv[3],argv[4],hardware.compatible);io=transport.get();return transport;});
  if(!created){fprintf(stderr,"NATIVE_INTEGER_STARTUP_ERROR %d %s\n",created.error->code,created.error->detail.c_str());}require(bool(created),"U20 common native runtime must bind existing integer operators for model A and external model B");auto daemon=*created.value;require(opens==1&&contexts==1,"one underlying C owner per service, not one per network");
  if(interleave){
   auto key=[&](const char*name){return "\"version\":1,\"boot_id\":\""+config.boot_id+"\",\"client_id\":\"B\",\"request_id\":\""+name+"\"";};
   auto submit=[&](const char*name){auto reply=rpc(daemon,root,"{"+key(name)+",\"command\":\"submit\",\"entry_id\":\"model-B\",\"input_id\":\"sealed\",\"parameters\":{}}");require(j::boolean(j::member(reply.get(),"ok")),"queue installed model-B request");};
   submit("A");{std::unique_lock<std::mutex>lock(gate_mutex);require(gate.wait_for(lock,std::chrono::seconds(10),[&]{return waiting;}),"A1 completes before ingress timing gate");}
   submit("B");submit("C");{std::lock_guard<std::mutex>lock(gate_mutex);released=true;}gate.notify_all();
   // Request A's result can be ready while C owns the single active port.
   // Complete all requested work before asking that port to retire any backing.
   for(const char*name:{"A","B","C"}){bool done=false;const auto until=now()+UINT64_C(30000000000);while(!done&&now()<until){auto reply=rpc(daemon,root,"{"+key(name)+",\"command\":\"status\"}");require(j::boolean(j::member(reply.get(),"ok")),"all-request status");auto state=j::str(j::member(j::member(reply.get(),"result"),"status"));require(state!="FAILED","interleaved request has no new execution error");done=state=="COMPLETED";if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(done&&!daemon->first_error(),"all requests complete before device resource reaping");}
   for(const char*name:{"A","B","C"}){
    bool done=false;const auto until=now()+UINT64_C(30000000000);while(!done&&now()<until){auto reply=rpc(daemon,root,"{"+key(name)+",\"command\":\"status\"}");require(j::boolean(j::member(reply.get(),"ok")),"three-request native status");auto status=j::str(j::member(j::member(reply.get(),"result"),"status"));require(status!="FAILED","interleaved legitimate request never fails as replay");done=status=="COMPLETED";if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(done&&!daemon->first_error(),"all three native requests complete without retry");
    auto result=rpc(daemon,root,"{"+key(name)+",\"command\":\"result\"}");auto*body=j::member(result.get(),"result");require(j::str(j::member(body,"numeric_class"))=="NOT_EVALUATED"&&!j::member(body,"gpu_raw_cycles"),"HOST reference never physical application acceptance");auto*artifact=j::at(j::member(body,"artifacts"),0);auto relative=j::str(j::member(artifact,"path"));require(relative.size()&&relative[0]!='/'&&relative.find("..")==std::string::npos,"private native output path");auto actual=read(std::string(root)+"/outputs/"+relative);require(actual==read(std::string(argv[2])+"/reference.i64")&&vxnn::content_digest(actual)==j::digest(j::member(artifact,"sha256")),"all three complete outputs equal frozen independent integer reference");
    auto duplicate=rpc(daemon,root,"{"+key(name)+",\"command\":\"submit\",\"entry_id\":\"model-B\",\"input_id\":\"sealed\",\"parameters\":{}}");require(j::boolean(j::member(duplicate.get(),"ok")),"request replay remains idempotent");auto reaped=rpc(daemon,root,"{"+key(name)+",\"command\":\"reap\"}");require(j::boolean(j::member(reaped.get(),"ok")),"release interleaved result");
   }
   ok(daemon->shutdown(),"interleaved native resources retire");require(pairs.size()==9&&s8_owner_bridge_starts(io->core)==9&&s8_owner_bridge_acks(io->core)==9&&io->loads==1&&!s8_owner_bridge_live(io->core),"one program/context, nine original starts/ACKs, no leaked device object");bool descending=false;for(size_t i=1;i<pairs.size();++i){require(pairs[i].wire>pairs[i-1].wire&&pairs[i].sequence>pairs[i-1].sequence,"actual wire cookies and sequences increase");descending=descending||pairs[i].logical<pairs[i-1].logical;}require(descending,"real service exercised preparation/submission order inversion");
   require(vxnn::content_digest(read(argv[3]))==catalog_before&&vxnn::content_digest(read("data/nnrt/programs/int16-family-v1/code.bin"))==code_before,"interleaving preserves catalog and GPU program");puts("U25_THREE_INSTALLED_NATIVE_REQUESTS_NINE_JOBS_EXACT_REFERENCE_COOKIE_INVERSION_PASS_HOST_NOT_GPU gpu_raw_cycles=null");continue;
  }
  for(unsigned i=0;i<(extended?2u:1u);++i){const auto name=i?"model-B":"model-A";const auto key="\"version\":1,\"boot_id\":\""+config.boot_id+"\",\"client_id\":\"B\",\"request_id\":\""+name+"\"";auto submitted=rpc(daemon,root,"{"+key+",\"command\":\"submit\",\"entry_id\":\""+name+"\",\"input_id\":\"sealed\",\"parameters\":{}}");require(j::boolean(j::member(submitted.get(),"ok")),"actual integer source admitted");bool done=false;const auto until=now()+UINT64_C(30000000000);while(!done&&now()<until){auto status=rpc(daemon,root,"{"+key+",\"command\":\"status\"}");require(j::boolean(j::member(status.get(),"ok")),"integer status query");auto state=j::str(j::member(j::member(status.get(),"result"),"status"));require(state!="FAILED","first integer failure retained");done=state=="COMPLETED";if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(done&&!daemon->first_error(),"native integer computation completes");
   auto result=rpc(daemon,root,"{"+key+",\"command\":\"result\"}");auto*body=j::member(result.get(),"result");require(j::str(j::member(body,"numeric_class"))=="NOT_EVALUATED"&&!j::member(body,"gpu_raw_cycles"),"HOST PE model cannot claim physical GPU/numeric PASS");auto*a=j::at(j::member(body,"artifacts"),0);const auto relative=j::str(j::member(a,"path"));require(!relative.empty()&&relative[0]!='/'&&relative.find("..") ==std::string::npos,"retained private relative output");auto actual=read(std::string(root)+"/outputs/"+relative),reference=read(std::string(argv[i+1])+"/reference.i64");require(actual==reference&&vxnn::content_digest(actual)==j::digest(j::member(a,"sha256"))&&j::str(j::member(a,"layout"))=="RM_I48S64","entire native service output equals independent frozen Python integer reference");
   auto duplicate=rpc(daemon,root,"{"+key+",\"command\":\"submit\",\"entry_id\":\""+name+"\",\"input_id\":\"sealed\",\"parameters\":{}}");require(j::boolean(j::member(duplicate.get(),"ok")),"idempotent integer replay");auto reaped=rpc(daemon,root,"{"+key+",\"command\":\"reap\"}");require(j::boolean(j::member(reaped.get(),"ok")),"integer result release");
  }
  require(s8_owner_bridge_starts(io->core)==expected_jobs&&s8_owner_bridge_acks(io->core)==expected_jobs&&io->loads==1,"A/B use one exact program/schema, each C job once with ACK");ok(daemon->shutdown(),"integer program/data/result lifetime retired");require(!s8_owner_bridge_live(io->core),"all integer device objects retired");require(vxnn::content_digest(read(argv[3]))==catalog_before&&vxnn::content_digest(read("data/nnrt/programs/int16-family-v1/code.bin"))==code_before,"extension does not change catalog or frozen GPU program");
  printf("U20_INTEGER_NATIVE_%s jobs=%llu EXACT_FROZEN_REFERENCE_HOST_C_PE_NOT_GPU gpu_raw_cycles=null\n",extended?"A_PLUS_B":"A",(unsigned long long)expected_jobs);
 }
 puts("U20_NATIVE_INTEGER_PACKAGE_ONLY_EXTENSION_SAME_C_DRIVER_OWNER_PROGRAM_AND_KERNEL_SOURCES_PASS_HOST_NOT_BOARD gpu_raw_cycles=null");
}

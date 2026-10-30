#include <vxunified/mosaic_request.hpp>
#include "scheduler_fixture.hpp"
#include "mosaic_owner_bridge.h"
#include "nnrt/package/json.hpp"
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <time.h>
using namespace vxunified;
extern "C" int mosaic_owner_validate_args(const void*p){VxnnMosaicShape shape{};return vxnn_mosaic_validate(static_cast<const VxnnMosaicArgs*>(p),true,&shape);}
namespace {
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport {
 void*bridge;Transport(const char*catalog,const char*code,const char*ro,Digest compatible):bridge(mosaic_owner_bridge_create(catalog,code,ro,compatible.data())){}~Transport(){mosaic_owner_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return mosaic_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}
};
}
int main(int argc,char**argv){
 require(argc==6,"actual package/catalog/CODE/RO/mode");auto package=value(vxnn::load_package(vxnn::Path(argv[1])),"actual original weight package");const auto&bytes=*package.assets().at("binding.json").content;auto config=vxnn::json::parse(std::string(bytes.begin(),bytes.end()));auto*c=config.get();
 // Host fixture only. Production U20 supplies a separately sealed manifest,
 // never trusts arbitrary package-declared implementation/hardware digests.
 vxnn::MosaicFamily family{vxnn::json::digest(vxnn::json::member(c,"code_sha256")),vxnn::json::digest(vxnn::json::member(c,"readonly_sha256")),vxnn::json::digest(vxnn::json::member(c,"schema_sha256")),vxnn::json::digest(vxnn::json::member(c,"implementation_sha256"))};auto*h=vxnn::json::member(c,"hardware");Digest compatible=vxnn::content_digest(vxnn::json::str(vxnn::json::member(h,"compatible")));
 char dir[]="/tmp/vx-mosaic-owner-XXXXXX";require(mkdtemp(dir)!=nullptr,"private owner");ServiceConfig sc;sc.private_dir=dir;auto owner=value(DeviceOwner::acquire(sc,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"single borrowed owner");std::shared_ptr<MosaicRequest>app;Transport*io=nullptr;unsigned traces=0,publishes=0,phases=0;const std::string mode=argv[5];std::optional<ExecutionReceipt>first_error;
 ExecutionHooks hooks;hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().finish_gpu(w,b,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return app->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,std::move(hooks),[&](int fd){require(fd==owner->fd(),"single borrowed fd");auto v=std::make_unique<Transport>(argv[2],argv[3],argv[4],compatible);io=v.get();return v;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C host identity metadata");auto backend=value(port->adapter_backend(),"shared native backend");auto caps=value(backend->capabilities(),"actual C pool caps");
 std::ifstream file(argv[2],std::ios::binary);std::vector<uint8_t>raw((std::istreambuf_iterator<char>(file)),{});Digest sha{};vxl_hunet_hash(raw.data(),raw.size(),sha.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sha),"full installed identity catalog");auto residency=value(ProgramResidency::create(catalog,backend),"shared single owner residency");
 MosaicTraceSink trace=[&](const RequestKey&,const MosaicJobTrace&t){
  require(t.node&&t.count&&t.begin_ns&&t.end_ns>=t.begin_ns&&!t.gpu_raw_cycles&&t.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"trace scope and original job times");auto encoded=value(encode_mosaic_job_trace(t),"fixed512 evidence packet");require(encoded.size()==512,"evidence budget binding");
  if(t.error){return Result<void>::success();}require(t.index==traces&&t.linux_detail&&t.linux_detail->sequence&&!t.linux_detail->cycles_valid,"strict actual ACK snapshot and streaming order, no fake cycle");traces++;if(t.phase_complete)phases++;
  if(mode=="trace-error"||(mode=="final-trace-error"&&t.index+1==app->size())){return Result<void>::failure({ENOSPC,"TEST_TRACE","original raw trace persistence failure"});}return Result<void>::success();
 };
 MosaicOutputSink output=[&](const WorkRequest&w,vxnn::Bytes v,const MosaicRequestReceipt&r){publishes++;require(v.size==1152&&r.completed_jobs==88&&std::all_of(v.data,v.data+v.size,[](uint8_t b){return b==0;}),"explicit host ZERO MARKER, never GPU inference");if(mode=="publish-error")return Result<PublishedResult>::failure({ENOSPC,"TEST_PUBLISH","original output persistence failure"});PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=AppKind::NN;p.lease.hardware=hw.hardware_contract;p.lease.model=w.model;p.lease.plan=w.plan;p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ResultArtifact a;a.path="host-zero-marker.f32";a.layout="CHW_F32";a.shape={3,8,12};a.bytes=v.size;vxl_hunet_hash(v.data,v.size,a.sha.data());p.lease.artifacts={a};if(mode=="publish-sha")p.lease.artifacts[0].sha[0]^=1;if(mode=="publish-scope")p.lease.scope=vxnn::EvidenceScope::REAL_BOARD;if(mode=="publish-numeric")p.numeric=vxnn::NumericClass::PASS;return Result<PublishedResult>::success(p);};
 RequestKey key{"boot","client","mosaic"};auto prepared=MosaicRequest::prepare(package,family,caps,hw,key,residency,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,2,60000000000ULL,trace,output);if(!prepared)std::fprintf(stderr,"MOSAIC_OWNER_ERROR %s\n",prepared.error->detail.c_str());app=value(prepared,"native Mosaic request must prepare");require(app->size()==88&&app->plan().nodes.size()==77&&mosaic_owner_bridge_live(io->bridge)==0&&mosaic_owner_bridge_starts(io->bridge)==0,"lazy preparation allocates no device objects or GPU jobs");
 uint64_t phase=0,prefix=0;for(uint64_t i=0;i<app->size();i++){while(i>=prefix+app->plan().nodes[phase].job_count)prefix+=app->plan().nodes[phase++].job_count;auto w=value(app->get(i),"pure lazy indexed work");require(w.work_index==i&&w.node==app->plan().nodes[phase].node.id&&w.kind==WorkKind::GPU&&gpu_work_cookie(w)==(2ULL<<32)+i+1,"phase prefix and unique global cookie");}require(!app->get(88),"source tail bound");
 auto bad=family;bad.implementation_sha[0]^=1;require(!MosaicRequest::prepare(package,bad,caps,hw,key,residency,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,3,60000000000ULL,trace,output),"implementation mismatch");require(!MosaicRequest::prepare(package,family,caps,hw,key,residency,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,0,60000000000ULL,trace,output),"cookie namespace required");
 // Delegates above can run only after immutable source admission.
 auto runtime=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[&](const ExecutionReceipt&r,DeliveryStage stage){if(stage==DeliveryStage::EXECUTION_ERROR)first_error=r;return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"one shared runtime");value(runtime->submit(app->request()),"bounded source admission");
 if(mode=="cancel"){ok(runtime->cancel(key),"cancel metadata only");ok(runtime->reap_result(key),"retire never materialized");require(!mosaic_owner_bridge_starts(io->bridge)&&!mosaic_owner_bridge_live(io->bridge)&&!traces,"no post-cancel load or start");ok(runtime->shutdown(),"runtime closes");ok(residency->shutdown(),"empty residency");puts("U13_MOSAIC_OWNER_CANCEL_PASS_HOST_NOT_MATH gpu_raw_cycles=null");return 0;}
 if(mode=="guard")mosaic_owner_bridge_corrupt_guard(io->bridge);
 if(mode=="guard"||mode=="trace-error"){
  require(!runtime->run_one()&&first_error&&app->receipt().first_error&&mosaic_owner_bridge_starts(io->bridge)==1&&mosaic_owner_bridge_acks(io->bridge)==1&&mosaic_owner_bridge_live(io->bridge)>5&&!publishes,"first guard/trace failure retains exact backing after ACK");require(!runtime->run_one()&&!runtime->retry_delivery()&&!runtime->shutdown()&&!residency->shutdown()&&mosaic_owner_bridge_starts(io->bridge)==1,"no resubmit or automatic release");puts("U13_MOSAIC_OWNER_FIRST_ERROR_RETAINED_PASS_HOST_NOT_MATH gpu_raw_cycles=null");return 0;
 }
 for(unsigned i=0;i<88;i++){
  auto ran=runtime->run_one();if(!ran){require(i==87&&(mode=="final-trace-error"||mode.compare(0,8,"publish-")==0)&&first_error&&app->receipt().first_error&&mosaic_owner_bridge_starts(io->bridge)==88&&mosaic_owner_bridge_acks(io->bridge)==88&&mosaic_owner_bridge_live(io->bridge)>5&&!app->receipt().retired,"last trace/output error retains original complete device backing");require(!runtime->run_one()&&!runtime->retry_delivery()&&!runtime->shutdown()&&!residency->shutdown(),"last failure never retries/clears state");puts("U13_MOSAIC_OWNER_FINAL_ERROR_RETAINED_PASS_HOST_NOT_MATH gpu_raw_cycles=null");return 0;}
 }
 require(mode=="flow"&&traces==88&&phases==77&&publishes==1&&app->receipt().completed_jobs==88&&app->receipt().completed_phases==77&&app->receipt().retired&&mosaic_owner_bridge_starts(io->bridge)==88&&mosaic_owner_bridge_acks(io->bridge)==88&&mosaic_owner_bridge_live(io->bridge)==5&&!residency->stats().owned,"all bounded jobs and phases retire once, only sealed program remains cached");auto result=value(runtime->take_result(key),"durable raw result");require(result.numeric==vxnn::NumericClass::NOT_EVALUATED&&!result.gpu_raw_cycles,"host control flow cannot claim numerical or GPU performance");ok(runtime->reap_result(key),"published result retirement");ok(runtime->shutdown(),"runtime shutdown");ok(residency->shutdown(),"last program release");require(!mosaic_owner_bridge_live(io->bridge),"no leaked owner objects");puts("U13_MOSAIC_OWNER_88_C_PROTOCOL_JOBS77_PHASES_PASS_NOT_GPU_MATH gpu_raw_cycles=null");
}

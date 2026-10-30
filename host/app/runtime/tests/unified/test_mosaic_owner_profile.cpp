#include <vxunified/mosaic_request.hpp>
#include "scheduler_fixture.hpp"
#include "mosaic_owner_bridge.h"
#include "mosaic_owner_profile_hooks.h"
#include "nnrt/package/json.hpp"
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <time.h>
using namespace vxunified;
static unsigned admitted_abi;
extern "C" int mosaic_owner_validate_args(const void*p){const auto*a=static_cast<const VxnnMosaicArgs*>(p);VxnnMosaicShape shape{};MosaicWorkLayoutV1 layout{};return !a||a->abi!=admitted_abi?-EINVAL:mosaic_profile_layout(*a,true,&shape,&layout);}
extern "C" int owner512_snapshot_matches(const void*);
namespace {
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport {
 void*bridge;Transport(const char*catalog,const char*code,const char*ro,Digest compatible):bridge(mosaic_owner_bridge_create(catalog,code,ro,compatible.data())){}~Transport(){mosaic_owner_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return mosaic_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}
};
bool same_error(const vxnn::Error&a,const vxnn::Error&b){return a.code==b.code&&a.category==b.category&&a.detail==b.detail;}
}
int main(int argc,char**argv){
 require(argc==6,"fresh500 package/catalog/CODE/RO/mode");
 auto package=value(vxnn::load_package(vxnn::Path(argv[1])),"fresh exact HOST closure package");
 const auto&bytes=*package.assets().at("binding.json").content;auto config=vxnn::json::parse(std::string(bytes.begin(),bytes.end()));auto*c=config.get();
 vxnn::MosaicFamily family{vxnn::json::digest(vxnn::json::member(c,"code_sha256")),vxnn::json::digest(vxnn::json::member(c,"readonly_sha256")),vxnn::json::digest(vxnn::json::member(c,"schema_sha256")),vxnn::json::digest(vxnn::json::member(c,"implementation_sha256"))};
 auto*h=vxnn::json::member(c,"hardware");Digest compatible=vxnn::content_digest(vxnn::json::str(vxnn::json::member(h,"compatible")));
 unsigned options=unsigned(vxnn::json::u64(vxnn::json::member(vxnn::json::member(c,"workspace_policy"),"profiling_options")));admitted_abi=unsigned(vxnn::json::u64(vxnn::json::member(vxnn::json::member(c,"workspace_policy"),"abi")));require(admitted_abi==3||admitted_abi==4,"explicit supported family ABI");const std::string mode=argv[5];owner512_config(mode.c_str(),options);
 const char*tmp=getenv("TMPDIR");require(tmp&&std::string(tmp).find("/mnt/g/s8_software_r5_20261028/mosaic-owner-profile-v7-512/")==0,"G-only persistent owner TMPDIR");
 const char*ram_owner=getenv("OWNER512_PRIVATE_DIR");require(ram_owner&&std::string(ram_owner).find("/dev/shm/s8_owner512_")==0,"explicit unique RAM private owner, exact production permissions");
 ServiceConfig sc;sc.private_dir=ram_owner;auto owner=value(DeviceOwner::acquire(sc,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"single borrowed owner");
 std::shared_ptr<MosaicRequest>app;Transport*io=nullptr;uint64_t traces=0,publishes=0,phases=0;std::optional<ExecutionReceipt>first_error;
 ExecutionHooks hooks;hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().prepare_gpu(w,b,r);};
 hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){
  ExecutionReceipt broken=r;ExecutionReceipt*use=&r;
  if(mode=="bad-ack"){broken.outcome.acknowledged=false;use=&broken;}
  if(mode=="bad-snapshot"){require(bool(broken.linux_detail),"snapshot present before adversarial mutation");++broken.linux_detail->sequence;use=&broken;}
  bool valid=use->outcome.retired&&use->outcome.acknowledged&&matching_submission(*use,gpu_work_cookie(w))&&use->linux_detail&&owner512_snapshot_matches(&*use->linux_detail);
  if(mode!="bad-ack"&&mode!="bad-snapshot")require(valid,"C original snapshot and matching ACK reach production finish");
  owner512_finish_begin(valid);auto result=app->handlers().finish_gpu(w,b,*use);owner512_finish_end();return result;
 };
 hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return app->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,std::move(hooks),[&](int fd){require(fd==owner->fd(),"single borrowed fd");auto v=std::make_unique<Transport>(argv[2],argv[3],argv[4],compatible);io=v.get();return v;});
 auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C fixture identity");require(hw.provider_flags==4,"joint provider4");
 auto backend=value(port->adapter_backend(),"shared native backend");auto caps=value(backend->capabilities(),"actual C pool caps");
 std::ifstream file(argv[2],std::ios::binary);std::vector<uint8_t>raw((std::istreambuf_iterator<char>(file)),{});Digest sha{};vxl_hunet_hash(raw.data(),raw.size(),sha.data());
 auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sha),"exact v6 installed catalog");auto residency=value(ProgramResidency::create(catalog,backend),"single owner residency");
 MosaicTraceSink trace=[&](const RequestKey&,const MosaicJobTrace&t){
  require(t.node&&t.count&&t.begin_ns&&t.end_ns>=t.begin_ns&&!t.gpu_raw_cycles&&t.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"explicit HOST scope and no measured GPU cycles");
  if(t.error)return Result<void>::success();
  require(t.index==traces&&t.linux_detail&&t.linux_detail->sequence&&!t.linux_detail->cycles_valid&&owner512_snapshot_matches(&*t.linux_detail),"original matching ACK snapshot persisted unchanged");
  require(bool(t.stage_args)==bool(options)&&bool(t.stage_records)==bool(options),"options0 no profile records; options1 bound records");
  auto encoded=value(encode_mosaic_extended_trace(t),"production profile2 codec");
  require(encoded.size()==(options?1536u:512u),"exact trace format version size");
  if(!options)require(!t.profile_read_ns&&!t.profile_prepare_ns,"options0 zero profile host timestamps");
  owner512_trace(t.index,bool(t.stage_records));++traces;if(t.phase_complete)++phases;
  if(mode=="trace-error"||(mode=="final-trace-error"&&t.index+1==app->size()))return Result<void>::failure({ENOSPC,"TEST_TRACE","original raw trace persistence failure"});
  return Result<void>::success();
 };
 MosaicOutputSink output=[&](const WorkRequest&w,vxnn::Bytes v,const MosaicRequestReceipt&r){
  ++publishes;require(v.size==1152&&r.completed_jobs==app->size()&&std::all_of(v.data,v.data+v.size,[](uint8_t b){return b==0;}),"HOST ZERO MARKER only, never GPU numeric inference");
  if(mode=="publish-error")return Result<PublishedResult>::failure({ENOSPC,"TEST_PUBLISH","original output persistence failure"});
  PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=AppKind::NN;p.lease.hardware=hw.hardware_contract;p.lease.model=w.model;p.lease.plan=w.plan;p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
  ResultArtifact a;a.path="host-zero-marker.f32";a.layout="CHW_F32";a.shape={3,8,12};a.bytes=v.size;vxl_hunet_hash(v.data,v.size,a.sha.data());p.lease.artifacts={a};
  if(mode=="publish-sha")p.lease.artifacts[0].sha[0]^=1;
  if(mode=="publish-scope")p.lease.scope=vxnn::EvidenceScope::REAL_BOARD;
  if(mode=="publish-numeric")p.numeric=vxnn::NumericClass::PASS;
  return Result<PublishedResult>::success(p);
 };
 RequestKey key{"boot","client","mosaic"};
 app=value(MosaicRequest::prepare(package,family,caps,hw,key,residency,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,2,60000000000ULL,trace,output),"native ABI3 Mosaic prepare");
 const uint64_t jobs=app->size(),nodes=app->plan().nodes.size();require(jobs==app->plan().total_jobs&&nodes==47&&jobs>=nodes,"dynamic47-node request count");
 require(!mosaic_owner_bridge_live(io->bridge)&&!mosaic_owner_bridge_starts(io->bridge),"lazy admission no allocations/start");
 uint64_t phase=0,prefix=0;for(uint64_t i=0;i<jobs;++i){while(i>=prefix+app->plan().nodes[phase].job_count)prefix+=app->plan().nodes[phase++].job_count;auto w=value(app->get(i),"lazy indexed work");require(w.work_index==i&&w.node==app->plan().nodes[phase].node.id&&gpu_work_cookie(w)==(2ULL<<32)+i+1,"dynamic phase/cookie mapping");}require(!app->get(jobs),"dynamic tail bound");
 auto runtime=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[&](const ExecutionReceipt&r,DeliveryStage stage){if(stage==DeliveryStage::EXECUTION_ERROR)first_error=r;return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"one runtime");
 value(runtime->submit(app->request()),"bounded source admission");
 if(mode=="cancel"){
  ok(runtime->cancel(key),"unmaterialized cancel");ok(runtime->reap_result(key),"retire never materialized");auto s=owner512_get();require(!s.jobs&&!s.profile_reads&&!s.header_writes&&!s.injected&&!traces&&!mosaic_owner_bridge_live(io->bridge),"cancel has no profile work/start/materialization");
  ok(runtime->shutdown(),"shutdown");ok(residency->shutdown(),"empty residency");owner512_print();std::puts("OWNER512_CANCEL_PASS_HOST_ZERO_MARKER_NOT_GPU");return 0;
 }
 if(mode=="guard")mosaic_owner_bridge_corrupt_guard(io->bridge);
 for(uint64_t i=0;i<jobs;++i){
  auto ran=runtime->run_one();
  if(!ran){ std::fprintf(stderr,"OWNER512_ORIGINAL_FAILURE index=%llu code=%d category=%s detail=%s\n",(unsigned long long)i,ran.error->code,ran.error->category.c_str(),ran.error->detail.c_str());
   bool early=mode=="guard"||mode=="trace-error"||mode=="bad-ack"||mode=="bad-snapshot"||mode=="bad-core"||mode=="bad-tag"||mode=="bad-version"||mode=="zero-csr";
   require(i==(early?0:jobs-1)&&mode!="flow"&&first_error&&app->receipt().first_error&&!app->receipt().retired,"expected first/final failure has exact backing");
   require(mosaic_owner_bridge_starts(io->bridge)==i+1&&mosaic_owner_bridge_acks(io->bridge)==i+1&&mosaic_owner_bridge_live(io->bridge)>5,"failed request backing remains after ACK");
   auto original=*app->receipt().first_error;require(same_error(*ran.error,original),"original runtime error preserved");
   if(mode=="guard")require(original.code==EBADMSG&&original.category=="ARENA_GUARD","original guard error");
   else if(mode=="bad-ack"||mode=="bad-snapshot")require(original.code==EBUSY&&original.category=="MOSAIC_OWNER","original binding error");
   else if(mode=="bad-core"||mode=="bad-tag"||mode=="bad-version")require(original.code==EPROTO&&original.category=="MOSAIC_PROFILE","invalid profiles rejected, not zero time");
   else if(mode=="zero-csr")require(original.code==ENODATA&&original.category=="MOSAIC_PROFILE"&&first_error->linux_detail&&first_error->linux_detail->result==0&&!publishes,"zero CSR is unavailable timing evidence, not GPU math failure or zero elapsed");
   else if(mode=="trace-error"||mode=="final-trace-error")require(original.code==ENOSPC&&original.category=="TEST_TRACE","original trace persistence error");
   else if(mode=="publish-error")require(original.code==ENOSPC&&original.category=="TEST_PUBLISH","original output error");
   auto s=owner512_get();uint64_t expected_reads=options?4*(i+1):0;
   if(mode=="guard"||mode=="bad-ack"||mode=="bad-snapshot")expected_reads=0;
   if(mode=="bad-core"||mode=="bad-tag"||mode=="bad-version")expected_reads=3;
   if(mode=="zero-csr")expected_reads=1;
   require(s.profile_reads==expected_reads&&s.header_writes==(options?4*(i+1):0),"failure has exact bounded read/clear count");
   unsigned live=mosaic_owner_bridge_live(io->bridge);
   require(!runtime->run_one()&&!runtime->retry_delivery()&&!runtime->shutdown()&&!residency->shutdown(),"no resubmit, retry or automatic release");
   auto after=owner512_get();require(after.jobs==s.jobs&&after.profile_reads==s.profile_reads&&after.header_writes==s.header_writes&&mosaic_owner_bridge_live(io->bridge)==live&&same_error(*app->receipt().first_error,original),"failure frozen with backing and original error");
   owner512_print();std::printf("OWNER512_RETAINED_PASS mode=%s options=%u failed_index=%llu code=%d category=%s backing=%u HOST_ZERO_MARKER_NOT_GPU\n",mode.c_str(),options,(unsigned long long)i,original.code,original.category.c_str(),live);return 0;
  }
 }
 require(mode=="flow"&&traces==jobs&&phases==nodes&&publishes==1&&app->receipt().completed_jobs==jobs&&app->receipt().completed_phases==nodes&&app->receipt().retired&&mosaic_owner_bridge_starts(io->bridge)==jobs&&mosaic_owner_bridge_acks(io->bridge)==jobs&&mosaic_owner_bridge_live(io->bridge)==5&&!residency->stats().owned,"complete dynamic flow retires once");
 auto s=owner512_get();require(owner512_current_abi()==admitted_abi,"actual native ABI matches admitted model policy");require(s.jobs==jobs&&s.profile_reads==(options?jobs*4:0)&&s.header_writes==(options?jobs*4:0)&&s.injected==(options?jobs*4:0),"exact options0/1 profile event counts");
 auto result=value(runtime->take_result(key),"durable raw HOST ZERO MARKER");require(result.numeric==vxnn::NumericClass::NOT_EVALUATED&&!result.gpu_raw_cycles,"HOST flow cannot pass numerics/performance");
 ok(runtime->reap_result(key),"result retirement");ok(runtime->shutdown(),"runtime shutdown");ok(residency->shutdown(),"program release");require(!mosaic_owner_bridge_live(io->bridge),"no leaked fixture objects");
 owner512_print();std::printf("OWNER512_FLOW_PASS jobs=%llu nodes=%llu options=%u scope=HOST_PROTOCOL_TEST HOST_ZERO_MARKER_NOT_GPU_NOT_NUMERIC_NOT_PERFORMANCE\n",(unsigned long long)jobs,(unsigned long long)nodes,options);
}

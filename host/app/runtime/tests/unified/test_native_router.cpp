#include <vxunified/native.hpp>
#include <vxunified/journal.hpp>
#include "scheduler_fixture.hpp"
#include "backend_bridge.h"
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <new>
static std::atomic<int> fail_after{-1};
void*operator new(std::size_t n){int remaining=fail_after.load();if(remaining>=0&&fail_after.fetch_sub(1)==0){fail_after=-1;throw std::bad_alloc();}auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
namespace {
struct Transport:IoctlTransport {
 void*bridge;unsigned acks=0;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long cmd,void*p)override{
  if(cmd==VXL_V2_WAIT)backend_bridge_finish(bridge,1);
  if(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_ACK)++acks;
  int n=backend_bridge_call(bridge,cmd,p);
  if(!n&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_READ))){auto*q=static_cast<vxl_compute_v2_request*>(p);auto*c=reinterpret_cast<vxl_v2_completion*>(q->user_ptr);c->cycles_valid=0;c->elapsed_raw=0;}
  return n;
 }
 uint64_t now_ns()override{return 100;}
};
struct Logical:WorkSource {
 uint64_t cookie=0,epoch=0;Digest sha{};
 uint64_t size()const override{return 2;}Digest identity()const override{return sha;}
 Result<WorkItem>get(uint64_t i)const override{
  WorkItem w;w.node=i+1;w.kind=i?WorkKind::TRANSFER:WorkKind::GPU;w.work_index=i;
  if(i){w.dependencies={0};w.operation=CpuTaskRef{"router.fixture.output",2};}else w.operation=DeferredGpuRef{"router.fixture.gpu",1,epoch,cookie};
  return Result<WorkItem>::success(w);
 }
};
}
int main(int argc,char**argv){
 require(argc==3,"catalog/mode");std::string mode=argv[2];char dir[]="/tmp/vx-native-router-XXXXXX";require(mkdtemp(dir),"private owner");ServiceConfig config;config.private_dir=dir;
 unsigned opens=0,prepared=0,finished=0,retired=0,cpu=0;vxnn::CpuPool*pool_identity=nullptr;
 auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;
 ExecutionHooks route;
 route.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};
 route.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};
 route.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};
 route.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,route,[&](int fd){require(fd==owner->fd(),"no second fd");auto t=std::make_unique<Transport>(argv[1]);io=t.get();return std::unique_ptr<IoctlTransport>(std::move(t));});
 auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual host C CAPS");auto backend=value(port->adapter_backend(),"actual common backend");
 std::string journal_dir=std::string(argv[1])+".native-journal-"+mode;require(mkdir(journal_dir.c_str(),0700)==0,"fresh native journal directory");char absolute[4096];require(realpath(journal_dir.c_str(),absolute),"absolute private native evidence");auto journal=value(EvidenceJournal::open(absolute),"real native routing evidence");
 const auto scope=mode=="admission-declared"?vxnn::EvidenceScope::REAL_BOARD:vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto rt=value(Runtime::start(owner,hw,scope,port,[journal](const ExecutionReceipt&r,DeliveryStage s){return journal->save_work(r,s);},[journal](const PublishedResult&p){return journal->save_result(p);}),"common Runtime");
 StartupContext context;context.borrowed_fd=owner->fd();context.identity=hw;context.compute_backend=backend;context.runtime=rt;context.results=rt->trusted_results();context.journal=journal;context.scope=scope;context.boot_id="boot";context.private_dir=dir;
 auto made=NativeRouter::create(context);require(bool(made),"U20 trusted native request router must share common service roots");router=*made.value;
 if(mode=="admission"||mode=="admission-declared"){
  auto admitted=admit_native_startup(context,1);require(bool(admitted),"U20 native admission must accept exact common roots without promoting declared bit identity");
  auto bad=context;bad.identity.board_identity_verified=true;require(!admit_native_startup(bad,1),"caller cannot promote manifest/CAPS identity to measured bit proof");
  bad=context;bad.identity.device_generation++;require(!admit_native_startup(bad,1),"old/foreign epoch cannot enter native factory");
  bad=context;bad.borrowed_fd=-1;require(!admit_native_startup(bad,1),"no separate device owner");
  bad=context;bad.compute_backend.reset();require(!admit_native_startup(bad,1),"actual borrowed backend required");
  bad=context;bad.results.reset();require(!admit_native_startup(bad,1),"same common result store required");
  bad=context;bad.boot_id.clear();require(!admit_native_startup(bad,1),"boot binding required");
  bad=context;bad.scope=scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::EvidenceScope::HOST_PROTOCOL_TEST:vxnn::EvidenceScope::REAL_BOARD;require(!admit_native_startup(bad,1),"factory cannot choose evidence scope");
  require(!admit_native_startup(context,2)&&!admit_native_startup(context,0),"exact frozen provider admission");
  if(scope==vxnn::EvidenceScope::REAL_BOARD){bad=context;bad.journal.reset();require(!admit_native_startup(bad,1),"real-I/O declaration still needs native durable journal");}
  require(!context.identity.board_identity_verified&&opens==1&&!backend_bridge_starts(io->bridge)&&rt->stats().scheduler.requests==0,"constructor-only test performs zero GPU work and publishes no result");
  ok(rt->shutdown(),"empty declared-context close");puts("U20_NATIVE_STARTUP_ROOTS_DECLARED_IDENTITY_ZERO_WORK_PASS_HOST_NOT_BOARD gpu_raw_cycles=null");return 0;
 }
 auto foreign=context;foreign.identity.xsa[0]^=1;require(!NativeRouter::create(foreign),"foreign physical identity refused");foreign=context;foreign.scope=vxnn::EvidenceScope::REAL_BOARD;require(!NativeRouter::create(foreign),"scope cannot be raised by native factory");foreign=context;foreign.borrowed_fd=-1;require(!NativeRouter::create(foreign),"factory cannot supply another owner fd");foreign=context;foreign.results.reset();require(!NativeRouter::create(foreign),"factory must use common result store");
 Digest entry{};backend_bridge_entry(io->bridge,entry.data());auto program=value(backend->program_load(entry),"frozen C control fixture program");auto args=value(backend->allocate(4,vxnn::AddressDomain::PL,3),"shared immutable fixture args");uint32_t one=1;ok(backend->copy(args,vxnn::Bytes{reinterpret_cast<uint8_t*>(&one),4}),"args");
 auto hooks=[&](AppKind app){ExecutionHooks h;
  h.prepare_gpu=[&,app](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&){require(&b==backend.get()&&w.app==app,"exact owner and synthetic logical application");++prepared;GPUJobDesc d;d.cookie=gpu_work_cookie(w);d.program_handle=program.handle;d.program_generation=program.program_generation;d.device_generation=program.device_generation;d.args=args;d.deadline_ns=1000000;return Result<GPUJobDesc>::success(d);};
  h.finish_gpu=[&,app](const WorkItem&w,UnifiedBackend&,ExecutionReceipt&r){require(w.app==app&&r.outcome.retired&&r.outcome.acknowledged,"correct hook only after original ACK");++finished;return Result<void>::success();};
  h.non_gpu=[&,app](const WorkItem&w,UnifiedBackend&,vxnn::CpuPool*p,ExecutionReceipt&r){require(w.app==app&&p&&(!pool_identity||pool_identity==p),"one actual shared CPU pool");pool_identity=p;++cpu;if(mode=="fault")return Result<void>::failure({EIO,"NATIVE_FIXTURE","original transfer failure"});PublishedResult out;out.execution_complete=true;out.lease.producer=w.request;out.lease.app=app;out.lease.hardware=hw.hardware_contract;out.lease.model=digest(static_cast<uint8_t>(app));out.lease.plan=digest(2);out.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ResultArtifact a;a.path="protocol-marker.bin";a.layout="NCHW";a.shape={1};a.bytes=4;a.sha=digest(7);out.lease.artifacts={a};r.output=out;r.outcome.status=RequestStatus::COMPLETED;r.outcome.retired=true;return Result<void>::success();};
  h.retire_request=[&](RequestKey,UnifiedBackend&b){require(&b==backend.get(),"same retirement backend");++retired;return Result<void>::success();};return h;
 };
 auto source=[&](const char*id,AppKind app,uint64_t cookie){auto r=request(id,2);r.app=app;r.model=digest(static_cast<uint8_t>(app));r.cpu_bytes=4096;r.resident_bytes=1024;auto s=std::make_shared<Logical>();s->cookie=cookie;s->epoch=hw.device_generation;s->sha=digest(40+cookie);r.source=s;return r;};
 auto a=source("nn",AppKind::NN,1),b=source("image",AppKind::IMAGE,2);auto acharge=value(rt->reserve_resident(a.model,a.resident_bytes),"first common resident"),bcharge=value(rt->reserve_resident(b.model,b.resident_bytes,AppKind::IMAGE),"second common resident");
 auto lifetime=std::make_shared<int>(7);auto bound_a=value(router->bind_source(a,hooks(a.app),acharge,lifetime),"bind first logical source");auto bound_b=value(router->bind_source(b,hooks(b.app),bcharge,lifetime),"bind second logical source");
 require(router->indexed()==2&&bound_a.source->identity()==a.source->identity()&&bound_b.source->size()==2&&opens==1&&backend_bridge_starts(io->bridge)==0,"binding retains original lazy source identity and makes no GPU call");
 auto wrong=a;wrong.request.request_id="p8";wrong.app=AppKind::P8;auto denied=router->bind_source(wrong,hooks(AppKind::P8),acharge,lifetime);require(!denied&&denied.error->code==EOPNOTSUPP&&router->indexed()==2&&prepared==0,"wrong physical provider rejected before GPU callback");
 auto duplicate=router->bind_source(a,hooks(a.app),acharge,lifetime);require(!duplicate&&duplicate.error->code==EEXIST,"bound request identity cannot be replaced");auto mismatched=a;mismatched.request.request_id="bad-charge";require(!router->bind_source(mismatched,hooks(a.app),bcharge,lifetime),"another model charge cannot authorize this source");
 {auto gpu_only=a;gpu_only.request.request_id="gpu-only-callback";auto h=hooks(a.app);h.non_gpu={};auto bound=value(router->bind_source(gpu_only,std::move(h),acharge,lifetime),"all-GPU sources do not require a synthetic non-GPU callback");require(bound.source->get(0)&&!bound.source->get(1),"GPU-only source admits original GPU and rejects any unsupported non-GPU item before activation");}require(router->indexed()==2,"unadmitted GPU-only test source drops exact backing");
 require(!bound_a.source->get(2)&&!bound_b.source->ready(2),"private source index is bounded before original callbacks");auto original=std::dynamic_pointer_cast<const Logical>(a.source);auto mutable_source=std::const_pointer_cast<Logical>(original);auto saved_sha=mutable_source->sha;mutable_source->sha[0]^=1;require(!bound_a.source->get(0)&&!bound_a.source->ready(0),"source mutation refused before GPU");mutable_source->sha=saved_sha;
 if(mode=="allocation"){
  unsigned refused=0;
  for(int point=0;point<40;++point){auto candidate=a;candidate.request.request_id="alloc-"+std::to_string(point);auto h=hooks(a.app);std::optional<Result<WorkRequest>>attempt;bool escaped=false;fail_after=point;try{attempt.emplace(router->bind_source(std::move(candidate),std::move(h),acharge,lifetime));}catch(const std::bad_alloc&){escaped=true;}fail_after=-1;require(!escaped&&attempt.has_value(),"router allocation must return explicit failure");if(!*attempt){++refused;require(attempt->error->code==ENOMEM,"native allocation reason");}attempt.reset();require(router->indexed()==2&&prepared==0&&backend_bridge_starts(io->bridge)==0,"allocation rollback keeps exact existing bindings and no device side effect");}
  require(refused>0,"router allocation refusal exercised");std::printf("U20_NATIVE_ROUTER_40_ALLOCATION_POINTS_PASS_NOT_APPS refused=%u gpu_raw_cycles=null\n",refused);
 }
 if(mode=="concurrency"){
  std::vector<std::thread>workers;for(unsigned t=0;t<8;++t)workers.emplace_back([&,t]{for(unsigned n=0;n<40;++n){auto candidate=a;candidate.request.request_id="parallel-"+std::to_string(t)+"-"+std::to_string(n);auto bound=value(router->bind_source(std::move(candidate),hooks(a.app),acharge,lifetime),"parallel native metadata bind");require(bound.source->get(0)&&bound.source->ready(0),"concurrent metadata retains exact source");}});for(auto&t:workers)t.join();
  require(router->indexed()==2&&prepared==0&&backend_bridge_starts(io->bridge)==0,"320 metadata requests leave no stale owned source or GPU call");
  std::vector<WorkRequest>held;for(unsigned n=0;n<254;++n){auto candidate=a;candidate.request.request_id="capacity-"+std::to_string(n);held.push_back(value(router->bind_source(std::move(candidate),hooks(a.app),acharge,lifetime),"bounded metadata index"));}auto candidate=a;candidate.request.request_id="over-capacity";auto full=router->bind_source(candidate,hooks(a.app),acharge,lifetime);require(!full&&full.error->code==ENOSPC&&router->indexed()==256,"257th indexed binding refused without replacing old source");held.clear();require(router->indexed()==2,"unadmitted source destruction rolls back weak registry");
  puts("U20_NATIVE_ROUTER_320_CONCURRENT_BINDINGS_AND_256_BOUND_PASS_NOT_APPS gpu_raw_cycles=null");
 }
 value(rt->submit(bound_a),"same global queue first source");value(rt->submit(bound_b),"same global queue second source");require(rt->stats().resident_models==2&&rt->stats().cpu_bytes==(128ULL<<20)+2048+8192,"one global resident and active-request accounting");
 if(mode=="fault"){
  bool failed=false;for(unsigned i=0;i<4&&!failed;++i){auto r=rt->run_one();if(!r){failed=true;require(r.error->code==EIO&&r.error->detail=="original transfer failure","exact routed first error");}}
  require(failed&&retired==0&&!rt->run_one()&&!rt->shutdown(),"routed execution failure preserves all active backing, never retries");puts("U20_NATIVE_ROUTER_FIRST_ERROR_PIN_PASS_HOST_C_CONTROL_NOT_APPS gpu_raw_cycles=null");return 0;
 }
 for(unsigned i=0;i<4;++i){auto result=rt->run_one();if(!result)std::fprintf(stderr,"NATIVE_ROUTER_STEP index=%u code=%d category=%s detail=%s prepared=%u finished=%u cpu=%u\n",i,result.error->code,result.error->category.c_str(),result.error->detail.c_str(),prepared,finished,cpu);ok(result,"actual single C owner control route");}
 require(prepared==2&&finished==2&&cpu==2&&backend_bridge_starts(io->bridge)==2&&io->acks==2,"two synthetic sources share one actual C submit/wait/ACK port");
 auto pa=value(rt->take_result(a.request),"first original result"),pb=value(rt->take_result(b.request),"second original result");require(pa.binding&&pb.binding&&pa.binding->source==a.source->identity()&&pb.binding->source==b.source->identity()&&!pa.gpu_raw_cycles&&!pb.gpu_raw_cycles,"full source lineage and no physical GPU counters");
 ok(rt->reap_result(a.request),"first backing retirement");ok(rt->reap_result(b.request),"second backing retirement");require(retired==2&&router->indexed()==0,"exact handler lifetime retired once");
 bound_a.source.reset();bound_b.source.reset();acharge.reset();bcharge.reset();require(rt->stats().resident_models==0,"last private source and session lifetime release common charge");
 ok(rt->shutdown(),"router queue and CPU pool drained");ok(backend->release_buffer(args),"global fixture args release");ok(backend->release_program(program),"global fixture CODE release");
 puts("U20_NATIVE_ROUTER_ONE_OWNER_TWO_SYNTHETIC_SOURCES_PASS_HOST_C_CONTROL_NOT_APPS gpu_raw_cycles=null");return 0;
}

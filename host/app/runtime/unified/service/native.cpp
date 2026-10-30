#include <vxunified/native.hpp>
#include <atomic>
#include <map>
#include <mutex>
#include <tuple>
namespace vxunified {
namespace {
using Key=std::tuple<std::string,std::string,std::string>;
Key key(const RequestKey&k){return {k.boot_id,k.client_id,k.request_id};}
bool same_hw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags;}
bool permitted(uint32_t provider,AppKind app){return (provider==4&&(app==AppKind::NN||app==AppKind::P8||app==AppKind::IMAGE))||(provider==1&&(app==AppKind::NN||app==AppKind::IMAGE))||(provider==2&&app==AppKind::P8);}
struct BoundNativeSource final:WorkSource {
 std::shared_ptr<const WorkSource>source;ExecutionHooks hooks;std::shared_ptr<ResidentLease>charge;std::shared_ptr<const void>session;
 RequestKey request;AppKind app=AppKind::NN;Digest sha{};uint64_t count=0,epoch=0;
 std::atomic<bool>retired{false},retire_busy{false};
 uint64_t size()const override{return count;}Digest identity()const override{return sha;}
 Result<WorkItem>get(uint64_t n)const override{
  if(n>=count||retired.load()||source->identity()!=sha)return Result<WorkItem>::failure({ESTALE,"NATIVE_SOURCE","source index/identity/lifetime differs"});
  auto got=source->get(n);if(!got)return got;auto w=*got.value;
  if(w.kind==WorkKind::LEGACY_EXCLUSIVE)return Result<WorkItem>::failure({EOPNOTSUPP,"NATIVE_SOURCE","legacy aggregate cannot enter native v2 routing"});
  if(w.kind!=WorkKind::GPU&&!hooks.non_gpu)return Result<WorkItem>::failure({EOPNOTSUPP,"NATIVE_SOURCE","non-GPU item has no trusted native callback"});
  if(w.kind==WorkKind::GPU&&(!std::holds_alternative<DeferredGpuRef>(w.operation)||gpu_work_epoch(w)!=epoch||!gpu_work_cookie(w)))return Result<WorkItem>::failure({ESTALE,"NATIVE_SOURCE","admitted deferred program/epoch/cookie required"});
  return Result<WorkItem>::success(std::move(w));
 }
 Result<bool>ready(uint64_t n)const override{
  if(n>=count||retired.load()||source->identity()!=sha)return Result<bool>::failure({ESTALE,"NATIVE_SOURCE","source ready identity/lifetime differs"});
  return source->ready(n);
 }
};
}
struct NativeRouter::State {
 std::weak_ptr<Runtime>runtime;std::shared_ptr<UnifiedBackend>backend;BackendIdentity identity;int fd=-1;std::mutex mutex;std::map<Key,std::weak_ptr<BoundNativeSource>>entries;std::atomic<uint64_t>cookie_namespace{1};
 void collect(){for(auto it=entries.begin();it!=entries.end();){if(it->second.expired())it=entries.erase(it);else ++it;}}
 Result<std::shared_ptr<BoundNativeSource>>lookup(const RequestKey&k,UnifiedBackend&b){
  auto rt=runtime.lock();if(!rt)return Result<std::shared_ptr<BoundNativeSource>>::failure({ESHUTDOWN,"NATIVE_ROUTER","common Runtime no longer alive"});
  if(&b!=backend.get()||!rt->owns_native_backend(fd,&b))return Result<std::shared_ptr<BoundNativeSource>>::failure({EXDEV,"NATIVE_ROUTER","borrowed common owner backend differs"});
  std::lock_guard<std::mutex>lock(mutex);auto it=entries.find(key(k));auto source=it==entries.end()?std::shared_ptr<BoundNativeSource>{}:it->second.lock();
  if(!source||source->retired.load())return Result<std::shared_ptr<BoundNativeSource>>::failure({ESTALE,"NATIVE_ROUTER","native request not retained or already retired"});
  return Result<std::shared_ptr<BoundNativeSource>>::success(std::move(source));
 }
 Result<std::shared_ptr<BoundNativeSource>>lookup(const WorkItem&w,UnifiedBackend&b){auto found=lookup(w.request,b);if(!found)return found;if(w.app!=found.value->get()->app||w.work_index>=found.value->get()->count)return Result<std::shared_ptr<BoundNativeSource>>::failure({ESTALE,"NATIVE_ROUTER","admitted native work application/index differs"});return found;}
};
NativeRouter::NativeRouter(std::shared_ptr<State>s):state_(std::move(s)){}
Result<std::shared_ptr<NativeRouter>>NativeRouter::create(const StartupContext&c){
 try{
  auto common=admit_native_startup(c,c.identity.provider_flags);if(!common)return Result<std::shared_ptr<NativeRouter>>::failure(*common.error);
  auto rt=c.runtime.lock();if(c.mode!=BackendMode::COMPUTE_V2||!rt||!c.compute_backend)return Result<std::shared_ptr<NativeRouter>>::failure({EOPNOTSUPP,"NATIVE_ROUTER","live explicit compute-v2 service roots required"});
  if(c.results.lock()!=rt->trusted_results()||c.scope!=rt->evidence_scope()||!same_hw(c.identity,rt->identity())||!rt->owns_native_backend(c.borrowed_fd,c.compute_backend.get())||(c.scope==vxnn::EvidenceScope::REAL_BOARD&&c.journal.expired()))return Result<std::shared_ptr<NativeRouter>>::failure({EXDEV,"NATIVE_ROUTER","actual common owner/result/scope/identity roots differ"});
  auto s=std::make_shared<State>();s->runtime=c.runtime;s->backend=c.compute_backend;s->identity=c.identity;s->fd=c.borrowed_fd;return Result<std::shared_ptr<NativeRouter>>::success(std::shared_ptr<NativeRouter>(new NativeRouter(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<NativeRouter>>::failure({ENOMEM,"NATIVE_ROUTER","native source registry allocation"});}
}
Result<WorkRequest>NativeRouter::bind_source(WorkRequest r,ExecutionHooks hooks,std::shared_ptr<ResidentLease>charge,std::shared_ptr<const void>session){
 try{
  auto&s=*state_;auto rt=s.runtime.lock();if(!rt)return Result<WorkRequest>::failure({ESHUTDOWN,"NATIVE_ROUTER","common Runtime no longer alive"});
  if(!permitted(s.identity.provider_flags,r.app))return Result<WorkRequest>::failure({EOPNOTSUPP,"NATIVE_ROUTER","native application belongs to another physical provider"});
  if(rt->stats().phase!=RuntimePhase::READY)return Result<WorkRequest>::failure({EBUSY,"NATIVE_ROUTER","common admission closed"});
  if(!charge||!rt->owns_resident(charge)||charge->model()!=r.model||charge->bytes()!=r.resident_bytes||charge->app()!=r.app)return Result<WorkRequest>::failure({ESTALE,"NATIVE_ROUTER","exact common model lifetime and application role reservation required"});
  if(!r.source||!session||!hooks.prepare_gpu||!hooks.finish_gpu||!hooks.retire_request||!nonzero_digest(r.model)||!nonzero_digest(r.plan)||!nonzero_digest(r.input_sha))return Result<WorkRequest>::failure({EINVAL,"NATIVE_ROUTER","trusted lazy source/session and GPU/retirement callbacks required"});
  auto count=r.source->size();auto sha=r.source->identity();if(!count||count>0xfffffffeu||!nonzero_digest(sha))return Result<WorkRequest>::failure({EINVAL,"NATIVE_ROUTER","bounded nonempty source identity required"});
  auto source=std::make_shared<BoundNativeSource>();source->source=r.source;source->hooks=std::move(hooks);source->charge=std::move(charge);source->session=std::move(session);source->request=r.request;source->app=r.app;source->sha=sha;source->count=count;source->epoch=s.identity.device_generation;
  r.source=source;
  std::lock_guard<std::mutex>lock(s.mutex);s.collect();auto k=key(r.request);if(s.entries.count(k))return Result<WorkRequest>::failure({EEXIST,"NATIVE_ROUTER","native request identity already bound"});
  if(s.entries.size()>=256)return Result<WorkRequest>::failure({ENOSPC,"NATIVE_ROUTER","common native request index256"});
  s.entries.emplace(std::move(k),source);return Result<WorkRequest>::success(std::move(r));
 }catch(const std::bad_alloc&){return Result<WorkRequest>::failure({ENOMEM,"NATIVE_ROUTER","native source binding allocation"});}
}
ExecutionHooks NativeRouter::handlers(){
 auto s=state_;ExecutionHooks h;
 // Lookup retains the exact source, then releases the registry mutex before
 // app callbacks. Display/result retirement must never invert that lock order.
 h.prepare_gpu=[s](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto e=s->lookup(w,b);return e?(*e.value)->hooks.prepare_gpu(w,b,r):Result<GPUJobDesc>::failure(*e.error);};
 h.finish_gpu=[s](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto e=s->lookup(w,b);return e?(*e.value)->hooks.finish_gpu(w,b,r):Result<void>::failure(*e.error);};
 h.non_gpu=[s](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){auto e=s->lookup(w,b);if(!e)return Result<void>::failure(*e.error);auto&hook=(*e.value)->hooks.non_gpu;if(!hook)return Result<void>::failure({EOPNOTSUPP,"NATIVE_ROUTER","non-GPU callback is not installed"});return hook(w,b,p,r);};
 h.retire_request=[s](RequestKey k,UnifiedBackend&b){
  auto e=s->lookup(k,b);if(!e)return Result<void>::failure(*e.error);auto source=*e.value;bool expected=false;if(!source->retire_busy.compare_exchange_strong(expected,true))return Result<void>::failure({EBUSY,"NATIVE_ROUTER","native request retirement already active"});
  auto done=source->hooks.retire_request(k,b);if(!done){source->retire_busy=false;return done;}source->retired=true;
  std::lock_guard<std::mutex>lock(s->mutex);auto it=s->entries.find(key(k));if(it==s->entries.end()||it->second.lock()!=source)return Result<void>::failure({ESTALE,"NATIVE_ROUTER","retired source identity no longer matches registry"});s->entries.erase(it);return Result<void>::success();
 };
 return h;
}
size_t NativeRouter::indexed()const{std::lock_guard<std::mutex>lock(state_->mutex);state_->collect();return state_->entries.size();}
bool NativeRouter::owns_runtime(const Runtime*r)const{return r&&state_->runtime.lock().get()==r;}
Result<uint32_t>NativeRouter::allocate_cookie_namespace(){auto&s=*state_;auto n=s.cookie_namespace.load();for(;;){if(n>UINT32_MAX)return Result<uint32_t>::failure({EOVERFLOW,"NATIVE_ROUTER","native cookie namespace exhausted; never wrap/reuse"});if(s.cookie_namespace.compare_exchange_weak(n,n+1))return Result<uint32_t>::success(uint32_t(n));}}
}

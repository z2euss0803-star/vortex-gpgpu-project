#include <vxunified/native_media.hpp>
#include <atomic>
#include <mutex>
#include <sstream>
namespace vxunified {namespace {
Result<void>roots(const StartupContext&c,uint32_t provider,const std::shared_ptr<NativeRouter>&router,const std::shared_ptr<ProgramResidency>&residency,const std::shared_ptr<NativeFiles>&files,const std::shared_ptr<NativeDisplay>&display){
 auto admitted=admit_native_startup(c,provider);if(!admitted)return admitted;auto rt=c.runtime.lock();
 if(!router||!router->owns_runtime(rt.get())||!residency||residency->borrowed_backend()!=c.compute_backend||!native_identity_equal(residency->identity(),c.identity)||!files||!files->owns_runtime(rt.get())||!display||!display->owns_runtime(rt.get()))return Result<void>::failure({EXDEV,"NATIVE_MEDIA","sole common router/residency/files/display roots required"});
 return Result<void>::success();
}
bool same_roots(const StartupContext&a,const StartupContext&b){return a.runtime.lock()==b.runtime.lock()&&a.compute_backend==b.compute_backend&&a.borrowed_fd==b.borrowed_fd&&native_identity_equal(a.identity,b.identity)&&a.scope==b.scope&&a.boot_id==b.boot_id&&a.private_dir==b.private_dir;}
std::string hex(const void*data,size_t n){const auto*p=static_cast<const uint8_t*>(data);const char*table="0123456789abcdef";std::string out;out.reserve(n*2);for(size_t i=0;i<n;++i){out+=table[p[i]>>4];out+=table[p[i]&15];}return out;}
PublishedResult published(const StartupContext&c,const WorkRequest&w,std::vector<ResultArtifact>artifacts,std::optional<uint64_t>cycles,std::string raw){PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=w.app;p.lease.hardware=c.identity.hardware_contract;p.lease.model=w.model;p.lease.plan=w.plan;p.lease.scope=c.scope;p.lease.artifacts=std::move(artifacts);p.gpu_raw_cycles=cycles;p.raw_legacy_json=std::move(raw);return p;}
std::string p8_row(const ExecutionReceipt&e,const P8OwnerReceipt&r){
 std::ostringstream s;s<<"{\"format\":\"native-p8-job-v1\",\"scope\":\""<<(e.scope==vxnn::EvidenceScope::REAL_BOARD?"REAL_BOARD":"HOST_PROTOCOL_TEST")<<"\",\"node\":"<<e.outcome.node<<",\"allocate_ns\":"<<r.allocate_ns<<",\"program_ns\":"<<r.program_ns<<",\"upload_ns\":"<<r.upload_ns<<",\"args_ns\":"<<r.args_ns<<",\"guard_ns\":"<<r.guard_ns<<",\"download_ns\":"<<r.download_ns<<",\"bins_ns\":"<<r.bins_ns<<",\"publish_ns\":"<<r.publish_ns<<",\"release_ns\":"<<r.release_ns<<",\"upload_bytes_scope\":\"P8_DIRECT_API_EXCLUDES_BACKEND_ARGS_STAGING\",\"upload_bytes\":"<<r.upload_bytes<<",\"args_upload_bytes\":"<<r.args_upload_bytes<<",\"static_reused_bytes\":"<<r.static_reused_bytes<<",\"cache_fast\":"<<(r.cache_fast?"true":"false")<<",\"cache_root_reused\":"<<(r.cache_root_reused?"true":"false")<<",\"cache_static_reused\":"<<(r.cache_static_reused?"true":"false")<<",\"gpu_raw_cycles\":";if(r.gpu_raw_cycles)s<<*r.gpu_raw_cycles;else s<<"null";s<<",\"original_linux_completion_hex\":";if(e.linux_detail)s<<'"'<<hex(&*e.linux_detail,sizeof(*e.linux_detail))<<'"';else s<<"null";s<<"}\n";return s.str();
}
}
struct NativeImageLibrary::State:std::enable_shared_from_this<NativeImageLibrary::State> {
 StartupContext ctx;std::shared_ptr<NativeRouter>router;std::shared_ptr<ProgramResidency>residency;std::shared_ptr<NativeFiles>files;std::shared_ptr<NativeDisplay>display;NativeImageSpec spec;std::shared_ptr<ResidentLease>charge;std::shared_ptr<ImageSession>session;
 std::atomic<bool>rollback_failed{false};std::shared_ptr<ImageWorkRequest>pinned;
 struct Rollback {std::shared_ptr<State>owner;std::shared_ptr<ImageWorkRequest>app;~Rollback(){if(!app)return;bool safe=false;try{safe=bool(NativeImageLibrary::discard(app));}catch(...){}if(!safe&&!owner->rollback_failed.exchange(true))owner->pinned=app;}};
};
NativeImageLibrary::NativeImageLibrary(std::shared_ptr<State>s):state_(std::move(s)){}
bool NativeImageLibrary::owns_runtime(const Runtime*r)const{return state_->ctx.runtime.lock().get()==r;}
Result<void>NativeImageLibrary::discard(const std::shared_ptr<ImageWorkRequest>&app){return app->discard_unsubmitted();}
Result<std::shared_ptr<NativeImageLibrary>>NativeImageLibrary::create(const StartupContext&c,std::shared_ptr<NativeRouter>router,std::shared_ptr<ProgramResidency>residency,std::shared_ptr<NativeFiles>files,std::shared_ptr<NativeDisplay>display,const NativeImageSpec&spec){
 using R=Result<std::shared_ptr<NativeImageLibrary>>;try{
  auto bound=roots(c,1,router,residency,files,display);if(!bound)return R::failure(*bound.error);
  if(!nonzero_digest(spec.implementation)||!nonzero_digest(spec.reference)||!spec.readonly_table||spec.readonly_table->size()!=65536||!spec.job_budget_ns||spec.job_budget_ns>60000000000ULL)return R::failure({EINVAL,"NATIVE_IMAGE","sealed implementation/reference/table and bounded budget required"});
  static std::mutex mutex;static std::weak_ptr<State>current;std::lock_guard<std::mutex>lock(mutex);auto old=current.lock();
  if(old){if(!same_roots(c,old->ctx)||router!=old->router||residency!=old->residency||files!=old->files||display!=old->display||spec.implementation!=old->spec.implementation||spec.reference!=old->spec.reference||spec.job_budget_ns!=old->spec.job_budget_ns||*spec.readonly_table!=*old->spec.readonly_table)return R::failure({EBUSY,"NATIVE_IMAGE","one exact native cache per process; previous lifetime retained"});return R::success(std::shared_ptr<NativeImageLibrary>(new NativeImageLibrary(old)));}
  auto rt=c.runtime.lock();auto charge=rt->reserve_resident(spec.implementation,resident_bytes,AppKind::IMAGE);if(!charge)return R::failure(*charge.error);
  auto session=ImageSession::create_native(c,residency->catalog(),residency,*charge.value,spec.implementation,spec.reference,{spec.readonly_table->data(),spec.readonly_table->size()});if(!session)return R::failure(*session.error);
  auto s=std::make_shared<State>();s->ctx=c;s->router=std::move(router);s->residency=std::move(residency);s->files=std::move(files);s->display=std::move(display);s->spec=spec;s->charge=*charge.value;s->session=*session.value;auto result=std::shared_ptr<NativeImageLibrary>(new NativeImageLibrary(s));current=s;return R::success(std::move(result));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_IMAGE","native cache allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_IMAGE",e.what()});}
}
Result<NativeImagePrepared>NativeImageLibrary::prepare(ImageRequest input){
 using R=Result<NativeImagePrepared>;try{
  auto s=state_;auto rt=s->ctx.runtime.lock();if(!rt||rt->stats().phase!=RuntimePhase::READY||s->rollback_failed)return R::failure({EBUSY,"NATIVE_IMAGE","common owner not ready or unsubmitted rollback unproven"});
  if(input.width!=1280||input.height!=720||!input.rgba||input.rgba->size()!=1280u*720u*4u||!input.display_cookie||input.request.boot_id!=s->ctx.boot_id)return R::failure({EINVAL,"NATIVE_IMAGE","native720 immutable input/display/boot contract required"});
  if(!input.cookie_namespace){auto ns=s->router->allocate_cookie_namespace();if(!ns)return R::failure(*ns.error);input.cookie_namespace=*ns.value;}
  auto quota=s->files->request(input.request,65536,1280u*720u*4u);if(!quota)return R::failure(*quota.error);auto trace=*quota.value;auto lifetime=std::make_shared<State::Rollback>();lifetime->owner=s;
  auto app=s->session->prepare(input,s->spec.job_budget_ns,[trace](const RequestKey&,const ImageJobTrace&t){auto raw=encode_image_job_trace(t);return raw?trace->append_trace({raw.value->data(),raw.value->size()},t.error!=0):Result<void>::failure(*raw.error);},[s,trace,cookie=input.display_cookie](const WorkRequest&w,vxnn::Bytes bytes,const ImageOwnerReceipt&r)->Result<ImagePresentation>{
   auto saved=trace->output("output.rgba",bytes,"RGBA8888",{720,1280,4});if(!saved)return Result<ImagePresentation>::failure(*saved.error);auto text="{\"format\":\"native-image-result-v1\",\"completed_jobs\":"+std::to_string(r.completed_jobs)+",\"numeric\":\"NOT_EVALUATED\"}";auto p=published(s->ctx,w,{*saved.value},r.gpu_raw_cycles,text);auto caps=s->display->capabilities();auto layout=raster_page_layout(p.lease,0,caps.width,caps.height);if(!layout)return Result<ImagePresentation>::failure(*layout.error);auto shown=s->display->stage_and_present(p,*layout.value,cookie);if(!shown)return Result<ImagePresentation>::failure(*shown.error);return Result<ImagePresentation>::success({*shown.value,std::move(p),false});
  });if(!app)return R::failure(*app.error);lifetime->app=*app.value;
  auto work=s->router->bind_source((*app.value)->request(),(*app.value)->handlers(),s->charge,lifetime);if(!work)return R::failure(*work.error);return R::success({*work.value,*app.value});
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_IMAGE","native request allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_IMAGE",e.what()});}
}
struct NativeP8Library::State {StartupContext ctx;std::shared_ptr<NativeRouter>router;std::shared_ptr<ProgramResidency>residency;std::shared_ptr<NativeFiles>files;std::shared_ptr<NativeDisplay>display;NativeP8Spec spec;std::shared_ptr<ResidentLease>charge;std::shared_ptr<const P8Assets>assets;std::shared_ptr<P8FrameSession>session;};
NativeP8Library::NativeP8Library(std::shared_ptr<State>s):state_(std::move(s)){}
bool NativeP8Library::owns_runtime(const Runtime*r)const{return r&&state_->ctx.runtime.lock().get()==r;}
Result<std::shared_ptr<NativeP8Library>>NativeP8Library::create(const StartupContext&c,std::shared_ptr<NativeRouter>router,std::shared_ptr<ProgramResidency>residency,std::shared_ptr<NativeFiles>files,std::shared_ptr<NativeDisplay>display,const NativeP8Spec&spec){
 using R=Result<std::shared_ptr<NativeP8Library>>;try{
  auto bound=roots(c,2,router,residency,files,display);if(!bound)return R::failure(*bound.error);if(spec.assets_directory.empty()||!nonzero_digest(spec.assets)||!spec.job_budget_ns||spec.job_budget_ns>60000000000ULL)return R::failure({EINVAL,"NATIVE_P8","sealed P8 assets and bounded budget required"});
  static std::mutex mutex;static std::weak_ptr<State>current;std::lock_guard<std::mutex>lock(mutex);auto old=current.lock();if(old){if(!same_roots(c,old->ctx)||router!=old->router||residency!=old->residency||files!=old->files||display!=old->display||spec.assets_directory!=old->spec.assets_directory||spec.assets!=old->spec.assets||spec.job_budget_ns!=old->spec.job_budget_ns)return R::failure({EBUSY,"NATIVE_P8","one exact frozen asset cache per process"});return R::success(std::shared_ptr<NativeP8Library>(new NativeP8Library(old)));}
  auto rt=c.runtime.lock();auto charge=rt->reserve_resident(spec.assets,resident_bytes,AppKind::P8);if(!charge)return R::failure(*charge.error);auto assets=P8Assets::load(spec.assets_directory);if(!assets)return R::failure(*assets.error);if((*assets.value)->identity()!=spec.assets)return R::failure({ESTALE,"NATIVE_P8","actual immutable P8 bank differs sealed identity"});
  auto adapter=P8Adapter::create_native(c,residency->catalog(),*assets.value,*charge.value);if(!adapter)return R::failure(*adapter.error);
  auto s=std::make_shared<State>();s->ctx=c;s->router=std::move(router);s->residency=std::move(residency);s->files=std::move(files);s->display=std::move(display);s->spec=spec;s->charge=*charge.value;s->assets=*assets.value;if(c.fast_runtime&&c.compute_backend->fast_runtime())s->session=std::make_shared<P8FrameSession>(c.compute_backend,c.identity);auto result=std::shared_ptr<NativeP8Library>(new NativeP8Library(s));current=s;return R::success(std::move(result));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_P8","native asset cache allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_P8",e.what()});}
}
Result<void>NativeP8Library::release_session(){return state_->session?state_->session->close():Result<void>::success();}
Result<void>NativeP8Library::release_inactive_session(){
 auto s=state_;if(!s->session)return Result<void>::success();auto rt=s->ctx.runtime.lock();
 if(!rt)return Result<void>::failure({ESHUTDOWN,"NATIVE_P8","common Runtime expired; cached root retained"});
 // Runtime's locks are released before close() calls the backend. A pending
 // switch still in P8, or a fault, never supplies an active-root free proof.
 const auto mode=rt->mode_snapshot();if(!s8_joint_platform(s->ctx.identity)||mode.current==AppKind::P8)return Result<void>::success();
 if(rt->stats().phase!=RuntimePhase::READY)return Result<void>::failure({EBUSY,"NATIVE_P8","fault or delivery state retains cached root"});
 return s->session->close();
}
Result<NativeP8Prepared>NativeP8Library::prepare(P8Request input){
 using R=Result<NativeP8Prepared>;try{
  auto s=state_;auto rt=s->ctx.runtime.lock();if(!rt||rt->stats().phase!=RuntimePhase::READY)return R::failure({EBUSY,"NATIVE_P8","common Runtime not ready"});if(input.request.boot_id!=s->ctx.boot_id||!input.display_cookie)return R::failure({EINVAL,"NATIVE_P8","exact native boot/display request required"});
  if(!input.cookie_namespace){auto ns=s->router->allocate_cookie_namespace();if(!ns)return R::failure(*ns.error);input.cookie_namespace=*ns.value;}if(input.cookie_namespace>0x7fffffffu)return R::failure({EOVERFLOW,"NATIVE_P8","original cookie range exhausted; never wrap"});
  auto frame=rt->reserve_host_memory(frame_lifetime_bytes);if(!frame)return R::failure(*frame.error);auto quota=s->files->request(input.request,65536,4ULL<<20);if(!quota)return R::failure(*quota.error);auto trace=*quota.value;
  auto app=P8WorkRequest::create_native(s->ctx,s->residency->catalog(),s->assets,s->residency,input,s->spec.job_budget_ns,[trace](const RequestKey&,const ExecutionReceipt&e,const P8OwnerReceipt&r){auto text=p8_row(e,r);return trace->append_trace({reinterpret_cast<const uint8_t*>(text.data()),text.size()},true);},[s,trace,cookie=input.display_cookie](const WorkRequest&w,vxnn::Bytes color,vxnn::Bytes depth,const P8OwnerReceipt&r)->Result<P8Presentation>{
   uint64_t side=color.size==uint64_t(256)*256*4?256:color.size==uint64_t(512)*512*4?512:0;if(!side||depth.size!=color.size)return Result<P8Presentation>::failure({EINVAL,"NATIVE_P8","original native256/512 color/depth extent required"});auto c=trace->output("output.rgba",color,"RGBA8888",{side,side,4});if(!c)return Result<P8Presentation>::failure(*c.error);auto d=trace->output("depth.u32",depth,"P8_DEPTH32",{side,side});if(!d)return Result<P8Presentation>::failure(*d.error);auto text="{\"format\":\"native-p8-result-v1\",\"rgba_crc32\":"+std::to_string(r.rgba_crc)+",\"depth_crc32\":"+std::to_string(r.depth_crc)+",\"numeric\":\"NOT_EVALUATED\"}";auto p=published(s->ctx,w,{*c.value,*d.value},r.gpu_raw_cycles,text);auto caps=s->display->capabilities();auto layout=raster_page_layout(p.lease,0,caps.width,caps.height);if(!layout)return Result<P8Presentation>::failure(*layout.error);auto shown=s->display->stage_and_present(p,*layout.value,cookie);if(!shown)return Result<P8Presentation>::failure(*shown.error);return Result<P8Presentation>::success({std::move(p),*shown.value});
  },s->charge,*frame.value,s->session);if(!app)return R::failure(*app.error);auto work=s->router->bind_source((*app.value)->request(),(*app.value)->handlers(),s->charge,s);if(!work)return R::failure(*work.error);return R::success({*work.value,*app.value});
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_P8","native frame allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_P8",e.what()});}
}
}

#include <vxunified/native_media.hpp>
#include <vxunified/texture.hpp>
#include <uapi/vx_compute_v2.h>
#include <cstring>
#include <map>
#include <vxunified/journal.hpp>
#include <vxl/hunet.h>
#include "scheduler_fixture.hpp"
#include "image_owner_bridge.h"
#include "p8_owner_bridge.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <type_traits>
#include <thread>
#include <atomic>
#include <time.h>
namespace {
static bool cache_fast=false;
template<class T>T checked(vxnn::Result<T>r,const char*name){if(!r)std::fprintf(stderr,"NATIVE_MEDIA_ERROR %s code=%d category=%s detail=%s\n",name,r.error->code,r.error->category.c_str(),r.error->detail.c_str());return value(std::move(r),name);}
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen media file");return {std::istreambuf_iterator<char>(f),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Transport:IoctlTransport {
 void*bridge;uint64_t uploaded_bytes=0,direct_bytes=0,staging_bytes=0,data_allocations=0,staging_allocations=0;std::map<uint64_t,uint32_t>purposes;
 Transport(const char*c,const char*d,Digest h){
#ifdef VX_NATIVE_P8
 bridge=p8_owner_bridge_create(c,d,h.data());if(cache_fast)p8_owner_bridge_enable_fast(bridge);
#else
 bridge=image_owner_bridge_create(c,d,h.data());
#endif
 }
 ~Transport(){
#ifdef VX_NATIVE_P8
 p8_owner_bridge_destroy(bridge);
#else
 image_owner_bridge_destroy(bridge);
#endif
 }
 int call(unsigned long c,void*p)override{
#ifdef VX_NATIVE_P8
 auto*r=static_cast<vxl_compute_v2_request*>(p);uint32_t purpose=0;if(c==VXL_V2_BUFFER_CREATE)purpose=reinterpret_cast<const vxl_v2_create_desc*>(uintptr_t(r->user_ptr))->purpose;
 int rc=p8_owner_bridge_call(bridge,c,p);if(!rc&&c==VXL_V2_BUFFER_CREATE){purposes[r->handle]=purpose;if(purpose==2)++staging_allocations;if(purpose==3)++data_allocations;}
 if(!rc&&c==VXL_V2_BUFFER_COPY){auto*d=reinterpret_cast<const vxl_v2_copy_desc*>(uintptr_t(r->user_ptr));if(d->direction==1){uploaded_bytes+=d->bytes;const auto kind=purposes.at(d->destination.handle);if(kind==2)staging_bytes+=d->bytes;if(kind==3)direct_bytes+=d->bytes;}}return rc;
#else
 return image_owner_bridge_call(bridge,c,p);
#endif
 }
 unsigned starts()const{
#ifdef VX_NATIVE_P8
 return p8_owner_bridge_starts(bridge);
#else
 return image_owner_bridge_starts(bridge);
#endif
 }
 uint64_t now_ns()override{return ns();}
};
struct Screen {
 unsigned created=0,destroyed=0;uint32_t scan=0,pending=0;uint64_t cookie=0;
 static int open(void*,const dp_config*,dp_caps*c){*c={1280,720,1,1,1,0};return 0;}
 static int create(void*p,uint32_t w,uint32_t h,dp_buffer*b){auto&s=*static_cast<Screen*>(p);auto n=uint64_t(w)*h*4;auto*data=static_cast<uint8_t*>(std::calloc(1,size_t(n)));if(!data)return -ENOMEM;*b={++s.created,w,h,w*4,DP_ARGB8888,n,data};return 0;}
 static int initial(void*p,const dp_buffer*b){static_cast<Screen*>(p)->scan=b->id;return 0;}
 static int flip(void*p,const dp_buffer*b,uint64_t cookie){auto&s=*static_cast<Screen*>(p);s.pending=b->id;s.cookie=cookie;const char*folder=std::getenv("VX_P8_SCANOUT_DIR");if(folder){auto path=std::string(folder)+"/scanout-"+std::to_string(cookie)+".bgra";FILE*f=std::fopen(path.c_str(),"wb");require(f&&std::fwrite(b->map,1,b->bytes,f)==b->bytes&&!std::fclose(f),"capture actual fake scanout bytes");}return 0;}
 static int receive(void*p,int,uint64_t*c,uint64_t*t){auto&s=*static_cast<Screen*>(p);*c=s.cookie;*t=ns();s.scan=s.pending;s.pending=0;return 0;}
 static int disable(void*p){auto&s=*static_cast<Screen*>(p);s.scan=s.pending=0;return 0;}
 static void destroy(void*p,dp_buffer*b){auto&s=*static_cast<Screen*>(p);require(b->id!=s.scan&&b->id!=s.pending,"native scanout retirement");std::free(b->map);++s.destroyed;}
 static void close(void*){}
 dp_ops ops(){return {open,create,initial,flip,receive,disable,destroy,close,this};}
};
template<class T,class=void>struct DeclaredImage {
 static void run(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<ProgramResidency>,std::shared_ptr<ResidentLease>,vxnn::Bytes){require(false,"U20 declared native IMAGE session must bind actual common owner without inventing measured bit identity");}
};
template<class T>struct DeclaredImage<T,std::void_t<decltype(T::create_native(std::declval<const StartupContext&>(),std::declval<std::shared_ptr<const InstalledCatalog>>(),std::declval<std::shared_ptr<ProgramResidency>>(),std::declval<std::shared_ptr<ResidentLease>>(),Digest{},Digest{},vxnn::Bytes{}))>> {
 static void run(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<ProgramResidency>residency,std::shared_ptr<ResidentLease>charge,vxnn::Bytes table){auto made=T::create_native(c,catalog,residency,charge,digest(7),digest(8),table);require(bool(made),"declared IMAGE metadata session with real common roots");auto bad=c;bad.identity.board_identity_verified=true;require(!T::create_native(bad,catalog,residency,charge,digest(7),digest(8),table),"native IMAGE cannot promote measurement marker");}
};
template<class T,class=void>struct DeclaredP8 {
 static void run(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<const P8Assets>,std::shared_ptr<ResidentLease>){require(false,"U20 declared native P8 adapter must bind actual common owner without inventing measured bit identity");}
};
template<class T>struct DeclaredP8<T,std::void_t<decltype(T::create_native(std::declval<const StartupContext&>(),std::declval<std::shared_ptr<const InstalledCatalog>>(),std::declval<std::shared_ptr<const P8Assets>>(),std::declval<std::shared_ptr<ResidentLease>>()))>> {
 static void run(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<const P8Assets>assets,std::shared_ptr<ResidentLease>charge){auto made=T::create_native(c,catalog,assets,charge);require(bool(made),"declared P8 metadata adapter with real common roots");auto bad=c;bad.identity.board_identity_verified=true;require(!T::create_native(bad,catalog,assets,charge),"native P8 cannot promote measurement marker");}
};
}
int main(int argc,char**argv){
 require(argc==6,"catalog programdir table output mode");const std::string mode=argv[5];cache_fast=mode!="cache-strict";char root[4096];const char*ram=std::getenv("VX_P8_CACHE_RAM_ROOT");require(ram&&std::snprintf(root,sizeof(root),"%s/owner",ram)>0,"unique RAM sandbox");require(!mkdir(root,0700),"sole private owner");ServiceConfig config;config.private_dir=root;unsigned opens=0;
 auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));}),"single owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;ExecutionHooks hooks;
 hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
#ifdef VX_NATIVE_P8
 auto compatible=vxnn::content_digest("vortex,hw4-s8-joint-v1");
#else
 auto compatible=vxnn::content_digest("vortex,hw4-s8-demo-v1");
#endif
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"same borrowed fd");auto t=std::make_unique<Transport>(argv[1],argv[2],compatible);io=t.get();return t;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual host C caps");auto backend=value(port->adapter_backend(),"same backend");if(cache_fast){auto native=std::dynamic_pointer_cast<ComputeV2Backend>(backend);require(bool(native),"actual ComputeV2 backend");ok(native->set_fast_runtime(true),"explicit HOST C FAST capability");}auto raw=read(argv[1]);Digest sha{};vxl_hunet_hash(raw.data(),raw.size(),sha.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sha),"frozen catalog");auto residency=value(ProgramResidency::create(catalog,backend),"same residency");
 require(!mkdir(argv[4],0700),"private output root");char output[4096];require(realpath(argv[4],output),"normalized output root");const auto jp=std::string(output)+".journal";require(!mkdir(jp.c_str(),0700),"durable original evidence directory");auto journal=value(EvidenceJournal::open(jp),"common journal");
 const auto scope=mode=="declared"?vxnn::EvidenceScope::REAL_BOARD:vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto rt=value(Runtime::start(owner,hw,scope,port,[journal](const ExecutionReceipt&r,DeliveryStage s){return journal->save_work(r,s);},[journal](const PublishedResult&r){return journal->save_result(r);}),"common Runtime");StartupContext c;c.borrowed_fd=owner->fd();c.identity=hw;c.compute_backend=backend;c.runtime=rt;c.results=rt->trusted_results();c.journal=journal;c.scope=scope;c.private_dir=root;c.boot_id="boot";c.fast_runtime=cache_fast;router=value(NativeRouter::create(c),"common native router");auto files=value(NativeFiles::create(c,output),"same common output store");Screen screen;
 if(mode=="declared"){
  auto invalid_device=NativeDisplay::create_native(c,[](const PublishedResult&,const DisplayLayout&){return Result<std::shared_ptr<const DisplayCanvas>>::failure({EOPNOTSUPP,"TEST","no physical frame permitted"});},[](const DisplayObservation&){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();},"/tmp/not-a-drm-card");require(!invalid_device&&invalid_device.error->code==EINVAL,"native display reaches actual native path validation without faking measured bit identity or touching KMS");
#ifdef VX_NATIVE_P8
  auto reference=value(P8Assets::load("data/p8"),"frozen reference identity only");auto id=reference->identity();reference.reset();auto charge=value(rt->reserve_resident(id,NativeP8Library::resident_bytes,AppKind::P8),"charge before native asset load");auto assets=value(P8Assets::load("data/p8"),"charged frozen bank");DeclaredP8<P8Adapter>::run(c,catalog,assets,charge);assets.reset();charge.reset();
#else
  auto charge=value(rt->reserve_resident(digest(7),NativeImageLibrary::resident_bytes,AppKind::IMAGE),"charge before native shadow allocation");auto table=read(argv[3]);DeclaredImage<ImageSession>::run(c,catalog,residency,charge,{table.data(),table.size()});charge.reset();
#endif
  require(!hw.board_identity_verified&&!io->starts()&&!rt->stats().scheduler.requests&&!files->stats().retained_requests&&!screen.created,"declared constructor test executes zero GPU jobs and no KMS call or publication");ok(rt->shutdown(),"empty declared context close");ok(residency->shutdown(),"no native program loaded");puts("U20_DECLARED_MEDIA_CONTEXT_ZERO_WORK_NO_DISPLAY_PASS_HOST_CONSTRUCTOR_NOT_BOARD gpu_raw_cycles=null");return 0;
 }
 auto display=value(NativeDisplay::create(c,screen.ops(),[files](const PublishedResult&p,const DisplayLayout&l){auto b=files->read(p.lease.artifacts[0]);if(!b)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*b.error);return compose_raster_page(p,0,{b.value->data(),b.value->size()},l);},[](const DisplayObservation&){return Result<void>::success();},[journal](const PublishedResult&p){return journal->save_result_stage(p);}),"same common DP owner, fake OS only");
#ifdef VX_NATIVE_P8
 auto independent=value(P8Assets::load("data/p8"),"independent frozen P8 bank");NativeP8Spec spec;spec.assets_directory="data/p8";spec.assets=independent->identity();auto library=checked(NativeP8Library::create(c,router,residency,files,display,spec),"actual native P8 factory");

 checked(rt->request_mode(AppKind::P8),"joint P8 mode");checked(rt->advance_mode(),"drained joint P8 activation");
 p8_owner_bridge_before_prepare(io->bridge,[](void*,unsigned){},nullptr);
 struct BankHold{std::shared_ptr<HostMemoryLease>charge;std::shared_ptr<const TexturePackage>bank;};std::shared_ptr<const TexturePackage>bank;
 {auto held=std::make_shared<BankHold>();held->charge=checked(rt->reserve_host_memory(2ULL*(1048576+16777216)),"input and copied dualLOD budget before allocation");std::vector<uint8_t>small(1048576),large(16777216);for(auto*v:{&small,&large})for(size_t i=0;i<v->size();++i)(*v)[i]=i%4==3?255:uint8_t(i*13+i/73);held->bank=checked(admit_uploaded_texture({small.data(),small.size()},{large.data(),large.size()}),"actual opaque dualLOD upload admission");bank=std::shared_ptr<const TexturePackage>(held,held->bank.get());}
 P8Request input;input.scene.startup_remaining=1;input.scene.remaining=1;unsigned successes=0;uint64_t static_savings=0;std::vector<P8OwnerReceipt>receipts;
 for(unsigned frame=0;frame<7;++frame){
  if(frame==2){auto live=p8_owner_bridge_live(io->bridge);ok(library->release_inactive_session(),"P8 mode retains idle static bank");require(p8_owner_bridge_live(io->bridge)==live,"mode-active cache stays resident");checked(rt->request_mode(AppKind::NN),"mode request NN");checked(rt->advance_mode(),"P8 fully retired before NN mode");ok(library->release_inactive_session(),"same worker policy releases inactive P8 root outside Runtime locks");require(p8_owner_bridge_live(io->bridge)==2,"normal P8 to NN leaves only two shader objects, no DATA root");checked(rt->request_mode(AppKind::P8),"mode back to P8");checked(rt->advance_mode(),"no inflight root across mode sequence");}
  if(frame==3){input.texture=bank;input.scene.theme=bank->identity();}
  if(frame==5)input.scene=checked(scene_control(input.scene,'3',ns()),"original scene control selects native512 fixed-view geometry");
  input.scene.remaining=1;input.request={"boot","cache-client","frame-"+std::to_string(frame)};input.display_cookie=1000+frame;input.numeric_reference=frame==0||!cache_fast;
  auto oracle=checked(input.texture?independent->prepare_texture(input.scene,input.texture,true):independent->prepare(input.scene,true),"independent scalar CPU oracle injection only");p8_owner_bridge_replace_oracle(io->bridge,oracle.buffers[4].data(),oracle.buffers[4].size()*4,oracle.buffers[0].data(),oracle.buffers[1].data(),oracle.buffers[0].size()*4);std::array<const void*,10>expected_data{};std::array<size_t,10>expected_bytes{};for(unsigned i=0;i<10;++i){expected_data[i]=oracle.buffers[i].data();expected_bytes[i]=oracle.buffers[i].size()*4;}p8_owner_bridge_expected_frame(io->bridge,expected_data.data(),expected_bytes.data(),oracle.geometry.width);
  auto before_bytes=io->uploaded_bytes,before_direct=io->direct_bytes,before_staging=io->staging_bytes,before_stage_alloc=io->staging_allocations,before_allocs=io->data_allocations;auto prepared=checked(library->prepare(input),"real native P8 cached preparation");
  if(mode=="cache-fault"&&frame==1)p8_owner_bridge_corrupt_guard(io->bridge);
  checked(rt->submit(prepared.work),"one actual shared frame queue");bool failed=false;for(unsigned step=0;step<4;++step){auto ran=rt->run_one();if(!ran){std::fprintf(stderr,"CACHE_NATIVE_FAILURE frame=%u step=%u code=%d detail=%s\n",frame,step,ran.error->code,ran.error->detail.c_str());failed=true;break;}}
  auto r=checked(prepared.owner->receipt_snapshot(),"actual owner snapshot");
  if(failed){require(mode=="cache-fault"&&frame==1&&r.first_error&&!r.retired&&!r.published&&!rt->take_result(input.request),"guard fault retains unpublished root");auto starts=io->starts();require(!rt->run_one()&&!library->release_session()&&io->starts()==starts&&p8_owner_bridge_live(io->bridge)>=3,"sticky frame/session fault cannot reuse free reset or retry");std::puts("NATIVE_P8_CACHE_REAL_C_GUARD_FAULT_RETAINS_ROOT_HOST_NOT_GPU");return 0;}
  require(r.retired&&r.published&&r.completed_gpu==2&&r.output_crc_valid&&r.next_scene&&r.display&&r.display->presented&&r.display->old_retired&&!r.gpu_raw_cycles,"original ACK guard durable output and presentation retirement");
  auto result=checked(rt->take_result(input.request),"actual final result");auto color=checked(files->read(result.lease.artifacts[0]),"durable RGBA source");auto depth=checked(files->read(result.lease.artifacts[1]),"durable depth source");require(color.size()==oracle.buffers[0].size()*4&&!std::memcmp(color.data(),oracle.buffers[0].data(),color.size())&&depth.size()==oracle.buffers[1].size()*4&&!std::memcmp(depth.data(),oracle.buffers[1].data(),depth.size()),"all actual HOST C-injected output bytes equal original scalar oracle");
  require(io->direct_bytes-before_direct==r.upload_bytes+r.args_upload_bytes&&r.args_upload_bytes==128,"owner direct receipt counts equal actual DATA-destination CPU upload ioctls");
  require(io->staging_bytes-before_staging==128&&io->staging_allocations-before_stage_alloc==2&&io->uploaded_bytes-before_bytes==r.upload_bytes+r.args_upload_bytes+128,"separate sealed ARGS staging two64B copies and all actual uploaded bytes counted");
  const bool expected_static=cache_fast&&(frame==1||frame==4||frame==6);require(r.cache_fast==cache_fast&&r.cache_static_reused==expected_static,"strict FAST and reinit keys are explicit");if(expected_static){require(r.cache_root_reused&&r.static_reused_bytes>0&&io->data_allocations==before_allocs,"retired static bank reused without new GPU DATA allocation");static_savings+=r.static_reused_bytes;}else require(!r.static_reused_bytes,"initial/mode/texture/geometry full initialization");
  if(cache_fast&&std::getenv("VX_P8_REQUIRE_ACTIVE_RANGES")){
   const auto&g=oracle.geometry;const uint64_t bins=oracle.buffers[6].size()*4+uint64_t(oracle.active_refs)*4+uint64_t(g.faces)*64*4;
   const uint64_t projected=uint64_t(r.cache_static_reused?g.active_slots*8:128512)*4;
   uint64_t statics=0;if(!r.cache_static_reused)for(unsigned i:{2u,3u,5u,9u})statics+=oracle.buffers[i].size()*4;
   const uint64_t expected=uint64_t(g.width)*g.width*8+projected+statics+(r.cache_static_reused?0:20*64)+bins;
   std::printf("P8_ACTIVE_BUDGET frame=%u actual=%llu expected=%llu refs=%u faces=%u\n",frame,(unsigned long long)r.upload_bytes,(unsigned long long)expected,oracle.active_refs,g.faces);std::fflush(stdout);
   require(r.upload_bytes==expected,"FAST upload matches active input and derived ranges");
  }
  auto view=display->snapshot();require(view.raster_cache_budget_bytes==RasterComposeCache::budget_bytes&&view.raster_cache.retained_canvases<=1&&view.raster_cache.retained_bytes<=RasterComposeCache::budget_bytes,"actual charged native raster cache bounded by persistent8MiB");
  uint64_t root_bytes=64;for(const auto&v:oracle.buffers)root_bytes+=(v.size()*4+191)&~UINT64_C(63);
  require(!cache_fast||frame!=2||(!r.cache_root_reused&&!r.cache_static_reused&&io->data_allocations==before_allocs+1),"back to P8 allocates a new root and fully initializes after inactive release");
  std::printf("P8_CACHE_FRAME frame=%u width=%u root_bytes=%llu fast=%u root_reused=%u static_reused=%u direct_data_bytes=%llu direct_args_bytes=%llu backend_staging_bytes=%llu actual_total_upload_bytes=%llu static_bytes_saved=%llu data_allocations=%llu color_sha=%s\n",frame,oracle.geometry.width,(unsigned long long)root_bytes,unsigned(r.cache_fast),unsigned(r.cache_root_reused),unsigned(r.cache_static_reused),(unsigned long long)r.upload_bytes,(unsigned long long)r.args_upload_bytes,(unsigned long long)(io->staging_bytes-before_staging),(unsigned long long)(io->uploaded_bytes-before_bytes),(unsigned long long)r.static_reused_bytes,(unsigned long long)(io->data_allocations-before_allocs),vxnn::digest_hex(result.lease.artifacts[0].sha).c_str());std::fflush(stdout);
  ok(rt->reap_result(input.request),"drop client while immutable scanout owns result");input.scene=*r.next_scene;receipts.push_back(r);++successes;
 }
 require(successes==7&&(!cache_fast||static_savings>0),"all real cached and reinitialized frames complete");ok(display->retire(),"original old front retired");ok(library->release_session(),"proven idle session root released before Runtime");require(p8_owner_bridge_live(io->bridge)==2,"only two immutable original shader objects remain");library.reset();bank.reset();ok(rt->shutdown(),"original runtime closes");ok(residency->shutdown(),"original shaders close");require(!p8_owner_bridge_live(io->bridge)&&screen.created==screen.destroyed,"all physical HOST objects and fake scanout mappings retired");std::puts("NATIVE_P8_SESSION_ACTUAL_C_IOCTL_UPLOAD_COUNTS_MODE_TEXTURE_GEOMETRY_BYTE_EQUALITY_HOST_NOT_GPU_DP");return 0;

#endif
}

#define VX_NATIVE_P8
#include <vxunified/native_p8_service.hpp>
#include <vxunified/texture_service.hpp>
#include <vxunified/native_media.hpp>
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
#include <utility>
namespace {
template<class T>T checked(vxnn::Result<T>r,const char*name){if(!r)std::fprintf(stderr,"NATIVE_MEDIA_ERROR %s code=%d category=%s detail=%s\n",name,r.error->code,r.error->category.c_str(),r.error->detail.c_str());return value(std::move(r),name);}
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen media file");return {std::istreambuf_iterator<char>(f),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
// Feature-detection fallback lets the unchanged100-frame production baseline
// compile and fail the daily1 behavioral assertion before the new option exists.
template<class T>auto startup_frames(const StartupContext&c,std::shared_ptr<NativeP8Library>library,std::shared_ptr<NativeRouter>router,uint32_t frames,int)->decltype(T::create(c,library,router,77,100,0,std::declval<const std::string&>(),frames)){return T::create(c,library,router,77,100,0,"127.0.0.1",frames);}
template<class T>auto startup_frames(const StartupContext&c,std::shared_ptr<NativeP8Library>library,std::shared_ptr<NativeRouter>router,uint32_t,long)->decltype(T::create(c,library,router,77,100,0,std::declval<const std::string&>())){return T::create(c,library,router,77,100,0,"127.0.0.1");}
uint32_t crc(vxnn::Bytes b){uint32_t c=~0u;for(size_t i=0;i<b.size;i++){c^=b.data[i];for(unsigned k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put32(std::vector<uint8_t>&v,unsigned at,uint32_t x){for(unsigned i=0;i<4;i++)v[at+i]=uint8_t(x>>(24-8*i));}
std::vector<uint8_t>packet(unsigned op){std::vector<uint8_t>p(64);put32(p,0,0x56545831);p[4]=1;p[5]=op;put32(p,8,77);put32(p,12,1);return p;}
struct Transport:IoctlTransport {
 void*bridge;
 Transport(const char*c,const char*d,Digest h){
#ifdef VX_NATIVE_P8
 bridge=p8_owner_bridge_create(c,d,h.data());
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
 return p8_owner_bridge_call(bridge,c,p);
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
 static int flip(void*p,const dp_buffer*b,uint64_t cookie){auto&s=*static_cast<Screen*>(p);s.pending=b->id;s.cookie=cookie;return 0;}
 static int receive(void*p,int,uint64_t*c,uint64_t*t){auto&s=*static_cast<Screen*>(p);*c=s.cookie;*t=ns();s.scan=s.pending;s.pending=0;return 0;}
 static int disable(void*p){auto&s=*static_cast<Screen*>(p);s.scan=s.pending=0;return 0;}
 static void destroy(void*p,dp_buffer*b){auto&s=*static_cast<Screen*>(p);require(b->id!=s.scan&&b->id!=s.pending,"native scanout retirement");std::free(b->map);++s.destroyed;}
 static void close(void*){}
 dp_ops ops(){return {open,create,initial,flip,receive,disable,destroy,close,this};}
};
template<class T,class=void>struct NativeFrameJournal{static void check(T&,const StartupContext&){require(false,"U20 native frame requires explicit zero-wire timing evidence");}};
template<class T>struct NativeFrameJournal<T,std::void_t<decltype(std::declval<T&>().save_frame_timing(std::declval<const RequestKey&>(),std::declval<const BackendIdentity&>(),vxnn::EvidenceScope{},std::declval<const IngressTiming&>()))>>{
 static void check(T&journal,const StartupContext&c){IngressTiming row;row.serial=1;row.begin_ns=ns();row.end_ns=ns();row.prepare_ns=1;row.complete_ns=1;RequestKey key{c.boot_id,"test-journal","frame"};require(!journal.save_ingress(key,c.identity,c.scope,row),"original UDP timing still requires real packet evidence");ok(journal.save_frame_timing(key,c.identity,c.scope,row),"explicit normal-frame timing with zero packets");row.packets=row.bytes=1;require(!journal.save_frame_timing(key,c.identity,c.scope,row),"frame cannot relabel real wire activity");}
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

template<class T,class=void>struct NativeTextureReceiver {
 static std::shared_ptr<T>create(const StartupContext&,std::shared_ptr<NativeRouter>){require(false,"U20 native P8 texture receiver must share common roots budget queue and cookie allocator");return {};}
 static void flow(const StartupContext&,std::shared_ptr<NativeRouter>,std::shared_ptr<NativeP8Library>,std::shared_ptr<Runtime>,std::shared_ptr<NativeDisplay>,Transport*,std::shared_ptr<const P8Assets>,bool,bool,bool){require(false,"U20 native P8 texture receiver must share common roots budget queue and cookie allocator");}
};
template<class T,class=void>struct TextureSlot{static std::shared_ptr<PreparationLease>get(const T&){require(false,"native texture must use global preparation8");return {};}};
template<class T>struct TextureSlot<T,std::void_t<decltype(std::declval<T>().preparation)>>{static std::shared_ptr<PreparationLease>get(const T&t){return t.preparation;}};
template<class T>struct NativeTextureReceiver<T,std::void_t<decltype(T::create_native(std::declval<const StartupContext&>(),std::declval<std::shared_ptr<NativeRouter>>(),uint32_t{},uint64_t{})),decltype(std::declval<T>().stage_native(std::declval<std::shared_ptr<const TexturePackage>>(),std::declval<const SceneState&>(),std::declval<std::shared_ptr<HostMemoryLease>>()))>>{
 static std::shared_ptr<T>create(const StartupContext&c,std::shared_ptr<NativeRouter>router){return checked(T::create_native(c,router,77,100),"native P8 receiver");}
 static void flow(const StartupContext&c,std::shared_ptr<NativeRouter>router,std::shared_ptr<NativeP8Library>library,std::shared_ptr<Runtime>rt,std::shared_ptr<NativeDisplay>display,Transport*io,std::shared_ptr<const P8Assets>reference,bool wire,bool cancel,bool abandon){
  auto before=rt->stats().cpu_bytes;auto receiver=create(c,router);require(rt->stats().cpu_bytes>=before+T::staging_bytes,"native P8 staging charged in common CPU budget before allocation");
  auto bank_charge=checked(rt->reserve_host_memory(T::staging_bytes),"charge immutable texture before factory");std::vector<uint8_t>small(512u*512u*4u,255),large(2048u*2048u*4u,255);auto bank=checked(admit_uploaded_texture({small.data(),small.size()},{large.data(),large.size()}),"opaque frozen dualLOD admission");SceneState scene;
  auto startup=receiver->stage_native(bank,scene,bank_charge);require(!startup&&startup.error->code==EPROTO&&!rt->stats().preparing_requests&&!io->starts(),"original100-frame startup redraw refusal preserved");
  // Explicit HOST scene fixture, as in test_texture_completion. This does not
  // claim a real prior displayed frame or bypass production startup controls.
  scene.startup_remaining=0;scene.have_displayed=true;
  if(wire){uint64_t now=1;auto send=[&](const std::vector<uint8_t>&p){return checked(receiver->handle({p.data(),p.size()},{0x7f000001,50123},now++),"original native TEX packet");};auto begin=packet(2);put32(begin,32,crc(bank->lod(0)));put32(begin,36,crc(bank->lod(1)));require(send(begin).size()==64,"native dualLOD BEGIN");for(unsigned lod=0;lod<2;++lod){auto bytes=bank->lod(lod);for(size_t at=0;at<bytes.size;at+=1200){auto p=packet(3);auto n=std::min<size_t>(1200,bytes.size-at);put32(p,16,lod);put32(p,20,at/1200);put32(p,24,n);put32(p,28,crc({bytes.data+at,n}));p.insert(p.end(),bytes.data+at,bytes.data+at+n);require(send(p).size()==64,"complete original native dualLOD packets");}}}
  auto acquire=[&]()->Result<TextureIngress>{if(!wire)return receiver->stage_native(bank,scene,bank_charge);auto r=receiver->acquire(scene);if(!r)return Result<TextureIngress>::failure(*r.error);if(!*r.value)return Result<TextureIngress>::failure({EAGAIN,"TEST","no ready wire"});return Result<TextureIngress>::success(**r.value);};
  std::vector<std::shared_ptr<PreparationLease>>full;for(unsigned n=0;n<8;++n)full.push_back(checked(rt->reserve_preparation({c.boot_id,"rpc","full-"+std::to_string(n)}),"one global RPC slot"));auto before_full=rt->stats().cpu_bytes;auto blocked=acquire();if(wire&&(!blocked&&blocked.error->code!=EAGAIN))require(false,"U20 native P8 wire ingress must share bounded original receiver path");require(!blocked&&blocked.error->code==EAGAIN&&rt->stats().preparing_requests==8&&!io->starts()&&rt->stats().cpu_bytes==before_full,"native texture cannot create ninth preparation, CPU snapshot or GPU work");
  if(abandon){ok(receiver->close(),"quiesce original unadmitted held wire before Runtime");receiver.reset();require(rt->stats().cpu_bytes+T::staging_bytes==before_full&&!io->starts(),"original unadmitted wire closes with exact shared staging charge release");full.clear();puts("U20_NATIVE_P8_TEXTURE_UNADMITTED_HELD_CLOSE_HOST_NOT_GPU_DP gpu_raw_cycles=null");return;}full.clear();
  auto in=checked(acquire(),"charged native texture source");auto slot=TextureSlot<decltype(in)>::get(in);require(slot&&rt->stats().preparing_requests==1&&in.input.cookie_namespace,"texture reserves same queue before metadata");if(wire)require(in.input.texture->source_kind()==TextureOrigin::PHOTO_UPLOAD,"wire upload never claims Mosaic lineage");auto following=checked(router->allocate_cookie_namespace(),"same global cookie namespace allocator");require(following!=in.input.cookie_namespace,"native texture cannot collide with other app requests");auto prepared=checked(library->prepare(in.input),"same native P8 factory");auto accepted=checked(rt->submit(prepared.work,slot),"consume exact texture preparation");require(!rt->stats().preparing_requests,"no private texture request queue");ok(receiver->commit(in.serial,prepared.owner,accepted),"exact native P8 source binding");
  if(cancel){ok(rt->cancel(accepted),"cancel exact common frame before execution");ok(rt->reap_result(accepted),"original cancelled frame backing retirement");ok(receiver->cancel(in.serial),"release typed cancelled owner outside receiver lock");require(checked(receiver->snapshot(),"cancelled native texture").phase==TexturePhase::CANCELLED&&!io->starts(),"cancel never renders or advances texture");ok(receiver->close(),"cancelled native receiver closes");puts("U20_NATIVE_P8_TEXTURE_CANCELLED_SHARED_LIFETIME_HOST_NOT_GPU_DP gpu_raw_cycles=null");return;}
  // Inject the independent CPU reference for THIS uploaded texture and scene,
  // not the unrelated default-theme frame. All exact readback checks remain.
  auto oracle=checked(reference->prepare_texture(in.input.scene,in.input.texture,true,in.input.texture_redraw),"independent uploaded texture CPU reference only");p8_owner_bridge_oracle(io->bridge,oracle.buffers[4].data(),oracle.buffers[4].size()*4,oracle.buffers[0].data(),oracle.buffers[1].data(),oracle.buffers[0].size()*4);
  auto waiting=checked(receiver->snapshot(),"pending wire state");require(waiting.phase!=TexturePhase::APPLIED&&!receiver->complete(in.serial),"no APPLIED before original frame and display proof");std::atomic<bool>done{false};std::thread snapshots([&](){while(!done){require(bool(receiver->snapshot()),"concurrent original receiver snapshot");rt->stats();std::this_thread::yield();}});for(uint64_t n=0;n<prepared.work.source->size();++n){auto step=rt->run_one();if(!step)std::fprintf(stderr,"NATIVE_TEXTURE_STEP_ERROR stage=%llu code=%d category=%s detail=%s\n",(unsigned long long)n,step.error->code,step.error->category.c_str(),step.error->detail.c_str());ok(step,"same native P8 control owner");}done=true;snapshots.join();auto output=checked(rt->take_result(accepted),"common P8 output");require(!output.gpu_raw_cycles&&output.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"CPU oracle/control is not physical GPU proof");auto next=checked(receiver->complete(in.serial),"actual frame receipt and old front retirement");require(next.theme==bank->identity()&&checked(receiver->snapshot(),"applied native texture").phase==TexturePhase::APPLIED,"frozen texture/scene transaction applied after retirement");
  ok(rt->reap_result(accepted),"release texture client");ok(display->retire(),"confirmed native old scanout retired");ok(rt->reap_result(accepted),"forget checked frame backing");ok(receiver->close(),"native texture quiesces");puts("U20_NATIVE_P8_TEXTURE_COMMON_ROOT_QUEUE8_COOKIE_LIFETIME_HOST_NOT_GPU_DP gpu_raw_cycles=null");
 }
};
}
int main(int argc,char**argv){
 require(argc==6,"catalog programdir table output mode");std::string mode=argv[5];const bool joint=mode.rfind("joint-",0)==0;if(joint)mode=mode.substr(6);char root[]="/tmp/vx-native-media-XXXXXX";require(mkdtemp(root),"sole private owner");ServiceConfig config;config.private_dir=root;unsigned opens=0;
 auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));}),"single owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;ExecutionHooks hooks;
 hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
#ifdef VX_NATIVE_P8
 auto compatible=vxnn::content_digest(joint?"vortex,hw4-s8-joint-v1":"vortex,w4-graphics-p8-v1");
#else
 auto compatible=vxnn::content_digest("vortex,hw4-s8-demo-v1");
#endif
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"same borrowed fd");auto t=std::make_unique<Transport>(argv[1],argv[2],compatible);io=t.get();return t;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual host C caps");auto backend=value(port->adapter_backend(),"same backend");auto raw=read(argv[1]);Digest sha{};vxl_hunet_hash(raw.data(),raw.size(),sha.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sha),"frozen catalog");auto residency=value(ProgramResidency::create(catalog,backend),"same residency");
 require(!mkdir(argv[4],0700),"private output root");char output[4096];require(realpath(argv[4],output),"normalized output root");const auto jp=std::string(output)+".journal";require(!mkdir(jp.c_str(),0700),"durable original evidence directory");auto journal=value(EvidenceJournal::open(jp),"common journal");
 const auto scope=mode=="declared"?vxnn::EvidenceScope::REAL_BOARD:vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto rt=value(Runtime::start(owner,hw,scope,port,[journal](const ExecutionReceipt&r,DeliveryStage s){return journal->save_work(r,s);},[journal](const PublishedResult&r){return journal->save_result(r);}),"common Runtime");if(joint){require(s8_joint_platform(hw),"joint catalog and exact hardware must be observed");checked(rt->request_mode(AppKind::P8),"joint native P8 mode request");checked(rt->advance_mode(),"joint native P8 complete handoff");}StartupContext c;c.borrowed_fd=owner->fd();c.identity=hw;c.compute_backend=backend;c.runtime=rt;c.results=rt->trusted_results();c.journal=journal;c.scope=scope;c.private_dir=root;c.boot_id="boot";router=value(NativeRouter::create(c),"common native router");auto files=value(NativeFiles::create(c,output),"same common output store");Screen screen;
 if(mode=="declared"){auto texture=NativeTextureReceiver<TextureService>::create(c,router);ok(texture->close(),"declared native texture zero work");texture.reset();
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
 auto independent=value(P8Assets::load("data/p8"),"independent frozen P8 bank");NativeP8Spec spec;spec.assets_directory="data/p8";spec.assets=independent->identity();auto made=NativeP8Library::create(c,router,residency,files,display,spec);require(bool(made),"U20 actual P8 native media factory must use sole owner and shared budget");auto library=*made.value;
 if(mode=="cookie-order"){
  for(uint32_t space:{3u,2u}){
   P8Request input;input.request={"boot","client","ordered-frame-"+std::to_string(space)};input.cookie_namespace=space;input.display_cookie=700+space;
   auto oracle=checked(independent->prepare(input.scene,true),"independent frozen P8 frame reference, HOST only");if(space==3)p8_owner_bridge_oracle(io->bridge,oracle.buffers[4].data(),oracle.buffers[4].size()*4,oracle.buffers[0].data(),oracle.buffers[1].data(),oracle.buffers[0].size()*4);
   auto prepared=checked(library->prepare(input),"legitimate newly prepared lower P8 namespace");auto accepted=checked(rt->submit(prepared.work),"admit native P8 frame");
   for(unsigned i=0;i<4;++i){auto step=rt->run_one();if(!step)std::fprintf(stderr,"P8_COOKIE_ORDER_FAILURE namespace=%u stage=%u code=%d detail=%s\n",space,i,step.error->code,step.error->detail.c_str());ok(step,"native P8 remapped cookie frame completes");}
   auto result=checked(rt->take_result(accepted),"completed frame result");require(result.execution_complete&&result.displayed&&!result.gpu_raw_cycles&&result.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"full frame readback/CRC/presentation and correct HOST scope");
  }
  require(io->starts()==4&&p8_owner_bridge_acks(io->bridge)==4,"two native frames, original four C starts and ACKs, no retry");ok(display->retire(),"retire final KMS fixture front");ok(rt->shutdown(),"all frame results retire");library.reset();ok(residency->shutdown(),"program cache retirement");require(!p8_owner_bridge_live(io->bridge),"no frame/program device object leak");puts("U25_NATIVE_P8_TWO_FRAMES_DESCENDING_LOGICAL_NAMESPACE_FULL_READBACK_ACK_DP_FIXTURE_PASS_NOT_GPU gpu_raw_cycles=null");return 0;
 }
 if(mode=="scene-service"||mode=="daily-selftest"){
  const bool daily=mode=="daily-selftest";
  NativeFrameJournal<EvidenceJournal>::check(*journal,c);
  auto made=daily?startup_frames<NativeP8Service>(c,library,router,1,0):NativeP8Service::create(c,library,router,77,100,0,"127.0.0.1");require(bool(made),"U20 native P8 scene service must use common worker");auto service=*made.value;
  auto initial=checked(service->snapshot(),"original scene snapshot");require(initial.scene.startup_remaining==(daily?1u:100u)&&initial.scene.remaining==(daily?1u:100u)&&initial.scene.pose==0&&!initial.active&&!io->starts(),daily?"daily1 must initialize fixedpose0 gate before any frame":"service startup preserves original100 gate and no START");
  auto stop=service->control('s');require(!stop&&stop.error->code==EAGAIN&&!io->starts(),"original control cannot bypass initial100");
  auto oracle=checked(independent->prepare(initial.scene,true),"independent first porcelain CPU oracle only");p8_owner_bridge_oracle(io->bridge,oracle.buffers[4].data(),oracle.buffers[4].size()*4,oracle.buffers[0].data(),oracle.buffers[1].data(),oracle.buffers[0].size()*4);
  const auto before_frame=rt->stats().cpu_bytes;
  auto prep=checked(service->step(),"same worker prepares original first frame");require(prep&&checked(service->snapshot(),"active first frame").active&&!io->starts(),"metadata preparation has no GPU START");
  for(unsigned n=0;n<4;++n)ok(rt->run_one(),"common original transform bins raster output");
  require(rt->stats().cpu_bytes<before_frame+(128ULL<<20),"retired P8 CPU workspace freed before next frame while durable files and DP front remain charged");
  require(checked(service->step(),"original checked completion"),"one completed first frame");auto saved=checked(service->snapshot(),"next scene only from retired frame");require(saved.scene.startup_remaining==(daily?0u:99u)&&saved.scene.completed==1&&saved.scene.pose==1&&saved.scene.have_displayed&&saved.timing.completed==1&&saved.timing.prepare_ns&&saved.timing.complete_ns&&!saved.timing.gpu_raw_cycles,"original scene evolution and Linux timing without physical GPU cycle claim");
  if(daily){
   require(io->starts()==2&&p8_owner_bridge_acks(io->bridge)==2,"daily1 still completes transform+raster and two actual fixture ACKs");
   ok(service->control('s'),"daily1 controls open only after complete checked frame");ok(service->control('r'),"explicit separate100-frame regression remains selectable");
   auto maintenance=checked(service->snapshot(),"independent maintenance100 state");require(maintenance.scene.pose==0&&maintenance.scene.remaining==100&&!maintenance.scene.startup_remaining&&maintenance.scene.completed==1,"maintenance100 schedules original pose0..99 without inventing completed frames");
   auto invalid=startup_frames<NativeP8Service>(c,library,router,2,0);require(!invalid&&invalid.error->code==EINVAL,"only daily1 or maintenance100 startup policy admitted");
  }
  ok(service->quiesce(),"native control stops");ok(display->retire(),"retire original first display before Runtime");ok(rt->shutdown(),"same common owner closes");ok(service->release(),"retired scene resources release");service.reset();library.reset();ok(residency->shutdown(),"same program cache closes");puts("U20_NATIVE_P8_SCENE_ORIGINAL_FIRST_FRAME_SAME_WORKER_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;
 }
 if(mode=="texture-ingress"||mode=="wire-ingress"||mode=="wire-cancel"||mode=="wire-abandon"){NativeTextureReceiver<TextureService>::flow(c,router,library,rt,display,io,independent,mode!="texture-ingress",mode=="wire-cancel",mode=="wire-abandon");library.reset();ok(display->retire(),"restore after texture");ok(rt->shutdown(),"shared Runtime closes");ok(residency->shutdown(),"same program cache closes");return 0;}
 P8Request input;input.request={"boot","client","frame"};input.display_cookie=701;auto oracle=value(independent->prepare(input.scene,true),"explicit CPU oracle injection, never GPU rendering");p8_owner_bridge_oracle(io->bridge,oracle.buffers[4].data(),oracle.buffers[4].size()*4,oracle.buffers[0].data(),oracle.buffers[1].data(),oracle.buffers[0].size()*4);
 auto prepared=checked(library->prepare(input),"metadata-only native P8 preparation");require(prepared.work.app==AppKind::P8&&prepared.owner&&prepared.work.source->size()==4&&rt->stats().resident_nn_models==0,"native P8 lazy transform/bins/raster/output preserves provider and NN slots");
#else
 auto a=value(rt->reserve_resident(digest(80),1024),"NN1"),b=value(rt->reserve_resident(digest(81),2048),"NN2");NativeImageSpec spec;spec.implementation=digest(7);spec.reference=digest(8);spec.readonly_table=std::make_shared<const std::vector<uint8_t>>(read(argv[3]));auto made=NativeImageLibrary::create(c,router,residency,files,display,spec);require(bool(made),"U20 actual IMAGE native media factory must use sole owner and shared budget");auto library=*made.value;
 ImageRequest input;input.request={"boot","client","image"};auto rgba=std::make_shared<std::vector<uint8_t>>(size_t(1280)*720*4);for(size_t i=0;i<rgba->size();++i)(*rgba)[i]=i%4==3?255:uint8_t((i*37+11)&255);input.rgba=rgba;input.width=1280;input.height=720;input.input_id=9;input.crc=image_crc32({rgba->data(),rgba->size()});input.rectangle={0,0,1280,720};input.params={1,1,4};input.display_cookie=701;
 auto invalid=library->prepare(input);require(!invalid&&invalid.error->code==EINVAL&&!io->starts(),"nonpixel style retains original block-zero validation");input.params.block=0;
 auto prepared=checked(library->prepare(input),"metadata-only native IMAGE preparation");require(prepared.work.app==AppKind::IMAGE&&prepared.owner&&prepared.work.source->size()>1&&rt->stats().resident_models==3&&rt->stats().resident_nn_models==2,"image uses media lifetime while both NN slots remain");
#endif
 require(opens==1&&!io->starts()&&rt->stats().scheduler.requests==0&&files->stats().retained_requests==0,"native prepare allocates no GPU object/submission and writes no phantom evidence");
 require(prepared.work.input_bytes<=(64ULL<<20)&&prepared.work.cpu_bytes>=prepared.work.input_bytes&&rt->stats().cpu_bytes<=(1ULL<<30),"common bounded CPU/input accounting before execution");
 auto charged=rt->stats().cpu_bytes;
#ifdef VX_NATIVE_P8
 auto duplicate=value(NativeP8Library::create(c,router,residency,files,display,spec),"same native P8 cache lifetime");
#else
 auto duplicate=value(NativeImageLibrary::create(c,router,residency,files,display,spec),"same native IMAGE cache lifetime");
#endif
 require(rt->stats().cpu_bytes==charged,"duplicate facade must not allocate an uncharged second native cache");duplicate.reset();
 const auto initial_created=screen.created,initial_scan=screen.scan;auto changed=c;changed.identity.bit[0]^=1;
#ifdef VX_NATIVE_P8
 require(!NativeP8Library::create(changed,router,residency,files,display,spec),"foreign P8 factory rejected before GPU");
#else
 require(!NativeImageLibrary::create(changed,router,residency,files,display,spec),"foreign IMAGE factory rejected before GPU");
#endif
 if(mode=="prepare"){prepared.work.source.reset();prepared.owner.reset();require(!router->indexed(),"unadmitted media metadata releases exact source index");input.request.request_id="abandoned-followup";input.display_cookie++;auto next=checked(library->prepare(input),"abandoned native metadata must leave same persistent cache usable");require(!io->starts()&&!rt->stats().scheduler.requests,"safe metadata rollback never runs device work");next.work.source.reset();next.owner.reset();require(!router->indexed(),"second abandoned source retires its metadata too");library.reset();}
 else if(mode=="guard"){
#ifdef VX_NATIVE_P8
  p8_owner_bridge_corrupt_guard(io->bridge);
#else
  image_owner_bridge_corrupt_guard(io->bridge);
#endif
  value(rt->submit(prepared.work),"admit exact fault candidate");auto ran=rt->run_one();require(!ran,"original per-job guard error propagates through native router");auto receipt=checked(prepared.owner->receipt_snapshot(),"original native error snapshot");std::fprintf(stderr,"NATIVE_MEDIA_GUARD_SNAPSHOT first_error=%u retired=%u published=%u created=%u initial_created=%u scan=%u initial_scan=%u cookie=%llu\n",unsigned(bool(receipt.first_error)),unsigned(receipt.retired),unsigned(receipt.published),screen.created,initial_created,screen.scan,initial_scan,(unsigned long long)screen.cookie);require(receipt.first_error&&!receipt.retired&&!receipt.published&&!rt->take_result(prepared.work.request)&&screen.created==initial_created&&screen.scan==initial_scan&&!screen.cookie,"guard fault retains unpublished backing and unchanged initial black scanout");auto starts=io->starts();require(!rt->run_one()&&!rt->shutdown()&&!residency->shutdown()&&io->starts()==starts,"native fault never auto retries resets or frees uncertain backing");std::fprintf(stderr,"NATIVE_MEDIA_EXPECTED_GUARD code=%d category=%s detail=%s\n",ran.error->code,ran.error->category.c_str(),ran.error->detail.c_str());puts("U20_NATIVE_MEDIA_FAULT_RETAINS_FIRST_ERROR_AND_BACKING_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;
 }
 else{value(rt->submit(prepared.work),"one global media request queue");std::atomic<bool>done{false};std::thread reader([&](){while(!done.load()){require(bool(prepared.owner->receipt_snapshot()),"concurrent original app snapshot");require(rt->stats().cpu_bytes<=(1ULL<<30),"common lifetime bound remains visible");files->stats();std::this_thread::yield();}});for(uint64_t n=0;n<prepared.work.source->size();++n)ok(rt->run_one(),"actual common control/retirement step");done=true;reader.join();auto p=value(rt->take_result(prepared.work.request),"public output after checked original stages and fake display");require(p.displayed&&!p.gpu_raw_cycles&&p.numeric==vxnn::NumericClass::NOT_EVALUATED&&p.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"host control is never GPU/numeric/DP evidence");auto bytes=value(files->read(p.lease.artifacts[0]),"retained exact output artifact");require(bytes.size()==p.lease.artifacts[0].bytes,"original raw output length");ok(rt->reap_result(prepared.work.request),"drop client result before old scanout retirement");ok(display->retire(),"original C fake scanout retires");ok(rt->reap_result(prepared.work.request),"forget retired native source");prepared.work.source.reset();prepared.owner.reset();library.reset();}
 ok(display->retire(),"single display restored before device close");ok(rt->shutdown(),"common Runtime shutdown");ok(residency->shutdown(),"common program residency shutdown");require(screen.created==screen.destroyed,"no live fake scanout released early");puts("U20_NATIVE_MEDIA_FACTORY_SAME_OWNER_STORAGE_BUDGET_CONTROL_PASS_HOST_C_ORACLE_NOT_GPU_DP gpu_raw_cycles=null");return 0;
}

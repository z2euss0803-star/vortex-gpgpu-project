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
namespace {
template<class T>T checked(vxnn::Result<T>r,const char*name){if(!r)std::fprintf(stderr,"NATIVE_MEDIA_ERROR %s code=%d category=%s detail=%s\n",name,r.error->code,r.error->category.c_str(),r.error->detail.c_str());return value(std::move(r),name);}
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen media file");return {std::istreambuf_iterator<char>(f),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
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
 require(argc==6,"catalog programdir table output mode");const std::string mode=argv[5];char root[]="/tmp/vx-native-media-XXXXXX";require(mkdtemp(root),"sole private owner");ServiceConfig config;config.private_dir=root;unsigned opens=0;
 auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));}),"single owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;ExecutionHooks hooks;
 hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
#ifdef VX_NATIVE_P8
 auto compatible=vxnn::content_digest("vortex,w4-graphics-p8-v1");
#else
 auto compatible=vxnn::content_digest("vortex,hw4-s8-demo-v1");
#endif
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"same borrowed fd");auto t=std::make_unique<Transport>(argv[1],argv[2],compatible);io=t.get();return t;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual host C caps");auto backend=value(port->adapter_backend(),"same backend");auto raw=read(argv[1]);Digest sha{};vxl_hunet_hash(raw.data(),raw.size(),sha.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sha),"frozen catalog");auto residency=value(ProgramResidency::create(catalog,backend),"same residency");
 require(!mkdir(argv[4],0700),"private output root");char output[4096];require(realpath(argv[4],output),"normalized output root");const auto jp=std::string(output)+".journal";require(!mkdir(jp.c_str(),0700),"durable original evidence directory");auto journal=value(EvidenceJournal::open(jp),"common journal");
 const auto scope=mode=="declared"?vxnn::EvidenceScope::REAL_BOARD:vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto rt=value(Runtime::start(owner,hw,scope,port,[journal](const ExecutionReceipt&r,DeliveryStage s){return journal->save_work(r,s);},[journal](const PublishedResult&r){return journal->save_result(r);}),"common Runtime");StartupContext c;c.borrowed_fd=owner->fd();c.identity=hw;c.compute_backend=backend;c.runtime=rt;c.results=rt->trusted_results();c.journal=journal;c.scope=scope;c.private_dir=root;c.boot_id="boot";router=value(NativeRouter::create(c),"common native router");auto files=value(NativeFiles::create(c,output),"same common output store");Screen screen;
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
 auto independent=value(P8Assets::load("data/p8"),"independent frozen P8 bank");NativeP8Spec spec;spec.assets_directory="data/p8";spec.assets=independent->identity();auto made=NativeP8Library::create(c,router,residency,files,display,spec);require(bool(made),"U20 actual P8 native media factory must use sole owner and shared budget");auto library=*made.value;
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

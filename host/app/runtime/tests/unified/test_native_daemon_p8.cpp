#include <vxunified/native_p8_service.hpp>
#include <vxunified/journal.hpp>
#include <vxl/hunet.h>
#include <vxl/v2/program.h>
#include "vendor/p8/network/texture_rx.h"
#include "scheduler_fixture.hpp"
#include "p8_owner_bridge.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fstream>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <time.h>
#include <mutex>
namespace {
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen file");return {std::istreambuf_iterator<char>(f),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Transport:IoctlTransport {
 void*bridge;Transport(const char*c,const char*d,Digest h):bridge(p8_owner_bridge_create(c,d,h.data())){}
 ~Transport(){p8_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return p8_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}
};
// Same injected HOST-only KMS contract as test_native_media, no physical display.
struct Screen {
 unsigned created=0,destroyed=0;uint32_t scan=0,pending=0;uint64_t cookie=0;
 static int open(void*,const dp_config*,dp_caps*c){*c={1280,720,1,1,1,0};return 0;}
 static int create(void*p,uint32_t w,uint32_t h,dp_buffer*b){auto&s=*static_cast<Screen*>(p);auto n=uint64_t(w)*h*4;auto*d=static_cast<uint8_t*>(std::calloc(1,size_t(n)));if(!d)return -ENOMEM;*b={++s.created,w,h,w*4,DP_ARGB8888,n,d};return 0;}
 static int initial(void*p,const dp_buffer*b){static_cast<Screen*>(p)->scan=b->id;return 0;}
 static int flip(void*p,const dp_buffer*b,uint64_t c){auto&s=*static_cast<Screen*>(p);s.pending=b->id;s.cookie=c;return 0;}
 static int receive(void*p,int,uint64_t*c,uint64_t*t){auto&s=*static_cast<Screen*>(p);*c=s.cookie;*t=ns();s.scan=s.pending;s.pending=0;return 0;}
 static int disable(void*p){auto&s=*static_cast<Screen*>(p);s.scan=s.pending=0;return 0;}
 static void destroy(void*p,dp_buffer*b){auto&s=*static_cast<Screen*>(p);require(b->id!=s.scan&&b->id!=s.pending,"no premature native scanout free");std::free(b->map);++s.destroyed;}
 static void close(void*){}dp_ops ops(){return {open,create,initial,flip,receive,disable,destroy,close,this};}
};

uint32_t crc(vxnn::Bytes b){uint32_t c=~0u;for(size_t i=0;i<b.size;i++){c^=b.data[i];for(unsigned k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put32(std::vector<uint8_t>&v,unsigned at,uint32_t x){for(unsigned i=0;i<4;i++)v[at+i]=uint8_t(x>>(24-8*i));}
uint32_t get32(const std::vector<uint8_t>&v,unsigned at){require(v.size()>=at+4,"bounded TEX reply");return uint32_t(v[at])<<24|uint32_t(v[at+1])<<16|uint32_t(v[at+2])<<8|v[at+3];}
std::vector<uint8_t>packet(unsigned op){std::vector<uint8_t>p(64);put32(p,0,TEX_MAGIC);p[4]=1;p[5]=op;put32(p,8,77);put32(p,12,1);return p;}
// Independent HOST CPU oracle mirror only. It never changes production scene
// state or claims that the injected host C marker is a rendered GPU frame.
struct Oracle {
 std::mutex mutex;std::shared_ptr<const P8Assets>assets;SceneState scene,rendered;
 std::shared_ptr<const TexturePackage>current,pending;Transport*io=nullptr;bool redraw=false,fault=false;
 static void prepare(void*p,unsigned id){auto&s=*static_cast<Oracle*>(p);std::lock_guard<std::mutex>lock(s.mutex);
  if(id==9||id==11){s.rendered=s.scene;s.redraw=bool(s.pending);auto bank=s.pending?s.pending:s.current;if(bank)s.rendered.theme=bank->identity();auto frame=value(bank?s.assets->prepare_texture(s.rendered,bank,true,s.redraw):s.assets->prepare(s.rendered,true),"independent CPU reference for exact next P8 scene");p8_owner_bridge_replace_oracle(s.io->bridge,frame.buffers[4].data(),frame.buffers[4].size()*4,frame.buffers[0].data(),frame.buffers[1].data(),frame.buffers[0].size()*4);if(s.fault)p8_owner_bridge_corrupt_guard(s.io->bridge);
  }else{require(id==10||id==12,"original raster IDs only");s.scene=value(scene_after_frame(s.rendered,s.redraw?s.rendered.displayed_pose:s.rendered.pose,s.redraw,ns()),"independent original scene expectation");if(s.redraw){s.current=s.pending;s.pending.reset();}}
 }
};
}
int main(int argc,char**argv){
 require(argc==5||argc==6,"catalog programdir assets output [fault|partial]");const std::string mode=argc==6?argv[5]:"normal";require(mode=="normal"||mode=="fault"||mode=="partial","fixed host scenario");alarm(600);char root[]="/tmp/vx-native-p8-daemon-XXXXXX";require(mkdtemp(root),"sole private owner");auto raw=read(argv[1]);require(raw.size()>=576,"catalog");vxl_v2_catalog_entry entry{};std::memcpy(&entry,raw.data()+64,512);
 DaemonConfig cfg;cfg.service.private_dir=root;cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;cfg.boot_id="boot-p8-daemon";cfg.evidence_dir=std::string(root)+"/evidence";cfg.config_sha=digest(13);cfg.identity.provider_flags=2;cfg.identity.device_generation=1;cfg.identity.compatible=vxnn::content_digest("vortex,w4-graphics-p8-v1");std::copy_n(entry.hardware_contract,32,cfg.identity.hardware_contract.begin());std::copy_n(entry.bit,32,cfg.identity.bit.begin());std::copy_n(entry.xsa,32,cfg.identity.xsa.begin());
 Screen screen;unsigned opens=0;std::shared_ptr<NativeP8Service>service;std::shared_ptr<NativeDisplay>display;std::shared_ptr<ProgramResidency>residency;std::shared_ptr<NativeRouter>router;std::weak_ptr<Runtime>runtime;Transport*io=nullptr;Oracle oracle;oracle.fault=mode=="fault";require(!mkdir(cfg.evidence_dir.c_str(),0700),"evidence directory");printf("NATIVE_DAEMON_OWNER=%s\n",root);fflush(stdout);
 auto builder=[&](const StartupContext&c)->Result<ApplicationSet>{
  runtime=c.runtime;Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},c.identity,sum),"matching frozen P8 catalog");residency=value(ProgramResidency::create(catalog,c.compute_backend),"same residency");router=value(NativeRouter::create(c),"same router");require(!mkdir(argv[4],0700),"private output");char output[4096];require(realpath(argv[4],output),"absolute output");auto files=value(NativeFiles::create(c,output),"same native files");
  display=value(NativeDisplay::create(c,screen.ops(),[files](const PublishedResult&p,const DisplayLayout&l){auto b=files->read(p.lease.artifacts[0]);if(!b)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*b.error);return compose_raster_page(p,0,{b.value->data(),b.value->size()},l);},[](const DisplayObservation&){return Result<void>::success();},[j=c.journal.lock()](const PublishedResult&p){return j->save_result_stage(p);}),"same display owner with explicitly fake HOST OS");
  oracle.assets=value(P8Assets::load(argv[3]),"independent original P8 reference assets");oracle.io=io;p8_owner_bridge_before_prepare(io->bridge,Oracle::prepare,&oracle);NativeP8Spec spec;spec.assets_directory=argv[3];spec.assets=oracle.assets->identity();auto library=value(NativeP8Library::create(c,router,residency,files,display,spec),"actual native library");service=value(NativeP8Service::create(c,library,router,77,100,0,"127.0.0.1"),"actual native scene service");ApplicationSet set;set.compute=router->handlers();ok(service->bind(set),"actual common daemon hooks");set.retire_display=[&](){return display->retire();};auto release=set.release_resources;set.release_resources=[&,release](){auto r=release();if(!r)return r;return residency->shutdown();};return Result<ApplicationSet>::success(std::move(set));
 };
 auto daemon=value(Daemon::start(cfg,builder,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){auto p=std::make_unique<Transport>(argv[1],argv[2],cfg.identity.compatible);io=p.get();return p;}),"actual sole daemon and existing execution worker");require(opens==1&&service->port(),"one device owner and native UDP port");
 auto wait=[&](uint64_t count){auto deadline=ns()+UINT64_C(300000000000);while(ns()<deadline){auto status=service->snapshot();if(status&&status.value->timing.completed>=count&&!status.value->active)return *status.value;auto r=daemon->serve_one(0);if(!r){auto primary=daemon->first_error();auto stats=runtime.lock()->stats();auto frame=service->snapshot();std::fprintf(stderr,"P8_DAEMON_ORIGINAL_ERROR code=%d category=%s detail=%s\n",r.error->code,r.error->category.c_str(),r.error->detail.c_str());if(primary)std::fprintf(stderr,"P8_DAEMON_PRIMARY code=%d category=%s detail=%s\n",primary.value->code,primary.value->category.c_str(),primary.value->detail.c_str());std::fprintf(stderr,"P8_DAEMON_RESOURCE_SNAPSHOT cpu=%llu indexed=%u requests=%u models=%u leases=%u prepared=%u frames=%llu\n",(unsigned long long)stats.cpu_bytes,stats.scheduler.indexed,stats.scheduler.requests,stats.resident_models,stats.resident_leases,stats.preparing_requests,(unsigned long long)(frame?frame.value->timing.completed:0));require(false,"normal P8 daemon must preserve original completion");}require(!daemon->first_error(),"no hidden P8 error");std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(false,"bounded HOST controller progress");return NativeP8ServiceSnapshot{};};
 if(mode=="fault"){auto deadline=ns()+UINT64_C(30000000000);while(ns()<deadline&&!daemon->first_error()){(void)daemon->serve_one(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(bool(daemon->first_error()),"original guard failure must stop daemon");require(!daemon->shutdown(),"unknown backing never guessed free after guard failure");auto state=value(service->snapshot(),"retained failed P8 scene");auto starts=p8_owner_bridge_starts(io->bridge);require(state.scene.startup_remaining==100&&!state.timing.completed&&state.active&&starts==1,"fault cannot advance scene or publish");require(!daemon->shutdown()&&p8_owner_bridge_starts(io->bridge)==starts,"no automatic retry or reset");puts("U20_NATIVE_P8_DAEMON_ORIGINAL_ERROR_RETAINED_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;}
 auto initial=wait(100);require(initial.scene.startup_remaining==0&&initial.scene.pose==0&&initial.scene.completed==100,"all original startup100 must complete, never skip");
 int client=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(client>=0,"actual UDP client");timeval timeout{10,0};require(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),"bounded host UDP response");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(service->port());
 auto send=[&](const std::vector<uint8_t>&p){require(sendto(client,p.data(),p.size(),0,reinterpret_cast<sockaddr*>(&address),sizeof(address))==ssize_t(p.size()),"actual native TEX datagram");ok(daemon->serve_one(0),"same control loop polls UDP without executing geometry");uint8_t reply[1280];auto n=recv(client,reply,sizeof(reply),0);require(n==64,"exact original TEX reply");return std::vector<uint8_t>(reply,reply+n);};
 std::vector<uint8_t>small(512u*512u*4u,255),large(2048u*2048u*4u,255);auto bank=value(admit_uploaded_texture({small.data(),small.size()},{large.data(),large.size()}),"independent original white dualLOD fixture");auto begin=packet(TEX_BEGIN);put32(begin,32,crc(bank->lod(0)));put32(begin,36,crc(bank->lod(1)));
 if(mode=="partial"){require(get32(send(begin),40)==TEX_RECEIVING,"partial original upload");ok(daemon->shutdown(),"quiesce partial network, join worker, retire DP and shared storage");require(screen.created==screen.destroyed&&!runtime.lock()->stats().preparing_requests,"no live display or preparation after partial close");close(client);puts("U20_NATIVE_P8_DAEMON_STARTUP100_PARTIAL_UPLOAD_SHUTDOWN_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;}
 auto control=[&](char command){std::lock_guard<std::mutex>lock(oracle.mutex);auto changed=value(scene_control(oracle.scene,command,ns()),"independent original command");ok(service->control(command),"same native command semantics and worker wake");oracle.scene=changed;};
 control('2');auto bag=wait(200);require(bag.scene.object==P8Object::PAPERBAG&&bag.scene.completed==200,"full original paperbag100");control('3');auto warrior=wait(201);require(warrior.scene.object==P8Object::TERRACOTTA,"native512 warrior");uint64_t count=201;for(char command:std::string("qazbcde")){control(command);wait(++count);}require(count==208,"warrior original eight fixed views");
 {std::lock_guard<std::mutex>lock(oracle.mutex);oracle.pending=bank;}
 require(get32(send(begin),40)==TEX_RECEIVING,"original dualLOD wire begin");for(unsigned lod=0;lod<2;++lod){auto bytes=bank->lod(lod);for(size_t at=0;at<bytes.size;at+=1200){auto p=packet(TEX_DATA);auto n=std::min<size_t>(1200,bytes.size-at);put32(p,16,lod);put32(p,20,at/1200);put32(p,24,n);put32(p,28,crc({bytes.data+at,n}));p.insert(p.end(),bytes.data+at,bytes.data+at+n);send(p);}}
 auto deadline=ns()+UINT64_C(30000000000);bool applied=false;while(ns()<deadline){auto r=send(packet(TEX_STATUS));require(get32(r,40)!=TEX_FAILED,"no hidden texture error");if(get32(r,40)==TEX_APPLIED){applied=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(applied,"APPLIED only after original GPU/readback/display receipts");auto final=wait(209);require(final.scene.theme==bank->identity()&&final.timing.packets&&final.timing.wire_bytes>=17825792&&final.timing.protocol_ns&&final.timing.prepare_ns&&final.timing.complete_ns&&!final.timing.gpu_raw_cycles,"actual service phases and explicit null physical cycles");
 ok(daemon->shutdown(),"single ingress/worker/DP/device shutdown chain");require(screen.created==screen.destroyed&&!runtime.lock()->stats().preparing_requests,"no leaked scanout or slot");close(client);printf("U20_NATIVE_P8_DAEMON_STARTUP100_THREE_OBJECTS_EIGHT_VIEWS_UDP_APPLIED_HOST_NOT_GPU_DP frames=%llu packets=%llu protocol_ns=%llu prepare_ns=%llu complete_ns=%llu gpu_raw_cycles=null\n",(unsigned long long)final.timing.completed,(unsigned long long)final.timing.packets,(unsigned long long)final.timing.protocol_ns,(unsigned long long)final.timing.prepare_ns,(unsigned long long)final.timing.complete_ns);return 0;
}

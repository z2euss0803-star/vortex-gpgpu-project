#include <vxunified/native_image_service.hpp>
#include <vxunified/journal.hpp>
#include <vxl/hunet.h>
#include <vxl/v2/program.h>
#include "image/network/image_rx_linux.h"
#include "scheduler_fixture.hpp"
#include "image_owner_bridge.h"
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
namespace {
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen file");return {std::istreambuf_iterator<char>(f),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Transport:IoctlTransport {
 void*bridge;Transport(const char*c,const char*d,Digest h):bridge(image_owner_bridge_create(c,d,h.data())){}
 ~Transport(){image_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return image_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}
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
}
int main(int argc,char**argv){
 require(argc==5||argc==6,"catalog programdir table output [partial]");const bool partial=argc==6&&std::string(argv[5])=="partial";alarm(90);char root[]="/tmp/vx-native-daemon-XXXXXX";require(mkdtemp(root),"private owner");auto raw=read(argv[1]);require(raw.size()>=576,"catalog entry extent");vxl_v2_catalog_entry entry{};std::memcpy(&entry,raw.data()+64,512);
 DaemonConfig cfg;cfg.service.private_dir=root;cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;cfg.boot_id="boot-daemon";cfg.evidence_dir=std::string(root)+"/evidence";cfg.config_sha=digest(13);cfg.identity.provider_flags=1;cfg.identity.device_generation=1;cfg.identity.compatible=vxnn::content_digest("vortex,hw4-s8-demo-v1");std::copy_n(entry.hardware_contract,32,cfg.identity.hardware_contract.begin());std::copy_n(entry.bit,32,cfg.identity.bit.begin());std::copy_n(entry.xsa,32,cfg.identity.xsa.begin());
 Screen screen;unsigned opens=0;std::shared_ptr<NativeImageService>service;std::shared_ptr<NativeDisplay>display;std::shared_ptr<ProgramResidency>residency;std::shared_ptr<NativeRouter>router;std::weak_ptr<Runtime>runtime;
 require(!mkdir(cfg.evidence_dir.c_str(),0700),"private evidence directory");
 auto builder=[&](const StartupContext&c)->Result<ApplicationSet>{
  runtime=c.runtime;Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},c.identity,sum),"catalog bound to actual common C fixture");residency=value(ProgramResidency::create(catalog,c.compute_backend),"sole program residency");router=value(NativeRouter::create(c),"sole native router");require(!mkdir(argv[4],0700),"private outputs");char output[4096];require(realpath(argv[4],output),"normalized outputs");auto files=value(NativeFiles::create(c,output),"same output budget");
  display=value(NativeDisplay::create(c,screen.ops(),[files](const PublishedResult&p,const DisplayLayout&l){auto b=files->read(p.lease.artifacts[0]);if(!b)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*b.error);return compose_raster_page(p,0,{b.value->data(),b.value->size()},l);},[](const DisplayObservation&){return Result<void>::success();},[j=c.journal.lock()](const PublishedResult&r){return j->save_result_stage(r);}),"injected HOST display");auto table=std::make_shared<const std::vector<uint8_t>>(read(argv[3]));NativeImageSpec spec;spec.implementation=digest(7);spec.reference=digest(8);spec.readonly_table=table;auto library=value(NativeImageLibrary::create(c,router,residency,files,display,spec),"native frozen library");auto created=NativeImageService::create(c,library,42,"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3",100,0,"127.0.0.1");require(bool(created),"U20 actual native UDP service must join daemon single worker");service=*created.value;ApplicationSet set;set.compute=router->handlers();ok(service->bind(set),"bind existing daemon execution worker and control poll");set.retire_display=[&](){return display->retire();};auto release=set.release_resources;set.release_resources=[&,release](){auto x=release();if(!x)return x;return residency->shutdown();};return Result<ApplicationSet>::success(std::move(set));
 };
 auto daemon=value(Daemon::start(cfg,builder,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){return std::make_unique<Transport>(argv[1],argv[2],cfg.identity.compatible);}),"actual daemon owns execution and ingress");require(opens==1&&service->port(),"exactly one GPU device open and actual loopback port");
 int rpc=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(rpc>=0,"original private RPC client");sockaddr_un endpoint{};endpoint.sun_family=AF_UNIX;auto path=std::string(root)+"/control.sock";require(path.size()<sizeof(endpoint.sun_path),"private RPC path");std::memcpy(endpoint.sun_path,path.c_str(),path.size()+1);require(!connect(rpc,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint)),"original RPC connects alongside UDP");std::string models="{\"version\":1,\"command\":\"models\"}";uint32_t frame=htonl(uint32_t(models.size()));require(send(rpc,&frame,4,MSG_NOSIGNAL)==4&&send(rpc,models.data(),models.size(),MSG_NOSIGNAL)==ssize_t(models.size()),"original framed inventory request");ok(daemon->serve_one(500),"same control loop preserves actual RPC peer deadlines");require(recv(rpc,&frame,4,MSG_WAITALL)==4,"RPC response prefix");auto length=ntohl(frame);require(length&&length<=65536,"bounded authenticated RPC response");std::string response(length,'\0');require(recv(rpc,&response[0],length,MSG_WAITALL)==ssize_t(length)&&response.find("\"ok\":true")!=std::string::npos,"original RPC succeeds with native ingress enabled");close(rpc);
 int client=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(client>=0,"UDP client");timeval timeout{2,0};require(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),"bounded UDP reply");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(service->port());
 uint64_t calls=0;auto send=[&](image_header q,const uint8_t*p,size_t n){std::vector<uint8_t>packet(64+n);q.length=n;q.payload_crc=n?image_crc32({p,n}):0;image_header_encode(packet.data(),&q);if(n)std::memcpy(packet.data()+64,p,n);require(sendto(client,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&address),sizeof(address))==ssize_t(packet.size()),"actual UDP send");auto handled=daemon->serve_one(0);if(!handled)std::fprintf(stderr,"NATIVE_DAEMON_POLL_ERROR code=%d category=%s detail=%s\n",handled.error->code,handled.error->category.c_str(),handled.error->detail.c_str());ok(handled,"common control loop polls original UDP wire");++calls;uint8_t reply[1280];auto bytes=recv(client,reply,sizeof(reply),0);require(bytes>=64,"original UDP reply received");image_header h{};require(!image_header_decode(&h,reply,size_t(bytes)),"original reply");return h;};
 std::vector<uint8_t>rgba(IMAGE_BYTES);for(size_t i=0;i<rgba.size();++i)rgba[i]=i%4==3?255:uint8_t(i%251);image_header q{};q.session=42;q.input_id=1;q.width=1280;q.height=720;q.image_crc=image_crc32({rgba.data(),rgba.size()});q.op=IMAGE_BEGIN;q.tx=1;q.style=1;q.preset=1;uint8_t rectangle[16]{};image_put32(rectangle+8,1280);image_put32(rectangle+12,720);require(send(q,rectangle,16).state==IMAGE_RECEIVING,"original BEGIN");
 if(partial){ok(daemon->shutdown(),"cancel partial original upload, no GPU submitted");require(!service->stats().completed&&screen.created==screen.destroyed,"partial shutdown retires original CPU/network/KMS state");close(client);puts("U20_NATIVE_IMAGE_DAEMON_PARTIAL_UPLOAD_SHUTDOWN_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;}
 q.op=IMAGE_DATA;for(size_t i=0;i<rgba.size();i+=1200){q.offset=i;send(q,rgba.data()+i,std::min(size_t(1200),rgba.size()-i));}
 q.op=IMAGE_STATUS;auto until=ns()+UINT64_C(30000000000);bool applied=false;while(ns()<until){auto h=send(q,nullptr,0);require(h.state!=IMAGE_FAILED,"no hidden daemon failure");if(h.state==IMAGE_APPLIED){applied=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(applied,"APPLIED after common worker, exact result and fake KMS retirement");auto stats=service->stats();require(stats.completed==1&&stats.packets>=calls&&stats.wire_bytes>=IMAGE_BYTES&&stats.protocol_ns&&stats.prepare_ns&&stats.complete_ns&&!stats.gpu_raw_cycles,"actual Linux phase timing and null GPU cycles");require(!daemon->first_error(),"no daemon first error");ok(daemon->shutdown(),"quiesce ingress, join single worker, retire display and shared resources");require(screen.created==screen.destroyed&&!runtime.lock()->stats().preparing_requests,"no pinned display or preparation slot after shutdown");close(client);printf("U20_NATIVE_IMAGE_DAEMON_ACTUAL_UDP_SINGLE_WORKER_FULL720_PASS_HOST_NOT_GPU_DP packets=%llu protocol_ns=%llu prepare_ns=%llu complete_ns=%llu gpu_raw_cycles=null\n",(unsigned long long)stats.packets,(unsigned long long)stats.protocol_ns,(unsigned long long)stats.prepare_ns,(unsigned long long)stats.complete_ns);return 0;
}

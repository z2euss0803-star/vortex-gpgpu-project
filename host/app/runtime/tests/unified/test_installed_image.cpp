#include <vxunified/native_image_service.hpp>
#include <vxunified/installation.hpp>
#include "nnrt/package/json.hpp"
#include <experimental/filesystem>
#include <sstream>
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
 unsigned opened=0,created=0,destroyed=0;uint32_t scan=0,pending=0;uint64_t cookie=0;
 static int open(void*p,const dp_config*,dp_caps*c){++static_cast<Screen*>(p)->opened;*c={1280,720,1,1,1,0};return 0;}
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
 require(argc==5||argc==6,"catalog programdir table output [partial]");const bool partial=argc==6&&std::string(argv[5])=="partial";alarm(90);char root[]="/tmp/vx-native-installed-image-XXXXXX";require(mkdtemp(root),"private owner");printf("NATIVE_DAEMON_OWNER=%s\n",root);std::fflush(stdout);auto raw=read(argv[1]);require(raw.size()>=576,"catalog entry extent");vxl_v2_catalog_entry entry{};std::memcpy(&entry,raw.data()+64,512);
 DaemonConfig cfg;cfg.service.private_dir=root;cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;cfg.boot_id="boot-daemon";cfg.evidence_dir=std::string(root)+"/evidence";cfg.config_sha=digest(13);cfg.identity.provider_flags=1;cfg.identity.device_generation=1;cfg.identity.compatible=vxnn::content_digest("vortex,hw4-s8-demo-v1");std::copy_n(entry.hardware_contract,32,cfg.identity.hardware_contract.begin());std::copy_n(entry.bit,32,cfg.identity.bit.begin());std::copy_n(entry.xsa,32,cfg.identity.xsa.begin());
 Screen screen;unsigned opens=0;std::weak_ptr<Runtime>runtime;
 require(!mkdir(cfg.evidence_dir.c_str(),0700),"private evidence directory");
 auto hex=[](const Digest&v){return vxnn::digest_hex(v);};const auto cat_path=std::string(root)+"/catalog.bin",table_path=std::string(root)+"/tables.bin";
 const auto table=read(argv[3]);for(const auto&item:std::vector<std::pair<std::string,const std::vector<uint8_t>*>>{{cat_path,&raw},{table_path,&table}}){std::ofstream f(item.first,std::ios::binary);f.write(reinterpret_cast<const char*>(item.second->data()),item.second->size());f.close();require(bool(f)&&!chmod(item.first.c_str(),0400),"private immutable exact asset copies");}Digest table_sha{},catalog_sha{};vxl_hunet_hash(raw.data(),raw.size(),catalog_sha.data());vxl_hunet_hash(table.data(),table.size(),table_sha.data());
 int reserve=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(reserve>=0,"reserve available test reserved_endpoint");sockaddr_in reserved_endpoint{};reserved_endpoint.sin_family=AF_INET;reserved_endpoint.sin_addr.s_addr=htonl(INADDR_LOOPBACK);require(!bind(reserve,reinterpret_cast<sockaddr*>(&reserved_endpoint),sizeof(reserved_endpoint)),"reserve loopback port");socklen_t reserved_endpoint_size=sizeof(reserved_endpoint);require(!getsockname(reserve,reinterpret_cast<sockaddr*>(&reserved_endpoint),&reserved_endpoint_size),"read actual reserved_endpoint");const auto port=ntohs(reserved_endpoint.sin_port);require(port,"nonzero sealed UDP port");close(reserve);
 std::ostringstream text;text<<"{\"version\":2,\"hardware\":{\"hardware_contract\":\""<<hex(cfg.identity.hardware_contract)<<"\",\"bit\":\""<<hex(cfg.identity.bit)<<"\",\"xsa\":\""<<hex(cfg.identity.xsa)<<"\",\"compatible\":\""<<hex(cfg.identity.compatible)<<"\",\"provider_flags\":1},\"catalog\":{\"path\":\""<<cat_path<<"\",\"sha256\":\""<<hex(catalog_sha)<<"\"},\"nn\":[],\"display\":{\"device\":\"/dev/dri/card0\"},\"image\":{\"implementation\":\""<<hex(digest(7))<<"\",\"reference\":\""<<hex(digest(8))<<"\",\"readonly\":{\"path\":\""<<table_path<<"\",\"sha256\":\""<<hex(table_sha)<<"\"},\"session\":42,\"bundle\":\"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3\",\"display_base\":100,\"port\":"<<port<<",\"address\":\"127.0.0.1\",\"job_budget_ns\":60000000000},\"p8\":null}";
 const auto manifest=std::string(root)+"/installation.json",seal=manifest+".sha256";{std::ofstream f(manifest,std::ios::binary);f<<text.str();require(bool(f),"write exact sealed installation");}{std::ofstream f(seal,std::ios::binary);f<<hex(vxnn::content_digest(text.str()))<<'\n';require(bool(f),"write separate installation seal");}
 require(!chmod(manifest.c_str(),0400)&&!chmod(seal.c_str(),0400),"sealed installed file permissions");
 auto native=installed_applications_for_host_test(manifest,seal,screen.ops());
 auto builder=[&](const StartupContext&c){runtime=c.runtime;return native(c);};
 auto daemon_result=Daemon::start(cfg,builder,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){return std::make_unique<Transport>(argv[1],argv[2],cfg.identity.compatible);});if(!daemon_result)std::fprintf(stderr,"INSTALLED_IMAGE_STARTUP_ERROR code=%d category=%s detail=%s\n",daemon_result.error->code,daemon_result.error->category.c_str(),daemon_result.error->detail.c_str());auto daemon=value(daemon_result,"actual daemon owns execution and ingress");require(opens==1&&screen.opened==1,"U20 sealed IMAGE installation must bind exactly one shared native display and UDP service");StartupContext real;real.scope=vxnn::EvidenceScope::REAL_BOARD;auto counterfeit=native(real);require(!counterfeit&&counterfeit.error->code==EPERM,"injected HOST display cannot create physical evidence");
 int rpc=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(rpc>=0,"original private RPC client");sockaddr_un endpoint{};endpoint.sun_family=AF_UNIX;auto path=std::string(root)+"/control.sock";require(path.size()<sizeof(endpoint.sun_path),"private RPC path");std::memcpy(endpoint.sun_path,path.c_str(),path.size()+1);require(!connect(rpc,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint)),"original RPC connects alongside UDP");std::string models="{\"version\":1,\"command\":\"models\"}";uint32_t frame=htonl(uint32_t(models.size()));require(send(rpc,&frame,4,MSG_NOSIGNAL)==4&&send(rpc,models.data(),models.size(),MSG_NOSIGNAL)==ssize_t(models.size()),"original framed inventory request");ok(daemon->serve_one(500),"same control loop preserves actual RPC peer deadlines");require(recv(rpc,&frame,4,MSG_WAITALL)==4,"RPC response prefix");auto length=ntohl(frame);require(length&&length<=65536,"bounded authenticated RPC response");std::string response(length,'\0');require(recv(rpc,&response[0],length,MSG_WAITALL)==ssize_t(length)&&response.find("\"ok\":true")!=std::string::npos,"original RPC succeeds with native ingress enabled");close(rpc);
 int client=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(client>=0,"UDP client");timeval timeout{2,0};require(!setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),"bounded UDP reply");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
 uint64_t calls=0;auto send=[&](image_header q,const uint8_t*p,size_t n){std::vector<uint8_t>packet(64+n);q.length=n;q.payload_crc=n?image_crc32({p,n}):0;image_header_encode(packet.data(),&q);if(n)std::memcpy(packet.data()+64,p,n);require(sendto(client,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&address),sizeof(address))==ssize_t(packet.size()),"actual UDP send");auto handled=daemon->serve_one(0);if(!handled)std::fprintf(stderr,"NATIVE_DAEMON_POLL_ERROR code=%d category=%s detail=%s\n",handled.error->code,handled.error->category.c_str(),handled.error->detail.c_str());ok(handled,"common control loop polls original UDP wire");++calls;uint8_t reply[1280];auto bytes=recv(client,reply,sizeof(reply),0);require(bytes>=64,"original UDP reply received");image_header h{};require(!image_header_decode(&h,reply,size_t(bytes)),"original reply");return h;};
 std::vector<uint8_t>rgba(IMAGE_BYTES);for(size_t i=0;i<rgba.size();++i)rgba[i]=i%4==3?255:uint8_t(i%251);image_header q{};q.session=42;q.input_id=1;q.width=1280;q.height=720;q.image_crc=image_crc32({rgba.data(),rgba.size()});q.op=IMAGE_BEGIN;q.tx=1;q.style=1;q.preset=1;uint8_t rectangle[16]{};image_put32(rectangle+8,1280);image_put32(rectangle+12,720);require(send(q,rectangle,16).state==IMAGE_RECEIVING,"original BEGIN");
 if(partial){ok(daemon->shutdown(),"cancel partial original upload, no GPU submitted");require(screen.created==screen.destroyed,"partial shutdown retires original CPU/network/KMS state");close(client);puts("U20_NATIVE_IMAGE_DAEMON_PARTIAL_UPLOAD_SHUTDOWN_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;}
 q.op=IMAGE_DATA;for(size_t i=0;i<rgba.size();i+=1200){q.offset=i;send(q,rgba.data()+i,std::min(size_t(1200),rgba.size()-i));}
 q.op=IMAGE_STATUS;auto until=ns()+UINT64_C(30000000000);bool applied=false;while(ns()<until){auto h=send(q,nullptr,0);require(h.state!=IMAGE_FAILED,"no hidden daemon failure");if(h.state==IMAGE_APPLIED){applied=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(applied,"APPLIED after common worker, exact result and fake KMS retirement");require(!daemon->first_error(),"no daemon first error");ok(daemon->shutdown(),"quiesce ingress, join single worker, retire display and shared resources");require(screen.created==screen.destroyed&&!runtime.lock()->stats().preparing_requests,"no pinned display or preparation slot after shutdown");close(client);
 unsigned ingress=0,staged=0,displayed=0;namespace fs=std::experimental::filesystem;
 for(const auto&f:fs::directory_iterator(cfg.evidence_dir)){
  if(!fs::is_regular_file(f.path())||f.path().extension()!=".json")continue;
  std::ifstream in(f.path().string());std::string raw_row{std::istreambuf_iterator<char>(in),{}};
  auto row=vxnn::json::parse(raw_row);auto kind=vxnn::json::str(vxnn::json::member(row.get(),"kind"));
  if(kind=="native-ingress"){
   ++ingress;require(!vxnn::json::member(row.get(),"gpu_raw_cycles"),"HOST raw cycle null preserved");
   for(const auto*field:{"protocol_ns","prepare_ns","complete_ns"})require(vxnn::json::u64(vxnn::json::member(row.get(),field))>0,"actual recorded Linux stage time");
  }
  if(kind=="result_stage")++staged;
  if(kind=="native-display"){++displayed;require(!vxnn::json::member(row.get(),"gpu_raw_cycles"),"display never invents GPU cycles");}
 }
 require(ingress==1&&staged>=1&&displayed>=1,"actual ingress phase journal and display-stage receipt persisted");
 printf("U20_INSTALLED_IMAGE_ACTUAL_SEALED_STARTUP_UDP_FULL720_COMMON_FILES_DISPLAY_AND_SHUTDOWN_PASS_HOST_NOT_GPU_DP packets=%llu gpu_raw_cycles=null\n",(unsigned long long)calls);return 0;
}

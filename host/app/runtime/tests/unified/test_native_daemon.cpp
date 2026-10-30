#include <vxunified/installation.hpp>
#include "scheduler_fixture.hpp"
#include "mosaic_owner_bridge.h"
#include "hunet_owner_bridge.h"
#include "nnrt/package/json.hpp"
#include <vxl/hunet.h>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#ifdef VX_TEST_HUNET
#define mosaic_owner_bridge_create(c,code,ro,compatible) hunet_owner_bridge_create(c,code,compatible)
#define mosaic_owner_bridge_destroy hunet_owner_bridge_destroy
#define mosaic_owner_bridge_call hunet_owner_bridge_call
#define mosaic_owner_bridge_starts hunet_owner_bridge_starts
#define mosaic_owner_bridge_acks hunet_owner_bridge_acks
#define mosaic_owner_bridge_live hunet_owner_bridge_live
#define register_mosaic_operators register_hunet_operators
#define prepare_mosaic prepare_hunet
#endif
extern "C" int mosaic_owner_validate_args(const void*p){VxnnMosaicShape s{};return vxnn_mosaic_validate(static_cast<const VxnnMosaicArgs*>(p),true,&s);}
namespace {
using vxnn::digest_hex;
uint64_t now(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"host clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
std::string absolute(const char*p){char path[4096];require(realpath(p,path),"real absolute fixture path");return path;}
std::string read(const std::string&p){std::ifstream f(p,std::ios::binary);require(bool(f),"read frozen file");return std::string(std::istreambuf_iterator<char>(f),{});}
void seal(const std::string&p,const std::string&raw){std::ofstream f(p,std::ios::binary);require(bool(f),"create own sealed fixture");f.write(raw.data(),raw.size());f.close();require(!chmod(p.c_str(),0400),"seal fixture0400");}
struct Transport:IoctlTransport {
 void*bridge;explicit Transport(char**v,Digest c):bridge(mosaic_owner_bridge_create(v[2],v[3],v[4],c.data())){}~Transport(){mosaic_owner_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return mosaic_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return now();}
};
struct Screen {
 unsigned opened=0,created=0,destroyed=0;uint32_t scan=0,pending=0;uint64_t cookie=0;
 static int open(void*p,const dp_config*,dp_caps*c){++static_cast<Screen*>(p)->opened;*c={1280,720,1,1,1,0};return 0;}
 static int create(void*p,uint32_t w,uint32_t h,dp_buffer*b){auto&s=*static_cast<Screen*>(p);auto n=uint64_t(w)*h*4;auto*d=static_cast<uint8_t*>(std::calloc(1,size_t(n)));if(!d)return -ENOMEM;*b={++s.created,w,h,w*4,DP_ARGB8888,n,d};return 0;}
 static int initial(void*p,const dp_buffer*b){static_cast<Screen*>(p)->scan=b->id;return 0;}
 static int flip(void*p,const dp_buffer*b,uint64_t c){auto&s=*static_cast<Screen*>(p);s.pending=b->id;s.cookie=c;return 0;}
 static int receive(void*p,int,uint64_t*c,uint64_t*t){auto&s=*static_cast<Screen*>(p);*c=s.cookie;*t=now();s.scan=s.pending;s.pending=0;return 0;}
 static int disable(void*p){auto&s=*static_cast<Screen*>(p);s.scan=s.pending=0;return 0;}
 static void destroy(void*p,dp_buffer*b){auto&s=*static_cast<Screen*>(p);require(b->id!=s.scan&&b->id!=s.pending,"no premature native scanout free");std::free(b->map);++s.destroyed;}
 static void close(void*){}dp_ops ops(){return {open,create,initial,flip,receive,disable,destroy,close,this};}
};
void bytes(int fd,void*p,size_t count,bool send_data){size_t n=0;while(n<count){auto r=send_data?send(fd,static_cast<char*>(p)+n,count-n,MSG_NOSIGNAL):recv(fd,static_cast<char*>(p)+n,count-n,0);if(r<0&&errno==EINTR)continue;if(r<=0)throw std::runtime_error("exact native RPC framing: syscall="+std::to_string(r)+" errno="+std::to_string(r<0?errno:0));n+=size_t(r);}}
vxnn::json::Ptr rpc(const std::shared_ptr<Daemon>&d,const std::string&dir,std::string input){
 int served=-1;std::string server_error;std::thread worker([&]{auto r=d->serve_one(2000);served=r?0:r.error->code;if(!r)server_error=r.error->category+":"+r.error->detail;});int fd=-1;std::string result;
 try{fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)throw std::runtime_error("one control socket");sockaddr_un a{};a.sun_family=AF_UNIX;auto p=dir+"/control.sock";if(p.size()>=sizeof(a.sun_path))throw std::runtime_error("RPC path bounded");memcpy(a.sun_path,p.c_str(),p.size()+1);if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)))throw std::runtime_error("connect real native Daemon errno="+std::to_string(errno));uint32_t n=htonl(uint32_t(input.size()));bytes(fd,&n,4,true);bytes(fd,&input[0],input.size(),true);bytes(fd,&n,4,false);n=ntohl(n);if(!n||n>65536)throw std::runtime_error("bounded native reply");result.resize(n);bytes(fd,&result[0],n,false);}
 catch(const std::exception&e){if(fd>=0)close(fd);worker.join();std::fprintf(stderr,"NATIVE_RPC_CLIENT_ERROR=%s SERVER_ERRNO=%d SERVER_ERROR=%s\n",e.what(),served,server_error.c_str());require(false,"exact native RPC framing after owned server join");}
 close(fd);worker.join();if(served)std::fprintf(stderr,"NATIVE_RPC_SERVER_ERRNO=%d ERROR=%s\n",served,server_error.c_str());require(!served,"actual service handles native RPC");return vxnn::json::parse(result);
}
}
int main(int argc,char**argv){
 require(argc==7||argc==9,"package/catalog/code/ro/output/case [view-dir view-SHA]");std::string mode=argv[6];const bool showing=mode=="view",exporting=mode=="export";require(showing?argc==9:argc==7,"exact optional view arguments");auto package=value(vxnn::load_package(vxnn::Path(argv[1])),"independent frozen package fixture contract");const auto&binding=*package.assets().at("binding.json").content;auto doc=vxnn::json::parse(std::string(binding.begin(),binding.end()));auto*b=doc.get();auto*h=vxnn::json::member(b,"hardware");BackendIdentity hw;hw.hardware_contract=vxnn::json::digest(vxnn::json::member(h,"hardware_contract"));hw.bit=vxnn::json::digest(vxnn::json::member(h,"bit_sha256"));hw.xsa=vxnn::json::digest(vxnn::json::member(h,"xsa_sha256"));hw.compatible=vxnn::content_digest(vxnn::json::str(vxnn::json::member(h,"compatible")));hw.device_generation=1;hw.provider_flags=1;
#ifdef VX_TEST_HUNET
 vxnn::HunetFamily family{vxnn::json::digest(vxnn::json::member(b,"code_sha256")),vxnn::json::digest(vxnn::json::member(b,"implementation_sha256")),vxnn::json::digest(vxnn::json::member(b,"schema_sha256"))};const std::string kind="hunet";
#else
 vxnn::MosaicFamily family{vxnn::json::digest(vxnn::json::member(b,"code_sha256")),vxnn::json::digest(vxnn::json::member(b,"readonly_sha256")),vxnn::json::digest(vxnn::json::member(b,"schema_sha256")),vxnn::json::digest(vxnn::json::member(b,"implementation_sha256"))};const std::string kind="mosaic";
#endif
 char owner[]="/tmp/vx-native-daemon-XXXXXX";require(mkdtemp(owner),"private sole owner");std::fprintf(stderr,"NATIVE_DAEMON_OWNER=%s\n",owner);require(!mkdir(argv[5],0700),"fresh output fixture");auto output=absolute(argv[5]);auto manifest_path=std::string(owner)+"/installation.json",sha_path=std::string(owner)+"/installation.sha256";auto catalog_path=std::string(owner)+"/catalog.bin";seal(catalog_path,read(argv[2]));
 DaemonConfig cfg;cfg.service.private_dir=owner;cfg.identity=hw;cfg.boot_id="nativeboot";cfg.evidence_dir=output+"/journal";require(!mkdir(cfg.evidence_dir.c_str(),0700),"private actual service journal");cfg.config_sha=digest(99);cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;Transport*io=nullptr;unsigned opens=0;uint64_t jobs=0;std::string manifest;Screen screen;
 auto builder=[&](const StartupContext&ctx)->Result<ApplicationSet>{
  auto caps=value(ctx.compute_backend->capabilities(),"actual cached provider limits");vxnn::OperatorRegistry ops;ok(vxnn::register_mosaic_operators(ops,family),"exact original operators");auto plan=value(vxnn::prepare_mosaic(package,ops,caps,ctx.identity),"independent original plan metadata");jobs=plan.total_jobs;
  auto o=vxnn::json::parse("{}");auto put=[](json_object*x,const char*k,const std::string&v){json_object_object_add(x,k,json_object_new_string_len(v.data(),int(v.size())));};auto num=[](json_object*x,const char*k,uint64_t v){json_object_object_add(x,k,json_object_new_uint64(v));};num(o.get(),"version",1);auto*y=json_object_new_object();put(y,"hardware_contract",digest_hex(hw.hardware_contract));put(y,"bit",digest_hex(hw.bit));put(y,"xsa",digest_hex(hw.xsa));put(y,"compatible",digest_hex(hw.compatible));num(y,"provider_flags",1);json_object_object_add(o.get(),"hardware",y);auto*c=json_object_new_object();auto raw=read(catalog_path);put(c,"path",catalog_path);put(c,"sha256",digest_hex(vxnn::content_digest(raw)));json_object_object_add(o.get(),"catalog",c);auto*array=json_object_new_array();auto*n=json_object_new_object();put(n,"id","installed-nn");put(n,"input_id","sealed-input");put(n,"kind",kind);put(n,"package",absolute(argv[1]));put(n,"model",digest_hex(package.model_id()));put(n,"plan",digest_hex(plan.plan_id));put(n,"input",digest_hex(package.assets().at("input.f32").sha));num(n,"input_bytes",package.assets().at("input.f32").content->size());num(n,"resident_bytes",package.resident_bytes());num(n,"job_budget_ns",60000000000ULL);auto*f=json_object_new_object();put(f,"code",vxnn::json::str(vxnn::json::member(b,"code_sha256")));put(f,"schema",vxnn::json::str(vxnn::json::member(b,"schema_sha256")));put(f,"implementation",vxnn::json::str(vxnn::json::member(b,"implementation_sha256")));if(kind=="mosaic")put(f,"readonly",vxnn::json::str(vxnn::json::member(b,"readonly_sha256")));json_object_object_add(n,"family",f);json_object_array_add(array,n);json_object_object_add(o.get(),"nn",array);
  if(showing){num(o.get(),"version",2);auto*display=json_object_new_object();put(display,"device","/dev/dri/card0");
#ifdef VX_TEST_HUNET
   auto*view=json_object_new_object();put(view,"directory",argv[7]);put(view,"sha256",argv[8]);json_object_object_add(display,"hunet",view);
#endif
   json_object_object_add(o.get(),"display",display);json_object_object_add(o.get(),"image",nullptr);json_object_object_add(o.get(),"p8",nullptr);
  }
  if(exporting){require(kind=="mosaic","Mosaic export never registers an HUNet lineage");num(o.get(),"version",2);for(const auto*k:{"display","image","p8"})json_object_object_add(o.get(),k,nullptr);auto*file=json_object_new_object();put(file,"build_id","HOST_FIXTURE_NOT_IMAGE");json_object_object_add(o.get(),"mosaic_export",file);}
  manifest=vxnn::json::dump(o.get());seal(manifest_path,manifest);seal(sha_path,digest_hex(vxnn::content_digest(manifest))+"\n");
  auto decoded=decode_native_installation(manifest,ctx.identity,vxnn::content_digest(manifest));require(bool(decoded),exporting?"U20 sealed Mosaic file export must bind to actual installation":"U20 sealed native installation must decode exact family and hardware before registry readiness");
  auto wrong=ctx.identity;wrong.bit[0]^=1;require(!decode_native_installation(manifest,wrong,vxnn::content_digest(manifest)),"foreign bit cannot reuse native install");require(!decode_native_installation(manifest,ctx.identity,digest(33)),"native install digest mismatch refused");
  if(mode!="red"){
   auto unsafe=std::string(owner)+"/unsafe-parent";require(!mkdir(unsafe.c_str(),0700)&&!chmod(unsafe.c_str(),0770),"own writable-ancestor negative fixture");seal(unsafe+"/installation.json",manifest);seal(unsafe+"/installation.sha256",digest_hex(vxnn::content_digest(manifest))+"\n");auto rejected=installed_applications_from(unsafe+"/installation.json",unsafe+"/installation.sha256")(ctx);require(!rejected&&rejected.error->code==EACCES&&rejected.error->category=="INSTALLATION_FILE"&&rejected.error->detail.find("parent")!=std::string::npos,"sealed installation cannot trust a writable ancestor");
   json_object_object_add(o.get(),"raw_program",json_object_new_int(9));auto bad=vxnn::json::dump(o.get());require(!decode_native_installation(bad,ctx.identity,vxnn::content_digest(bad)),"unknown raw program field cannot enter installation");json_object_object_del(o.get(),"raw_program");
   require(!chmod(manifest_path.c_str(),0600),"own mutable installation fixture");require(!installed_applications_from(manifest_path,sha_path)(ctx),"writable installation refused");require(!chmod(manifest_path.c_str(),0400),"restore sealed fixture");auto link=sha_path+"-link";require(!symlink(sha_path.c_str(),link.c_str()),"own seal symlink fixture");require(!installed_applications_from(manifest_path,link)(ctx),"symlink seal refused");require(!unlink(link.c_str()),"remove own link fixture");
  }
  auto installed=showing?installed_applications_for_host_test(manifest_path,sha_path,screen.ops())(ctx):installed_applications_from(manifest_path,sha_path)(ctx);if(!installed)std::fprintf(stderr,"NATIVE_INSTALLATION_FIRST_ERROR code=%d category=%s detail=%s\n",installed.error->code,installed.error->category.c_str(),installed.error->detail.c_str());return installed;
 };
 auto daemon=value(Daemon::start(cfg,builder,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){auto p=std::make_unique<Transport>(argv,hw.compatible);io=p.get();return p;}),"actual installed native service");require(opens==1&&!mosaic_owner_bridge_starts(io->bridge)&&!mosaic_owner_bridge_live(io->bridge),"production registry preflight never loads/starts GPU");
 auto models=rpc(daemon,owner,"{\"version\":1,\"command\":\"models\"}");require(vxnn::json::dump(models.get()).find("installed-nn")!=std::string::npos,"production registry is actually nonempty");const std::string key="\"version\":1,\"boot_id\":\"nativeboot\",\"client_id\":\"B\",\"request_id\":\"one\"";
 auto invalid=rpc(daemon,owner,"{"+key+",\"command\":\"submit\",\"entry_id\":\"installed-nn\",\"input_id\":\"sealed-input\",\"parameters\":{\"raw_program\":9}}");require(!vxnn::json::boolean(vxnn::json::member(invalid.get(),"ok"))&&!daemon->first_error()&&!mosaic_owner_bridge_starts(io->bridge),"invalid peer metadata rejected before reserving or preparing, without faulting service");
 auto submit=rpc(daemon,owner,"{"+key+",\"command\":\"submit\",\"entry_id\":\"installed-nn\",\"input_id\":\"sealed-input\",\"parameters\":{}}");require(vxnn::json::boolean(vxnn::json::member(submit.get(),"ok")),"native model submitted by actual B control RPC");
 // This bounds the host observer, including cold preparation and instrumented
 // original CPU work. It does not change the2000ms RPC or GPU/job deadlines.
 const auto observe_end=std::chrono::steady_clock::now()+std::chrono::seconds(120);bool done=false;std::string last_state;unsigned polls=0;
 while(!done&&std::chrono::steady_clock::now()<observe_end){auto status=rpc(daemon,owner,"{"+key+",\"command\":\"status\"}");auto*result=vxnn::json::member(status.get(),"result");last_state=vxnn::json::str(vxnn::json::member(result,"status"));require(last_state!="FAILED","native service must retain any first error");done=last_state=="COMPLETED";++polls;if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 if(!done){std::fprintf(stderr,"NATIVE_HOST_OBSERVER_EXPIRED polls=%u last_status=%s\n",polls,last_state.c_str());}require(done,"native source reaches common worker completed state");auto result=rpc(daemon,owner,"{"+key+",\"command\":\"result\"}");auto text=vxnn::json::dump(result.get());require(vxnn::json::boolean(vxnn::json::member(result.get(),"ok"))&&text.find("NOT_EVALUATED")!=std::string::npos&&text.find("\"gpu_raw_cycles\":null")!=std::string::npos,"host marker never promoted to GPU/numeric PASS");
 if(showing){
  require(screen.opened==1,"installed NN uses the sole native display owner");auto shown=rpc(daemon,owner,"{"+key+",\"command\":\"display\",\"artifact\":0}");const auto raw_reply=vxnn::json::dump(shown.get());if(!vxnn::json::boolean(vxnn::json::member(shown.get(),"ok")))std::fprintf(stderr,"INSTALLED_VIEW_RPC_ERROR %s\n",raw_reply.c_str());require(vxnn::json::boolean(vxnn::json::member(shown.get(),"ok"))&&screen.created,"U20 actual installed HUNet result must reach dedicated native view and KMS retirement");
  auto unchanged=rpc(daemon,owner,"{"+key+",\"command\":\"result\"}");const auto after=vxnn::json::dump(unchanged.get());require(after.find("NOT_EVALUATED")!=std::string::npos&&after.find("\"gpu_raw_cycles\":null")!=std::string::npos,"presentation cannot promote original producer numeric/cycle evidence");
 }
 if(exporting){
  const auto before=mosaic_owner_bridge_starts(io->bridge);const std::string command="{"+key+",\"command\":\"export\",\"artifact\":0,\"destination_id\":\"mosaic-file-v1\"}";
  auto exported=rpc(daemon,owner,command);const auto exported_text=vxnn::json::dump(exported.get());if(!vxnn::json::boolean(vxnn::json::member(exported.get(),"ok")))std::fprintf(stderr,"INSTALLED_EXPORT_RPC_ERROR %s\n",exported_text.c_str());
  require(vxnn::json::boolean(vxnn::json::member(exported.get(),"ok")),"U20 actual installed Mosaic result must export through registered file destination");
  auto*answer=vxnn::json::member(exported.get(),"result");auto path=vxnn::json::str(vxnn::json::member(vxnn::json::member(answer,"manifest"),"path"));require(path.find("..")==std::string::npos&&!path.empty()&&path[0]!='/',"only retained private relative paths returned");
  auto duplicate=rpc(daemon,owner,command);require(vxnn::json::dump(duplicate.get())==exported_text&&mosaic_owner_bridge_starts(io->bridge)==before,"file export replay does not run NN again or change artifacts");
  auto denied=rpc(daemon,owner,"{"+key+",\"command\":\"export\",\"artifact\":0,\"destination_id\":\"unregistered\"}");require(!vxnn::json::boolean(vxnn::json::member(denied.get(),"ok"))&&!daemon->first_error(),"bad file destination rejected without faulting original result");
  std::printf("NATIVE_INSTALLED_EXPORTED_MANIFEST=%s/outputs/%s\n",owner,path.c_str());
 }
 require(mosaic_owner_bridge_starts(io->bridge)==jobs&&mosaic_owner_bridge_acks(io->bridge)==jobs,"original native source actually dispatches every C-core job once");ok(daemon->shutdown(),"native service closes without client reap");require(!mosaic_owner_bridge_live(io->bridge),"native DATA/program cache retirement complete");ok(daemon->shutdown(),"native shutdown repeated without another device call");require(!daemon->first_error(),"no unreported native error");if(showing)require(screen.created==screen.destroyed,"installed NN display fully retires before shared resources");daemon.reset();puts(exporting?"U20_INSTALLED_MOSAIC_RPC_FILE_EXPORT_AND_RETIREMENT_PASS_HOST_MARKER_NOT_INFERENCE_GPU_DP gpu_raw_cycles=null":showing?"U20_INSTALLED_NN_RPC_NATIVE_VIEW_AND_RETIREMENT_PASS_HOST_MARKER_NOT_INFERENCE_GPU_DP gpu_raw_cycles=null":"U20_SEALED_INSTALLATION_ACTUAL_NN_RPC_SOURCE_OWNER_AND_RETIREMENT_PASS_HOST_MARKER_NOT_INFERENCE gpu_raw_cycles=null");
}

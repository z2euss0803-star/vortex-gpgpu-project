#include <vxunified/native_p8_service.hpp>
#include <vxunified/installation.hpp>
#include "nnrt/package/json.hpp"
#include <experimental/filesystem>
#include <sstream>
#include <set>
#include <sys/inotify.h>
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
#include <sys/time.h>
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

// Independent HOST CPU oracle mirror only. It never changes production scene
// state or claims that the injected host C marker is a rendered GPU frame.
struct Oracle {
 std::mutex mutex;std::shared_ptr<const P8Assets>assets;SceneState scene,rendered;
 std::atomic<bool>hold_first{false},first_waiting{false};
 std::shared_ptr<const TexturePackage>current,pending;Transport*io=nullptr;bool redraw=false,fault=false;
 static void prepare(void*p,unsigned id){auto&s=*static_cast<Oracle*>(p);std::lock_guard<std::mutex>lock(s.mutex);
  if((id==9||id==11)&&!s.scene.completed&&s.hold_first.load()){s.first_waiting=true;while(s.hold_first.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(id==9||id==11){s.rendered=s.scene;s.redraw=bool(s.pending);auto bank=s.pending?s.pending:s.current;if(bank)s.rendered.theme=bank->identity();auto frame=value(bank?s.assets->prepare_texture(s.rendered,bank,true,s.redraw):s.assets->prepare(s.rendered,true),"independent CPU reference for exact next P8 scene");p8_owner_bridge_replace_oracle(s.io->bridge,frame.buffers[4].data(),frame.buffers[4].size()*4,frame.buffers[0].data(),frame.buffers[1].data(),frame.buffers[0].size()*4);if(s.fault)p8_owner_bridge_corrupt_guard(s.io->bridge);
  }else{require(id==10||id==12,"original raster IDs only");s.scene=value(scene_after_frame(s.rendered,s.redraw?s.rendered.displayed_pose:s.rendered.pose,s.redraw,ns()),"independent original scene expectation");if(s.redraw){s.current=s.pending;s.pending.reset();}}
 }
};
void rpc_bytes(int fd,void*p,size_t n,bool sending){size_t done=0;while(done<n){auto r=sending?send(fd,static_cast<char*>(p)+done,n-done,MSG_NOSIGNAL):recv(fd,static_cast<char*>(p)+done,n-done,0);if(r<0&&errno==EINTR)continue;require(r>0,"exact public P8 control framing");done+=size_t(r);}}
vxnn::json::Ptr control_rpc(const std::shared_ptr<Daemon>&d,const std::string&root,std::string request){

 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);require(fd>=0,"one private P8 control peer");
 timeval timeout{5,0};require(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))&&!setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)),"bounded HOST RPC I/O even if server rejects request");
 sockaddr_un address{};address.sun_family=AF_UNIX;auto path=root+"/control.sock";require(path.size()<sizeof(address.sun_path),"bounded P8 socket path");memcpy(address.sun_path,path.c_str(),path.size()+1);require(!connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"connect actual installed P8 service");
 uint32_t size=htonl(uint32_t(request.size()));rpc_bytes(fd,&size,4,true);rpc_bytes(fd,&request[0],request.size(),true);
 // Queue the actual framed peer before the single bounded serve call. Native
 // idle polling may legitimately report no ready peer; test scheduling cannot
 // assume its former 50ms poll delay as an implicit synchronization barrier.
 int served=-1;std::thread server([&]{auto value=d->serve_one(2000);served=value?0:value.error->code;});
 rpc_bytes(fd,&size,4,false);size=ntohl(size);require(size&&size<=65536,"bounded P8 control reply");std::string response(size,'\0');rpc_bytes(fd,&response[0],response.size(),false);require(!close(fd),"close P8 control peer");server.join();require(!served,"actual P8 control request served");return vxnn::json::parse(response);
}
}
int main(int argc,char**argv){
 require(argc==5||argc==6||argc==9,"catalog programs assets output [controls|file-red|file source package package-id]");const bool joint=argc==6&&std::string(argv[5])=="joint";const bool file=argc==9;const bool permission=file&&std::string(argv[5])=="file-permission";const bool controls=argc>=6&&!joint;const bool file_red=controls&&std::string(argv[5])=="file-red";require(!controls||file_red||permission||std::string(argv[5])==(file?"file":"controls"),"registered P8 test mode");alarm(600);
 char root[]="/tmp/vx-native-p8-daemon-XXXXXX";require(mkdtemp(root),"private owner");printf("NATIVE_DAEMON_OWNER=%s\n",root);fflush(stdout);
 std::string file_source,file_package;std::shared_ptr<const TexturePackage>file_bank;
 if(file){namespace fs=std::experimental::filesystem;auto base=std::string(root)+"/texture-input";require(!mkdir(base.c_str(),0700),"private source import before sealed install");fs::copy(fs::path(argv[6]).parent_path(),base+"/source",fs::copy_options::recursive);fs::copy(argv[7],base+"/package",fs::copy_options::recursive);for(auto&entry:fs::recursive_directory_iterator(base)){require(!chmod(entry.path().c_str(),fs::is_directory(entry.status())?0700:0400),"seal only owned HOST fixture paths, keep production checks");}file_source=base+"/source/"+fs::path(argv[6]).filename().string();file_package=base+"/package";auto original=value(import_result(file_source),"independent frozen Mosaic file oracle");auto id=vxnn::json::parse(std::string("\"")+argv[8]+"\"");file_bank=value(make_texture(original,{file_package,vxnn::json::digest(id.get())}),"original Mosaic texture file, never wire downgrade");require(file_bank->source_kind()==TextureOrigin::MOSAIC_RESULT,"preserve neural file lineage");if(permission)require(!chmod((base+"/source").c_str(),0770),"explicit unsafe-parent negative fixture");}
 auto raw=read(argv[1]);require(raw.size()>=576,"catalog");vxl_v2_catalog_entry entry{};std::memcpy(&entry,raw.data()+64,512);
 DaemonConfig cfg;cfg.service.private_dir=root;cfg.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;cfg.boot_id="boot-p8-installed";cfg.evidence_dir=std::string(root)+"/evidence";cfg.config_sha=digest(13);cfg.identity.provider_flags=joint?4:2;cfg.identity.device_generation=1;cfg.identity.compatible=vxnn::content_digest(joint?"vortex,hw4-s8-joint-v1":"vortex,w4-graphics-p8-v1");std::copy_n(entry.hardware_contract,32,cfg.identity.hardware_contract.begin());std::copy_n(entry.bit,32,cfg.identity.bit.begin());std::copy_n(entry.xsa,32,cfg.identity.xsa.begin());
 require(!mkdir(cfg.evidence_dir.c_str(),0700),"private evidence");
 Screen screen;unsigned opens=0;Oracle oracle;oracle.hold_first=controls;oracle.assets=value(P8Assets::load(argv[3]),"independent P8 original assets");
 auto hex=[](const Digest&v){return vxnn::digest_hex(v);};const auto catalog=std::string(root)+"/catalog.bin";char assets[4096];require(realpath(argv[3],assets),"sealed installed assets path");{std::ofstream f(catalog,std::ios::binary);f.write(reinterpret_cast<const char*>(raw.data()),raw.size());f.close();require(bool(f)&&!chmod(catalog.c_str(),0400),"private immutable exact catalog");}Digest catalog_sha{};vxl_hunet_hash(raw.data(),raw.size(),catalog_sha.data());
 int reserve=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(reserve>=0,"reserve UDP");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);require(!bind(reserve,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"reserve ephemeral test port");socklen_t size=sizeof(address);require(!getsockname(reserve,reinterpret_cast<sockaddr*>(&address),&size),"actual sealed endpoint");const auto port=ntohs(address.sin_port);require(port,"nonzero endpoint");close(reserve);
 std::ostringstream text;text<<"{\"version\":2,\"hardware\":{\"hardware_contract\":\""<<hex(cfg.identity.hardware_contract)<<"\",\"bit\":\""<<hex(cfg.identity.bit)<<"\",\"xsa\":\""<<hex(cfg.identity.xsa)<<"\",\"compatible\":\""<<hex(cfg.identity.compatible)<<"\",\"provider_flags\":"<<cfg.identity.provider_flags<<"},\"catalog\":{\"path\":\""<<catalog<<"\",\"sha256\":\""<<hex(catalog_sha)<<"\"},\"nn\":[],\"display\":{\"device\":\"/dev/dri/card0\"},\"image\":null,\"p8\":{\"assets_directory\":\""<<assets<<"\",\"assets\":\""<<hex(oracle.assets->identity())<<"\",\"session\":77,\"display_base\":100,\"port\":"<<port<<",\"address\":\"127.0.0.1\",\"job_budget_ns\":60000000000}}";
 if(joint){
  auto table=read("vendor/image-studio/gpu_programs/tables-64KiB.bin");const auto table_path=std::string(root)+"/image-tables.bin";{std::ofstream f(table_path,std::ios::binary);f.write(reinterpret_cast<const char*>(table.data()),table.size());f.close();require(bool(f)&&!chmod(table_path.c_str(),0400),"seal own real original IMAGE table");}
  int image_fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(image_fd>=0,"joint IMAGE test endpoint");address.sin_port=0;require(!bind(image_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"reserve joint IMAGE endpoint");size=sizeof(address);require(!getsockname(image_fd,reinterpret_cast<sockaddr*>(&address),&size),"observe joint IMAGE endpoint");auto image_port=ntohs(address.sin_port);close(image_fd);require(image_port&&image_port!=port,"distinct sealed joint endpoints");
  auto tree=vxnn::json::parse(text.str());std::ostringstream media;media<<"{\"implementation\":\""<<hex(digest(7))<<"\",\"reference\":\""<<hex(digest(8))<<"\",\"readonly\":{\"path\":\""<<table_path<<"\",\"sha256\":\""<<hex(vxnn::content_digest(std::string(table.begin(),table.end())))<<"\"},\"session\":42,\"bundle\":\"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3\",\"display_base\":100,\"port\":"<<image_port<<",\"address\":\"127.0.0.1\",\"job_budget_ns\":60000000000}";auto image=vxnn::json::parse(media.str());json_object_object_add(tree.get(),"image",json_object_get(image.get()));
  // Test executable only: admit actual full frozen NN packages while the C
  // transport remains a P8-only CPU oracle. No NN/GPU inference is emulated.
  if(const char*models=std::getenv("HOST_JOINT_NN_MANIFEST")){auto bytes=read(models);auto rows=vxnn::json::parse(std::string(bytes.begin(),bytes.end()));json_object_object_add(tree.get(),"nn",json_object_get(rows.get()));auto exporter=vxnn::json::parse("{\"build_id\":\"HOST_JOINT_ALL_PREFLIGHT_NOT_IMAGE_OR_NN_EXECUTION\"}");json_object_object_add(tree.get(),"mosaic_export",json_object_get(exporter.get()));}
  text.str("");text.clear();text<<vxnn::json::dump(tree.get());
 }
 if(file){auto tree=vxnn::json::parse(text.str());auto*p8=vxnn::json::member(tree.get(),"p8");auto*rows=json_object_new_array();auto row=vxnn::json::parse("{}");json_object_object_add(row.get(),"id",json_object_new_string("mosaic-offline"));json_object_object_add(row.get(),"result_path",json_object_new_string(file_source.c_str()));auto source=read(file_source.c_str());auto source_sha=hex(vxnn::content_digest(std::string(source.begin(),source.end())));json_object_object_add(row.get(),"result_sha256",json_object_new_string(source_sha.c_str()));json_object_object_add(row.get(),"package_directory",json_object_new_string(file_package.c_str()));json_object_object_add(row.get(),"package_id",json_object_new_string(argv[8]));json_object_array_add(rows,json_object_get(row.get()));json_object_object_add(p8,"textures",rows);text.str("");text.clear();text<<vxnn::json::dump(tree.get());}
 const auto manifest=std::string(root)+"/installation.json",seal=manifest+".sha256";{std::ofstream f(manifest);f<<text.str();require(bool(f),"exact installed manifest");}{std::ofstream f(seal);f<<hex(vxnn::content_digest(text.str()))<<'\n';require(bool(f),"separate installation SHA");}
 require(!chmod(manifest.c_str(),0400)&&!chmod(seal.c_str(),0400),"immutable installed manifest and seal");
 int observer=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);require(observer>=0&&inotify_add_watch(observer,cfg.evidence_dir.c_str(),IN_CREATE|IN_MOVED_TO)>=0,"observe every immutable evidence publication before worker starts");
 auto installed=installed_applications_for_host_test(manifest,seal,screen.ops());
 auto created=Daemon::start(cfg,installed,[&](const std::string&){++opens;return Result<int>::success(::open("/dev/null",O_RDWR|O_CLOEXEC));},{},[&](int){auto io=std::make_unique<Transport>(argv[1],argv[2],cfg.identity.compatible);oracle.io=io.get();p8_owner_bridge_before_prepare(io->bridge,Oracle::prepare,&oracle);return io;});
 if(!created)std::fprintf(stderr,"INSTALLED_P8_STARTUP_ERROR code=%d category=%s detail=%s\n",created.error->code,created.error->category.c_str(),created.error->detail.c_str());
 require(bool(created),"U20 sealed P8 installation must bind native P8 without an S8 NN library");auto daemon=*created.value;require(opens==1&&screen.opened==1,"single device/display owner on actual installed P8 path");
 if(joint){auto switched=control_rpc(daemon,root,"{\"version\":1,\"command\":\"mode\",\"boot_id\":\"boot-p8-installed\",\"mode\":\"p8\"}");require(vxnn::json::boolean(vxnn::json::member(switched.get(),"ok")),"joint sealed IMAGE/P8 service uses one actual mode RPC");}
 if(controls){
  const auto fence_until=ns()+UINT64_C(5000000000);while(ns()<fence_until&&!oracle.first_waiting.load()){ok(daemon->serve_one(0),"HOST first-frame observation");std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(oracle.first_waiting,"HOST oracle reached first prepare before GPU marker");
  const std::string key="\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"front\"";auto early=control_rpc(daemon,root,"{"+key+",\"command\":\"submit\",\"entry_id\":\"p8-control\",\"input_id\":\"f\",\"parameters\":{}}");require(!vxnn::json::boolean(vxnn::json::member(early.get(),"ok"))&&!daemon->first_error()&&!p8_owner_bridge_starts(oracle.io->bridge),"B controls cannot bypass original startup100 or start a GPU job");oracle.hold_first=false;
 }
 std::set<std::string>seen;unsigned n=0;
 auto frames=[&](){
  alignas(inotify_event) char events[16384];
  for(;;){
   auto bytes=::read(observer,events,sizeof(events));
   if(bytes<0&&errno==EINTR)continue;
   if(bytes<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
   require(bytes>0,"bounded evidence event read");
   size_t offset=0;
   while(offset<size_t(bytes)){
    require(size_t(bytes)-offset>=sizeof(inotify_event),"complete evidence event header");auto*event=reinterpret_cast<const inotify_event*>(events+offset);
    require(event->len<=size_t(bytes)-offset-sizeof(inotify_event),"bounded evidence event name");require(!(event->mask&IN_Q_OVERFLOW),"observer must not lose original evidence events");
    std::string name(event->name,strnlen(event->name,event->len));offset+=sizeof(inotify_event)+event->len;
    if(name.size()!=69||name.substr(64)!=".json")continue;
    require(std::all_of(name.begin(),name.begin()+64,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),"original hash-named evidence required");
    if(!seen.insert(name).second)continue;
    std::ifstream input(cfg.evidence_dir+"/"+name);require(bool(input),"open published event");std::string body{std::istreambuf_iterator<char>(input),{}};require(vxnn::digest_hex(vxnn::content_digest(body))==name.substr(0,64),"published event bytes match original filename hash");
    auto row=vxnn::json::parse(body);
    if(vxnn::json::str(vxnn::json::member(row.get(),"kind"))=="native-frame"){++n;require(!vxnn::json::member(row.get(),"gpu_raw_cycles"),"HOST cycles remain null");}
   }
  }
  return n;
 };
 auto deadline=ns()+UINT64_C(300000000000);while(ns()<deadline&&frames()<100){auto r=daemon->serve_one(0);if(!r)std::fprintf(stderr,"INSTALLED_P8_RUN_ERROR %d %s\n",r.error->code,r.error->detail.c_str());ok(r,"installed daemon original frame execution");require(!daemon->first_error(),"no hidden native error");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 require(frames()==100,"all original startup100 retained in installed P8 path");puts("P8_PROGRESS original_startup100_retired");fflush(stdout);
 if(file_red||file){auto available=control_rpc(daemon,root,"{\"version\":1,\"command\":\"scenes\"}");require(vxnn::json::dump(available.get()).find("p8-texture")!=std::string::npos,"U20 installed P8 must expose sealed Mosaic texture file admission in common service");}
 if(controls){
  auto available=control_rpc(daemon,root,"{\"version\":1,\"command\":\"scenes\"}");require(vxnn::json::dump(available.get()).find("p8-control")!=std::string::npos,"U20 installed P8 must register B controls in the common request service");
  // Advance only the independent HOST oracle. Production scene state must
  // change exclusively through the actual registered request below.
  {std::lock_guard<std::mutex>lock(oracle.mutex);oracle.scene=value(scene_control(oracle.scene,'f',ns()),"independent original P8 front-view reference");}
  const auto starts=p8_owner_bridge_starts(oracle.io->bridge);const std::string key="\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"front\"";
  const std::string command="{"+key+",\"command\":\"submit\",\"entry_id\":\"p8-control\",\"input_id\":\"f\",\"parameters\":{}}";auto submitted=control_rpc(daemon,root,command);require(vxnn::json::boolean(vxnn::json::member(submitted.get(),"ok")),"B front-view command admitted without raw GPU descriptors");
  bool done=false;const auto until=ns()+UINT64_C(120000000000);while(ns()<until&&!done){auto status=control_rpc(daemon,root,"{"+key+",\"command\":\"status\"}");require(vxnn::json::boolean(vxnn::json::member(status.get(),"ok")),"P8 control status");auto value=vxnn::json::str(vxnn::json::member(vxnn::json::member(status.get(),"result"),"status"));require(value!="FAILED","P8 control request retains any first error");done=value=="COMPLETED";if(!done)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(done,"common CPU worker completes original P8 control");
  auto result=control_rpc(daemon,root,"{"+key+",\"command\":\"result\"}");auto text=vxnn::json::dump(result.get());require(text.find("NOT_EVALUATED")!=std::string::npos&&text.find("\"gpu_raw_cycles\":null")!=std::string::npos,"CPU control receipt never invents GPU rendering or precision evidence");
  auto payload=vxnn::json::parse(vxnn::json::str(vxnn::json::member(vxnn::json::member(result.get(),"result"),"raw_legacy_json")));require(vxnn::json::has(payload.get(),"displayed_pose")&&vxnn::json::has(payload.get(),"gallery_due_ns"),"P8 control snapshot must retain display pose and gallery deadline");require(vxnn::json::boolean(vxnn::json::member(payload.get(),"applied"))&&vxnn::json::u64(vxnn::json::member(payload.get(),"completed"))==100&&vxnn::json::u64(vxnn::json::member(payload.get(),"displayed_pose"))==99,"command receipt is not the later presented frame");
  // HOST observer only; the real per-GPU deadline is unchanged.
  const auto frame_until=ns()+UINT64_C(30000000000);while(ns()<frame_until&&frames()<101){ok(daemon->serve_one(0),"front frame follows original checked native scene path");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(frames()!=101)std::fprintf(stderr,"P8_CONTROL_FRAME_OBSERVER frames=%u starts=%u before=%u cpu_control_completed=1\n",frames(),p8_owner_bridge_starts(oracle.io->bridge),starts);
  require(frames()==101&&p8_owner_bridge_starts(oracle.io->bridge)==starts+2,"registered front command produces exactly one original frame after startup100");
  auto repeated=control_rpc(daemon,root,command);require(vxnn::json::boolean(vxnn::json::member(repeated.get(),"ok"))&&p8_owner_bridge_starts(oracle.io->bridge)==starts+2,"idempotent control replay does not rotate or render again");
  const std::string unsupported="\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"unsupported-view\"";auto q=control_rpc(daemon,root,"{"+unsupported+",\"command\":\"submit\",\"entry_id\":\"p8-control\",\"input_id\":\"q\",\"parameters\":{}}");require(vxnn::json::boolean(vxnn::json::member(q.get(),"ok")),"known UI command uses current original object rules");bool handled=false;while(ns()<until&&!handled){auto status=control_rpc(daemon,root,"{"+unsupported+",\"command\":\"status\"}");auto state=vxnn::json::str(vxnn::json::member(vxnn::json::member(status.get(),"result"),"status"));require(state!="FAILED","unsupported object view is a handled command error, not GPU failure");handled=state=="COMPLETED";if(!handled)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(handled,"unsupported UI command receipt retained");auto q_result=control_rpc(daemon,root,"{"+unsupported+",\"command\":\"result\"}");auto q_payload=vxnn::json::parse(vxnn::json::str(vxnn::json::member(vxnn::json::member(q_result.get(),"result"),"raw_legacy_json")));require(!vxnn::json::boolean(vxnn::json::member(q_payload.get(),"applied"))&&vxnn::json::u64(vxnn::json::member(q_payload.get(),"command_error"))==EOPNOTSUPP&&p8_owner_bridge_starts(oracle.io->bridge)==starts+2,"original unsupported diagonal error retained with no new GPU call");
  auto bad=control_rpc(daemon,root,"{\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"bad-key\",\"command\":\"submit\",\"entry_id\":\"p8-control\",\"input_id\":\"raw-program\",\"parameters\":{}}");require(!vxnn::json::boolean(vxnn::json::member(bad.get(),"ok")),"unregistered key rejected before preparation");
  require(!daemon->first_error(),"public P8 controls have no hidden error");
 }
 if(file){
  const auto starts=p8_owner_bridge_starts(oracle.io->bridge);{std::lock_guard<std::mutex>lock(oracle.mutex);oracle.pending=file_bank;}
  const std::string key="\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"mosaic-file\"";const auto command="{"+key+",\"command\":\"submit\",\"entry_id\":\"p8-texture\",\"input_id\":\"mosaic-offline\",\"parameters\":{}}";auto accepted=control_rpc(daemon,root,command);require(vxnn::json::boolean(vxnn::json::member(accepted.get(),"ok")),"public sealed Mosaic file command accepted without paths or program handles");
  bool completed=false;const auto until=ns()+UINT64_C(120000000000);while(ns()<until&&!completed){auto status=control_rpc(daemon,root,"{"+key+",\"command\":\"status\"}");require(vxnn::json::boolean(vxnn::json::member(status.get(),"ok")),"Mosaic file command status");auto name=vxnn::json::str(vxnn::json::member(vxnn::json::member(status.get(),"result"),"status"));if(permission&&name=="FAILED"){require(vxnn::json::u32(vxnn::json::member(vxnn::json::member(status.get(),"result"),"preparation_error_code"))==EACCES,"original unsafe-parent error query retained");completed=true;}else{require(name!="FAILED","no hidden file admission error");completed=name=="COMPLETED";}if(!completed)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(completed,"one common CPU file command retires");
  if(permission){require(daemon->first_error()&&daemon->first_error().value->code==EACCES&&p8_owner_bridge_starts(oracle.io->bridge)==starts,"failed preparation keeps first error and never submits texture GPU work");auto denied=control_rpc(daemon,root,"{\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"after-file-error\",\"command\":\"submit\",\"entry_id\":\"p8-control\",\"input_id\":\"f\",\"parameters\":{}}");require(!vxnn::json::boolean(vxnn::json::member(denied.get(),"ok")),"failed owner cannot admit new control work");ok(daemon->shutdown(),"failed preparation has no active GPU and retires existing display");require(screen.created==screen.destroyed,"negative fixture scanout fully retired");require(!close(observer),"negative observer close");puts("U20_INSTALLED_P8_PERMISSION_ERROR_QUERYABLE_NEW_ADMISSION_CLOSED_NO_TEXTURE_GPU_PASS_HOST gpu_raw_cycles=null");return 0;}

  auto ack=control_rpc(daemon,root,"{"+key+",\"command\":\"result\"}");auto payload=vxnn::json::parse(vxnn::json::str(vxnn::json::member(vxnn::json::member(ack.get(),"result"),"raw_legacy_json")));require(vxnn::json::str(vxnn::json::member(payload.get(),"phase"))=="FILE_ACCEPTED"&&!vxnn::json::boolean(vxnn::json::member(payload.get(),"applied")),"file reception acknowledgment cannot claim rendered APPLIED");require(vxnn::json::str(vxnn::json::member(payload.get(),"clock_domain"))=="HOST_MONOTONIC"&&vxnn::json::u64(vxnn::json::member(payload.get(),"source_sealed_read_ns"))&&vxnn::json::u64(vxnn::json::member(payload.get(),"source_import_ns"))&&vxnn::json::u64(vxnn::json::member(payload.get(),"package_verify_ns")),"source sealing/import/package time recorded in explicit HOST clock domain, not physical GPU timing");
  bool applied=false;while(ns()<until&&!applied){auto status=control_rpc(daemon,root,"{"+key+",\"command\":\"export\",\"artifact\":0,\"destination_id\":\"texture-status-v1\"}");if(vxnn::json::boolean(vxnn::json::member(status.get(),"ok"))){auto*value=vxnn::json::member(status.get(),"result");applied=vxnn::json::str(vxnn::json::member(value,"phase"))=="APPLIED";if(applied){require(vxnn::json::boolean(vxnn::json::member(value,"applied"))&&vxnn::json::str(vxnn::json::member(value,"source_kind"))=="MOSAIC_RESULT"&&vxnn::json::u32(vxnn::json::member(value,"source_scope"))==uint32_t(file_bank->source_scope())&&vxnn::json::digest(vxnn::json::member(value,"source_result_sha256"))==file_bank->source_result_sha()&&vxnn::json::digest(vxnn::json::member(value,"source_tensor_sha256"))==file_bank->source_tensor_sha()&&!vxnn::json::member(value,"gpu_raw_cycles"),"APPLIED retains exact source identities/scope and null offline GPU cycles");}}require(!daemon->first_error(),"original frame guard/CRC/retirement checks stay active");if(!applied)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  require(applied&&frames()==102&&p8_owner_bridge_starts(oracle.io->bridge)==starts+2,"original Mosaic texture redraw completes exactly two checked GPU protocol jobs and retires old KMS front");auto repeated=control_rpc(daemon,root,command);require(vxnn::json::boolean(vxnn::json::member(repeated.get(),"ok"))&&p8_owner_bridge_starts(oracle.io->bridge)==starts+2,"file replay cannot re-import or re-render");
  auto bad=control_rpc(daemon,root,"{\"version\":1,\"boot_id\":\"boot-p8-installed\",\"client_id\":\"B\",\"request_id\":\"file-path\",\"command\":\"submit\",\"entry_id\":\"p8-texture\",\"input_id\":\"mosaic-offline\",\"parameters\":{\"path\":\"/tmp/anything\"}}");require(!vxnn::json::boolean(vxnn::json::member(bad.get(),"ok"))&&!daemon->first_error(),"wire paths rejected before file preparation");
 }
 ok(daemon->shutdown(),"installed shutdown quiesces ingress/worker/KMS/program lifetime");require(screen.created==screen.destroyed,"every installed fake scanout retired");require(!close(observer),"close HOST-only evidence observer");
 if(file)puts("U20_INSTALLED_P8_PUBLIC_MOSAIC_FILE_SHARED_QUEUE_LINEAGE_ORIGINAL_RENDER_AND_RETIREMENT_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");
 if(controls)puts("U20_INSTALLED_P8_PUBLIC_COMMON_QUEUE_CPU_CONTROL_AND_ORIGINAL_FRAME_RETIREMENT_PASS_HOST_ORACLE_NOT_GPU_DP gpu_raw_cycles=null");
 puts("U20_INSTALLED_P8_SEALED_STARTUP100_COMMON_DAEMON_FILES_DISPLAY_AND_SHUTDOWN_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;
}

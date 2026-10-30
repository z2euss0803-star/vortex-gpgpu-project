#include "session.h"
#include "reference.h"
#include <vxl/s8_client.h>
#include <vxl/s8_runtime.h>
#include <vxl/hunet.h>
#include <nlohmann_json.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
using Json=nlohmann::json;
using Bytes=std::vector<uint8_t>;
static volatile sig_atomic_t interrupted;
static void stop(int){interrupted=1;}
struct Signals {
 struct sigaction old_int{},old_term{};bool installed=false;
 Signals(){struct sigaction handler{};handler.sa_handler=stop;sigemptyset(&handler.sa_mask);interrupted=0;if(sigaction(SIGINT,&handler,&old_int))throw -errno;if(sigaction(SIGTERM,&handler,&old_term)){int e=-errno;sigaction(SIGINT,&old_int,nullptr);throw e;}installed=true;}
 ~Signals(){if(installed){sigaction(SIGINT,&old_int,nullptr);sigaction(SIGTERM,&old_term,nullptr);}}
};
static void need(bool value,int error=-EINVAL){if(!value)throw error;}
static std::string hex(const uint8_t *p,size_t n){static const char d[]="0123456789abcdef";std::string s;for(size_t i=0;i<n;++i){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
static std::string hash(const void *data,size_t n){uint8_t out[32];vxl_hunet_hash(data,n,out);return hex(out,32);}
static uint32_t decimal(const std::string &s){need(!s.empty()&&s.size()<=10);uint64_t n=0;for(char c:s){need(c>='0'&&c<='9');n=n*10+unsigned(c-'0');need(n<=0xffffffff);}return uint32_t(n);}
static Bytes unhex(const std::string &s){need(s.size()%2==0);Bytes out;for(size_t i=0;i<s.size();i+=2){auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};int a=digit(s[i]),b=digit(s[i+1]);need(a>=0&&b>=0);out.push_back(uint8_t(a*16+b));}return out;}
int s8_parse_options(int argc,const char *const *argv,S8Options *out){try{
 need(out);S8Options o;std::map<std::string,std::string> options;
 for(int i=1;i<argc;i+=2){need(i+1<argc);std::string key=argv[i];need(!options.count(key));need(key=="--manifest"||key=="--catalog"||key=="--case"||key=="--run-id"||key=="--evidence"||key=="--core-mask"||key=="--owner-warp"||key=="--phase");options[key]=argv[i+1];}
 for(const char *k:{"--manifest","--catalog","--case","--run-id","--evidence"})need(options.count(k));
 o.manifest=options["--manifest"];o.catalog=options["--catalog"];o.evidence=options["--evidence"];o.run_id=options["--run-id"];o.case_id=decimal(options["--case"]);
 if(options.count("--core-mask"))o.core_mask=decimal(options["--core-mask"]);
 if(options.count("--owner-warp"))o.owner_warp=decimal(options["--owner-warp"]);
 if(options.count("--phase"))o.flags=decimal(options["--phase"]);
 need(o.core_mask==1||o.core_mask==2||o.core_mask==4||o.core_mask==8||o.core_mask==15);need(o.owner_warp<4&&o.flags<=1);need(o.run_id.size()==32);auto run=unhex(o.run_id);need(std::any_of(run.begin(),run.end(),[](uint8_t v){return v!=0;}));need(!o.manifest.empty()&&!o.catalog.empty()&&!o.evidence.empty()&&o.case_id);*out=o;return 0;
 }catch(int e){return e;}catch(...){return -EINVAL;}}
int s8_persist(int directory,const char *name,const void *data,size_t bytes){
 if(!name||!data||!bytes||strchr(name,'/')||strchr(name,'\\')||!strcmp(name,".")||!strcmp(name,".."))return -EINVAL;
 int fd=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -errno;
 int rc=0;size_t off=0;while(off<bytes){ssize_t n=write(fd,static_cast<const uint8_t *>(data)+off,bytes-off);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}off+=size_t(n);}
 if(!rc&&fsync(fd))rc=-errno;
 if(close(fd)&&!rc)rc=-errno;
 if(!rc&&fsync(directory))rc=-errno;
 return rc;
}
static Bytes read_fd(int fd,size_t limit){struct stat s{};need(!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_size>=0&&uint64_t(s.st_size)<=limit);Bytes data(size_t(s.st_size));size_t off=0;while(off<data.size()){ssize_t n=read(fd,data.data()+off,data.size()-off);if(n<0&&errno==EINTR)continue;need(n>0,-EIO);off+=size_t(n);}return data;}
static Bytes file(const std::string &path,size_t limit=64*1024*1024){int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);need(fd>=0,-errno);try{auto data=read_fd(fd,limit);close(fd);return data;}catch(...){close(fd);throw;}}
static Bytes asset(const std::string &root,const std::string &path,size_t limit=64*1024*1024){
 need(!path.empty()&&path[0]!='/'&&path.find('\\')==std::string::npos);int fd=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);need(fd>=0,-errno);
 try{size_t at=0;for(;;){size_t slash=path.find('/',at);std::string part=path.substr(at,slash==std::string::npos?slash:slash-at);need(!part.empty()&&part!="."&&part!="..");int child=openat(fd,part.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|(slash==std::string::npos?0:O_DIRECTORY));need(child>=0,-errno);close(fd);fd=child;if(slash==std::string::npos)break;at=slash+1;}auto data=read_fd(fd,limit);close(fd);return data;}catch(...){close(fd);throw;}
}
static Json parse(const Bytes &data){std::map<int,std::set<std::string>> keys;return Json::parse(data.begin(),data.end(),[&](int depth,Json::parse_event_t event,Json &value){if(event==Json::parse_event_t::object_start)keys[depth+1].clear();if(event==Json::parse_event_t::key)need(keys[depth].insert(value.get<std::string>()).second);if(event==Json::parse_event_t::object_end)keys.erase(depth+1);return true;});}
static std::string parent(const std::string &s){auto p=s.rfind('/');need(p!=std::string::npos);return s.substr(0,p);}
static uint64_t user_now(){timespec t{};need(!clock_gettime(CLOCK_MONOTONIC_RAW,&t)&&t.tv_sec>=0&&t.tv_nsec>=0&&t.tv_nsec<1000000000,-EIO);return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);}
static unsigned compat_size(uint32_t id){return id==2?256:id==3?128:id==4?20:id==5?16:0;}
static Bytes trace_bytes(const std::string &name){
 need(name=="binding.bin"||name=="jobs.csv"||name=="operations.csv");int fd=open(("/sys/kernel/debug/vxl_s8/"+name).c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);need(fd>=0,-errno);
 Bytes data;uint8_t buffer[4096];int rc=0;for(;;){ssize_t n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0){rc=-errno;break;}if(!n)break;if(data.size()+size_t(n)>4*1024*1024){rc=-EOVERFLOW;break;}data.insert(data.end(),buffer,buffer+n);}close(fd);need(!rc,rc);need(!data.empty());return data;
}
struct Session {
 S8Options options;Json manifest,layout,catalog,files=Json::array(),events=Json::array(),errors=Json::array();int directory=-1;bool clock_bad=false;vxl_s8_client client{};
 std::array<Bytes,53> initial;std::map<uint32_t,Bytes> capture;vxl_s8_binding_v1 binding{};vxl_s8_job_v1 submitted{};
 Session(){client.fd=-1;}
 ~Session(){vxl_s8_client_unbind(&client);if(client.fd>=0)close(client.fd);if(directory>=0)close(directory);}
 void save(const std::string &name,const void *data,size_t bytes){uint64_t a=user_now();int rc=s8_persist(directory,name.c_str(),data,bytes);need(!rc,rc);need(files.size()<512,-ENOSPC);files.push_back(Json{{"path",name},{"bytes",bytes},{"sha256",hash(data,bytes)}});events.push_back(Json{{"name","persist:"+name},{"clock","CLOCK_MONOTONIC_RAW"},{"begin_ns",a},{"end_ns",user_now()},{"result",0}});}
 void json(const std::string &name,const Json &data){auto s=data.dump(2)+"\n";save(name,s.data(),s.size());}
 void trace(const std::string &name,bool post){auto data=trace_bytes(name);if(name=="binding.bin")need(data.size()==512&&!memcmp(data.data(),&binding,512),-ESTALE);save((post?"post-":"")+name,data.data(),data.size());}
 static int call(void *,int fd,vxl_u32 cmd,vxl_request *r){if(interrupted&&(cmd==VXL_SUBMIT||cmd==VXL_COPY_TO))return -EINTR;int rc=ioctl(fd,cmd,r)<0?-errno:0;return !rc&&interrupted&&cmd==VXL_WAIT?-EINTR:rc;}
 static int saved(void *p,const char *name,const void *data,vxl_u32 bytes){auto &s=*static_cast<Session *>(p);try{s.save(name,data,bytes);if(!strcmp(name,"job.bin")){need(bytes==128);memcpy(&s.submitted,data,128);}if(!strncmp(name,"capture-",8)){unsigned index=0,gpu=0;need(sscanf(name,"capture-%u-%x.bin",&index,&gpu)==2);need(!s.capture.count(gpu));s.capture[gpu]=Bytes(static_cast<const uint8_t *>(data),static_cast<const uint8_t *>(data)+bytes);}return 0;}catch(int e){s.errors.push_back(Json{{"operation","save"},{"path",name},{"error",e}});return e;}catch(...){return -EIO;}}
 static int traced(void *p,const char *name,int post){auto &s=*static_cast<Session *>(p);try{s.trace(name,post!=0);return 0;}catch(int e){s.errors.push_back(Json{{"operation","trace"},{"path",name},{"error",e}});return e;}catch(...){return -EIO;}}
 static vxl_u64 now(void *p){auto &s=*static_cast<Session *>(p);try{return user_now();}catch(...){s.clock_bad=true;return 0;}}
 static int event(void *p,const char *name,vxl_u64 a,vxl_u64 b,int rc){auto &s=*static_cast<Session *>(p);try{need(!s.clock_bad&&a<=b,-ERANGE);need(s.events.size()<1024,-ENOSPC);s.events.push_back(Json{{"name",name},{"clock","CLOCK_MONOTONIC_RAW"},{"begin_ns",a},{"end_ns",b},{"result",rc}});return 0;}catch(int e){return e;}catch(...){return -EIO;}}
 static int sealed(void *p,int post){auto &s=*static_cast<Session *>(p);try{if(!post)s.validate();s.json(post?"user-timing-post.json":"user-timing.json",s.events);
#ifdef S8_SESSION_TEST_ONLY
 const char *scope="SOFTWARE_TEST_ONLY";
#else
 const char *scope="REAL_DRIVER_CAPTURE_PENDING_INDEPENDENT_AUDIT";
#endif
 s.json(post?"post-manifest.json":"case-manifest.json",Json{{"version",1},{"scope",scope},{"profile",5},{"program",11},{"case_id",s.submitted.case_id},{"generation",s.submitted.generation},{"sequence",s.submitted.sequence},{"job_id",s.client.job_id},{"run_id",s.options.run_id},{"flags",s.options.flags},{"binding_sha256",hash(&s.binding,512)},{"gpu_seconds",nullptr},{"files",s.files},{"errors",s.errors}});return 0;}catch(int e){return e;}catch(...){return -EIO;}}
 void validate();
};
static std::vector<std::string> split(const std::string &s,char delim){std::vector<std::string> out;size_t at=0;for(;;){auto p=s.find(delim,at);out.push_back(s.substr(at,p==std::string::npos?p:p-at));if(p==std::string::npos)break;at=p+1;}return out;}
void Session::validate(){
 need(capture.size()==182);const auto &j=submitted;uint32_t family=j.case_id>>8;
 for(const auto &part:capture){uint32_t gpu=part.first;const auto &raw=part.second;unsigned obj=53;for(unsigned i=0;i<53;++i)if(gpu>=client.objects[i].gpu&&uint64_t(gpu)+raw.size()<=uint64_t(client.objects[i].gpu)+client.objects[i].bytes){obj=i;break;}need(obj<53);size_t offset=gpu-client.objects[obj].gpu;
  if(obj<34||obj==52){need(!memcmp(raw.data(),initial[obj].data()+offset,raw.size()),-EDOM);continue;}
  if(obj==50){need(raw.size()==4096);uint32_t tls=layout.at("elf").at("tls_base"),stride=layout.at("elf").at("tbss_stride");for(unsigned h=0;h<64;++h){vxl_s8_identity_v1 id;memcpy(&id,raw.data()+64+h*32,32);uint32_t top=0xffff0000u-h*8192;need(id.core==h/16&&id.warp==h%16/4&&id.lane==h%4&&id.hart==h&&!(id.sp&15)&&id.sp<=top&&id.sp>=top-8128&&id.tp==tls+h*stride&&id.stack_tag==(VXL_S8_STACK_TAG^h)&&id.tls_tag==(VXL_S8_TLS_TAG^h),-ESTALE);}for(unsigned core=0;core<4;++core){vxl_s8_core_done done;memcpy(&done,raw.data()+0x840+core*32,32);need(done.magic==VXL_S8_CORE_MAGIC&&done.version==1&&done.core==core&&done.generation==j.generation&&done.sequence==j.sequence&&!done.error&&done.harts==16&&done.done==VXL_S8_DONE);}
   vxl_s8_global_done global;memcpy(&global,raw.data()+0x8c0,64);need(global.magic==VXL_S8_GLOBAL_MAGIC&&global.version==1&&global.bytes==64&&global.generation==j.generation&&global.sequence==j.sequence&&global.cores==15&&global.harts==64&&global.done==VXL_S8_DONE&&!memcmp(global.run_id,j.run_id,16)&&!memcmp(global.binding_tag,j.binding_tag,16));for(unsigned i=0;i<64;++i)need(raw[i]==initial[obj][i]&&raw[4032+i]==initial[obj][4032+i]);for(unsigned i=0x900;i<4032;++i)need(raw[i]==initial[obj][i]);continue;}
  if(obj==51){for(unsigned i=0;i<4096;++i)if(i<64||i>=320)need(raw[i]==initial[obj][i]);for(unsigned core=0;core<4;++core){vxl_s8_sync sync;memcpy(&sync,raw.data()+64+core*64,64);need(!sync.abort);for(auto v:sync.reserved)need(v==0);if(family!=206&&family!=207)need(!sync.phase_seq&&!sync.owner_ready&&!sync.contender_done);}continue;}
  unsigned slot=obj-34,core=slot/4,warp=slot%4;bool selected=(j.core_mask&(1u<<core))&&warp==j.owner_warp;
  if(!selected||j.case_id==1){need(raw==initial[obj],-EDOM);continue;}
  vxl_s8_result_v1 m;memcpy(&m,raw.data()+0x3c0,256);need(m.magic==VXL_S8_RESULT_MAGIC&&m.version==1&&m.bytes==256&&m.case_id==j.case_id&&!m.sw_status&&m.generation==j.generation&&m.sequence==j.sequence&&m.core==core&&m.warp==warp&&m.owner_warp==warp&&m.k==j.k&&m.flags==j.flags&&m.done==VXL_S8_DONE&&!memcmp(m.run_id,j.run_id,16)&&!memcmp(m.binding_tag,j.binding_tag,16),-ESTALE);for(auto v:m.reserved)need(!v);
  bool compat=j.case_id>=2&&j.case_id<=5,negative=family>=201&&family<=209,primary=negative&&family!=201&&family!=208&&family!=209;
  std::vector<std::pair<unsigned,unsigned>> mutable_spans={{0x3c0,256}};
  auto allow=[&](unsigned off,unsigned n){mutable_spans.push_back({off,n});};
  if(compat){need(!s8_compare_compat(j.case_id,raw.data()+64,compat_size(j.case_id)),-EDOM);allow(64,compat_size(j.case_id));need(m.format==(j.case_id==5?4:j.case_id)&&m.valid_bits==S8_RAW_VALID);}
  else{allow(0x1c0,512);if(!negative||primary){allow(64,384);allow(0x4c0,512);}if(negative){allow(0x900,384);allow(0xa80,512);}if(j.flags)allow(0x6c0,512);
   auto compare=[&](unsigned k,unsigned r,unsigned d){int64_t expected[64],decoded[64];const auto *a=reinterpret_cast<const int16_t *>(initial[2+slot].data()+64),*b=reinterpret_cast<const int16_t *>(initial[18+slot].data()+64);need(!s8_reference(a,b,k,expected)&&!s8_unpack48(raw.data()+r,decoded));need(!memcmp(expected,decoded,512)&&!memcmp(expected,raw.data()+d,512),-EDOM);};
   if(!negative||primary)compare(j.k,64,0x4c0);
   if(negative)compare(1,0x900,0xa80);
  }
  for(unsigned i=0;i<4096;++i){bool writable=false;for(auto span:mutable_spans)if(i>=span.first&&i<span.first+span.second){writable=true;break;}if(!writable)need(raw[i]==initial[obj][i],-EDOM);}
 }
 auto data=file(options.evidence+"/jobs.csv",4*1024*1024);auto lines=split(std::string(data.begin(),data.end()),'\n');need(lines.size()>=3&&lines[0].find("#VXL_S8_TRACE_V1 ")==0);for(const char *field:{"overflow=0","active=0","failed=0","abandoned=0"})need(lines[0].find(field)!=std::string::npos);
 auto keys=split(lines[1],',');std::map<std::string,std::string> row;bool found=false;for(size_t line=2;line<lines.size();++line)if(!lines[line].empty()){auto values=split(lines[line],',');need(values.size()==keys.size());std::map<std::string,std::string> candidate;for(size_t i=0;i<keys.size();++i)need(candidate.emplace(keys[i],values[i]).second);if(candidate.at("job_id")==std::to_string(client.job_id)&&candidate.at("generation")==std::to_string(j.generation)&&candidate.at("sequence")==std::to_string(j.sequence)){need(!found);row=candidate;found=true;}}
 need(found);for(const char *field:{"result","error_class","errors","pending_w","pending_r","tex_pending","timing_invalid","exit0","exit1","exit2","exit3"})need(row.at(field)=="0");
 for(const char *field:{"elapsed_valid","pre_ack","pre_ack_ready","ack_ready","ack_idle","guard_valid","immutable_valid","input_valid","ack_valid","identity_valid","metadata_valid","clock"})need(row.at(field)=="1");
 need(row.at("pre_ack_idle")=="0"&&((decimal(row.at("status"))>>8)&7)==5&&(decimal(row.at("status"))&2)&&!(decimal(row.at("status"))&4));
 need(row.at("state")=="3"&&row.at("elapsed_address")==std::to_string(0xa0000028u)&&row.at("watchdog")=="4000000000"&&row.at("exit_valid")=="15");uint32_t cycles=decimal(row.at("elapsed_raw"));need(cycles&&cycles<4000000000u&&row.at("elapsed_u64")==row.at("elapsed_raw"));need(row.at("run_id")==options.run_id&&row.at("binding_tag")==hex(j.binding_tag,16));
 uint32_t cdma=decimal(row.at("cdma_status"));need((cdma&2)&&!(cdma&0x4770)&&decimal(row.at("jobs"))==uint32_t(decimal(row.at("jobs_before"))+1));
 json("gpu-raw.json",Json{{"elapsed_raw",cycles},{"elapsed_address",0xa0000028u},{"source","GPU_CONTROLLER_PRE_ACK"},{"gpu_seconds",nullptr},{"phase_collected",j.flags==1}});
}
static void create_directory(Session &s){const std::string &path=s.options.evidence;need(path.find("/run/")==0||path.find("/tmp/")==0);need(path.find("/../")==std::string::npos&&path.find("/./")==std::string::npos&&path.back()!='/');auto base=parent(path);int fd=open(base.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);need(fd>=0,-errno);struct statfs fs{};int rc=fstatfs(fd,&fs);close(fd);need(!rc);need(uint64_t(fs.f_type)==0x01021994u||uint64_t(fs.f_type)==0x858458f6u,-EPERM);need(!mkdir(path.c_str(),0700),-errno);s.directory=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);need(s.directory>=0,-errno);}
int s8_session_run_selected(const S8Options &options){Session s;s.options=options;s.client.fd=-1;try{
 Signals signals;need(options.core_mask==1||options.core_mask==2||options.core_mask==4||options.core_mask==8||options.core_mask==15);need(options.owner_warp<4&&options.flags<=1);create_directory(s);uint64_t preflight=user_now();auto raw=file(options.manifest);s.manifest=parse(raw);need(s.manifest.at("version")==1&&s.manifest.at("scope")=="S8_RELEASE"&&s.manifest.at("profile")==5&&s.manifest.at("program")==11);
 auto root=parent(options.manifest);auto binding=asset(root,"binding.bin",512);need(binding.size()==512&&hash(binding.data(),512)==s.manifest.at("binding_sha256"));memcpy(&s.binding,binding.data(),512);need(s.binding.magic==VXL_S8_BINDING_MAGIC&&s.binding.version==1&&s.binding.bytes==512&&s.binding.profile==5&&s.binding.program==11&&s.binding.abi_major==1&&s.binding.features==1&&!s.binding.reserved0);for(auto b:s.binding.reserved)need(!b);for(const auto &digest:s.binding.digests)need(std::any_of(digest,digest+32,[](uint8_t v){return v!=0;}));need(hex(s.binding.digests[0],32)==s.manifest.at("source_sha256"));
 uint64_t hash_begin=user_now();int self=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);need(self>=0,-errno);Bytes exe;try{exe=read_fd(self,64*1024*1024);}catch(...){close(self);throw;}close(self);auto self_hash=hash(exe.data(),exe.size());need(self_hash==hex(s.binding.digests[8],32)&&self_hash==s.manifest.at("runner_sha256"),-ESTALE);need(!Session::event(&s,"runner_self_sha",hash_begin,user_now(),0));
 auto catalog=file(options.catalog);need(hash(catalog.data(),catalog.size())==hex(s.binding.digests[5],32),-ESTALE);s.catalog=parse(catalog);need(s.catalog.at("version")==1&&s.catalog.at("cases").size()==35);Json selected;unsigned matches=0;for(auto item:s.catalog.at("cases"))if(item.at("id")==options.case_id){selected=item;matches++;}need(matches==1);need(!options.flags||selected.at("phase_allowed")==true);need(options.case_id!=120||options.core_mask==15);
 auto layout=asset(root,"layout.json");need(hash(layout.data(),layout.size())==hex(s.binding.digests[3],32),-ESTALE);s.layout=parse(layout);
 auto run=unhex(options.run_id);need(run.size()==16&&std::any_of(run.begin(),run.end(),[](uint8_t v){return v!=0;}));
 Json case_entry;matches=0;for(auto item:s.manifest.at("cases"))if(item.at("id")==options.case_id){case_entry=item;matches++;}need(matches==1&&case_entry.at("objects").size()==53);
 for(unsigned i=0;i<53;++i){need(!interrupted,-EINTR);uint64_t begin=user_now();auto row=case_entry.at("objects").at(i);need(!vxl_s8_object_contract(i,&s.client.objects[i]));auto &object=s.client.objects[i];need(row.at("gpu")==object.gpu&&row.at("bytes")==object.bytes&&row.at("purpose")==object.purpose);s.initial[i]=asset(root,row.at("path"));need(s.initial[i].size()==object.bytes&&hash(s.initial[i].data(),s.initial[i].size())==row.at("sha256"),-ESTALE);object.initial=s.initial[i].data();need(!Session::event(&s,("asset_read_sha:"+std::to_string(i)).c_str(),begin,user_now(),0));}
 s.save("release-manifest.json",raw.data(),raw.size());s.save("preflight-binding.bin",binding.data(),512);s.save("catalog.json",catalog.data(),catalog.size());s.save("layout.json",layout.data(),layout.size());
 /* x86 software tests cannot open the real device through this product path. */
#if !defined(__aarch64__)
 throw -ENOEXEC;
#endif
 auto actual=trace_bytes("binding.bin");need(actual==binding,-ESTALE);
 need(!interrupted,-EINTR);need(!Session::event(&s,"preflight_total",preflight,user_now(),0));
 s.client.fd=open("/dev/vortex0",O_RDWR|O_NOFOLLOW|O_CLOEXEC);need(s.client.fd>=0,-errno);s.client.io={&s,Session::call,Session::saved,Session::traced,Session::sealed,Session::now,Session::event};need(!vxl_s8_client_bind(&s.client));int rc=vxl_s8_client_prepare(&s.client);need(!rc,rc);
 vxl_s8_job_v1 j{};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=options.case_id;j.k=selected.at("k");j.flags=options.flags;j.core_mask=options.core_mask;j.owner_warp=options.owner_warp;j.a_base=0x81000040;j.b_base=0x81200040;j.input_slot_stride=0x20000;j.output_base=0x81400000;j.output_stride=4096;j.identity_base=0x81410000;j.control_base=0x81411000;j.spin_limit=65536;memcpy(j.run_id,run.data(),16);auto tag=unhex(hash(binding.data(),512));memcpy(j.binding_tag,tag.data(),16);
 rc=vxl_s8_run_case(s.client.fd,&j,options.evidence.c_str());need(!rc,rc);return 0;
 }catch(int e){try{if(s.directory>=0)s.json("session-error.json",Json{{"state","FAIL_STOP"},{"error",e},{"errors",s.errors},{"events",s.events},{"job_id",s.client.job_id},{"captured",s.client.captured},{"unknown_inflight_not_freed",true}});}catch(...){fprintf(stderr,"S8 evidence persistence failed; original files retained\n");}return e;}catch(const std::exception &e){fprintf(stderr,"S8 session exception: %s\n",e.what());try{if(s.directory>=0)s.json("session-error.json",Json{{"state","FAIL_STOP"},{"error",-EINVAL},{"message",e.what()},{"events",s.events}});}catch(...){ }return -EINVAL;}}
int s8_session_run(const char *manifest,const char *catalog,const char *evidence){try{need(manifest&&catalog&&evidence);auto doc=parse(file(manifest));S8Options o;o.manifest=manifest;o.catalog=catalog;o.evidence=evidence;o.case_id=doc.at("selection").at("case_id");o.run_id=doc.at("selection").at("run_id");return s8_session_run_selected(o);}catch(...){return -EINVAL;}}

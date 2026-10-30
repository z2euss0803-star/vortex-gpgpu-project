#include "json.hpp"
#include <vxl/hunet.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxnn {namespace {
struct Fd {int n;explicit Fd(int x):n(x){json::check(n>=0,"secure open failed");}~Fd(){close(n);}Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;};
void readonly(const struct stat&s,bool dir){json::check((dir?S_ISDIR(s.st_mode):S_ISREG(s.st_mode))&&s.st_uid==geteuid()&&!(s.st_mode&0222),"not private owner/read-only regular content");if(!dir)json::check(s.st_nlink==1,"hard link rejected");}
bool safe(const std::string&p){if(p.empty()||p.size()>240||p[0]=='/'||p.find('\\')!=std::string::npos||p.find(':')!=std::string::npos||p.find('\0')!=std::string::npos)return false;size_t begin=0;while(begin<=p.size()){auto end=p.find('/',begin);auto part=p.substr(begin,end==std::string::npos?end:end-begin);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)break;begin=end+1;}return true;}
void list(int fd,const std::string&prefix,std::set<std::string>&files,uint32_t max,unsigned depth=0){
 json::check(depth<=8,"directory depth");Fd dir(openat(fd,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));DIR*raw=fdopendir(dup(dir.n));json::check(raw!=nullptr,"directory scan");std::unique_ptr<DIR,int(*)(DIR*)> stream(raw,closedir);
 while(auto*e=readdir(stream.get())){std::string n=e->d_name;if(n=="."||n=="..")continue;json::check(safe(n),"directory member name");struct stat s{};json::check(fstatat(fd,n.c_str(),&s,AT_SYMLINK_NOFOLLOW)==0,"member stat");if(S_ISDIR(s.st_mode)){readonly(s,true);Fd child(openat(fd,n.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));auto before=files.size();list(child.n,prefix+n+"/",files,max,depth+1);json::check(files.size()>before,"unlisted empty directory");}else{readonly(s,false);json::check(files.insert(prefix+n).second&&files.size()<=max,"file count");}}
}
std::vector<uint8_t> read_file(int root,const std::string&path,uint64_t limit){
 json::check(safe(path),"unsafe asset path");Fd parent(dup(root));size_t begin=0;
 while(true){size_t end=path.find('/',begin);if(end==std::string::npos)break;std::string part=path.substr(begin,end-begin);int next=openat(parent.n,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);json::check(next>=0,"parent link rejected");close(parent.n);parent.n=next;struct stat ds{};json::check(fstat(parent.n,&ds)==0,"parent stat");readonly(ds,true);begin=end+1;}
 Fd file(openat(parent.n,path.substr(begin).c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat before{},after{};json::check(fstat(file.n,&before)==0,"file stat");readonly(before,false);json::check(before.st_size>=0&&uint64_t(before.st_size)<=limit,"file byte limit");std::vector<uint8_t> bytes(size_t(before.st_size));size_t offset=0;while(offset<bytes.size()){ssize_t n=read(file.n,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;json::check(n>0,"short asset read");offset+=size_t(n);}uint8_t extra;json::check(read(file.n,&extra,1)==0&&fstat(file.n,&after)==0,"changed asset read");readonly(after,false);json::check(before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec&&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec,"asset changed while reading");return bytes;
}
std::string text(const std::vector<uint8_t>&b){return std::string(b.begin(),b.end());}
}
struct ModelPackage::Data {Digest id{};std::map<std::string,VerifiedAsset> assets;std::string graph,programs,numeric;uint64_t resident=0,evidence=0;uint32_t jobs=0;};
const Digest& ModelPackage::model_id()const{return data_->id;}
const std::map<std::string,VerifiedAsset>&ModelPackage::assets()const{return data_->assets;}
const std::string&ModelPackage::graph_json()const{return data_->graph;}
const std::string&ModelPackage::program_requirements_json()const{return data_->programs;}
const std::string&ModelPackage::numeric_contract_json()const{return data_->numeric;}
uint64_t ModelPackage::resident_bytes()const{return data_->resident;}
uint32_t ModelPackage::declared_max_jobs()const{return data_->jobs;}
uint64_t ModelPackage::declared_max_evidence_bytes()const{return data_->evidence;}
Result<ModelPackage> load_package(const Path&path,const PackageLimits&limits){try{
 json::check(limits.max_total_bytes&&limits.max_total_bytes<=(1ULL<<30)&&limits.max_json_bytes&&limits.max_json_bytes<=(256ULL<<10)&&limits.max_files&&limits.max_files<=4096&&limits.max_jobs&&limits.max_jobs<=0xfffffffeU&&limits.max_evidence_bytes<=(1ULL<<30),"invalid package limits");
 Fd root(open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat ds{};json::check(fstat(root.n,&ds)==0,"root stat");readonly(ds,true);std::set<std::string> files;list(root.n,"",files,limits.max_files);
 auto manifest=read_file(root.n,"manifest.json",std::min(limits.max_json_bytes,limits.max_total_bytes));auto raw=text(manifest);auto m=json::parse(raw);json::fields(m.get(),{"schema","model_id","assets","max_jobs","max_evidence_bytes"});json::check(json::u64(json::member(m.get(),"schema"))==1,"package schema");auto data=std::make_shared<ModelPackage::Data>();data->id=json::digest(json::member(m.get(),"model_id"));auto canonical=canonical_metadata(raw,true);json::check(bool(canonical)&&content_digest(*canonical.value)==data->id,"model content identity");data->jobs=json::u32(json::member(m.get(),"max_jobs"));data->evidence=json::u64(json::member(m.get(),"max_evidence_bytes"));json::check(data->jobs&&data->jobs<=limits.max_jobs&&data->evidence<=limits.max_evidence_bytes,"task/evidence admission budget");data->resident=manifest.size();auto*a=json::member(m.get(),"assets");size_t n=json::count(a,limits.max_files-1);json::check(n>=3&&files.size()==n+1&&files.count("manifest.json"),"unlisted or missing file");std::set<std::string> listed;
 for(size_t i=0;i<n;i++){auto*r=json::at(a,i);json::fields(r,{"path","bytes","sha256","type","role"});auto name=json::str(json::member(r,"path"));auto type=json::str(json::member(r,"type"));auto role=json::str(json::member(r,"role"));uint64_t size=json::u64(json::member(r,"bytes"));json::check(safe(name)&&name!="manifest.json"&&files.count(name)&&listed.insert(name).second,"asset path/list");json::check((type=="json"||type=="binary")&&!role.empty()&&role.size()<=64,"asset type/role");json::check(size<=limits.max_total_bytes-data->resident&&(type!="json"||size<=limits.max_json_bytes),"total package byte budget");auto bytes=read_file(root.n,name,size);json::check(bytes.size()==size,"asset length");Digest digest{};vxl_hunet_hash(bytes.data(),bytes.size(),digest.data());json::check(digest==json::digest(json::member(r,"sha256")),"asset SHA");if(type=="json")json::check(bool(canonical_metadata(text(bytes))),"asset JSON contract");data->resident=json::add(data->resident,size);data->assets.emplace(name,VerifiedAsset{name,type,role,digest,std::make_shared<const std::vector<uint8_t>>(std::move(bytes))});}
 auto metadata=[&](const char*name,const char*role){auto it=data->assets.find(name);json::check(it!=data->assets.end()&&it->second.type=="json"&&it->second.role==role,"mandatory metadata role");auto s=text(*it->second.content);data->resident=json::add(data->resident,s.size());return s;};data->graph=metadata("graph.json","graph");data->programs=metadata("program-requirements.json","programs");data->numeric=metadata("numeric-contract.json","numeric");
 return Result<ModelPackage>::success(ModelPackage(data));
 }catch(const std::exception&e){return Result<ModelPackage>::failure({EINVAL,"PACKAGE_REJECTED",e.what()});}}
}

#include <vxunified/native_files.hpp>
#include "unified/results/io.hpp"
#include <atomic>
#include <map>
#include <mutex>
namespace vxunified {namespace {
struct IoFailure {int code;const char*detail;};
void check(bool yes,const char*text){if(!yes)throw IoFailure{errno?errno:EIO,text};}
void private_directory(int fd){struct stat st{};check(fd>=0&&!fstat(fd,&st),"private directory stat");if(!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0700)throw IoFailure{EACCES,"same-uid private0700 directory required"};}
bool same_hw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
bool identifier(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
bool leaf_ok(const std::string&s){return !s.empty()&&s.size()<=128&&s!="."&&s!=".."&&s!="trace.raw"&&std::all_of(s.begin(),s.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';});}
std::string directory_name(const RequestKey&k){std::string raw="VX_NATIVE_OUTPUT_V1:";for(const auto*s:{&k.boot_id,&k.client_id,&k.request_id})raw+=std::to_string(s->size())+":"+*s;return vxnn::digest_hex(vxnn::content_digest(raw));}
Digest digest(vxnn::Bytes b){Digest out{};vxl_hunet_hash(b.data,b.size,out.data());return out;}
struct OutputDirectory {result_io::Fd fd;explicit OutputDirectory(int f):fd(f){}};
struct FileRecord {
 struct Output {ResultArtifact artifact;bool complete=false;};
 std::mutex mutex;std::map<std::string,Output>outputs;
 uint64_t reserved=0;
 uint64_t trace_limit=0,output_limit=0,trace_written=0,output_written=0;
 std::string directory;std::optional<vxnn::Error>first_error;
};
}
struct NativeFiles::State {
 std::weak_ptr<Runtime>runtime;std::string boot;std::shared_ptr<OutputDirectory>root;
 mutable std::mutex mutex;std::map<std::string,std::weak_ptr<NativeRequestFiles::State>>live;
 std::shared_ptr<HostMemoryLease>account;
 // Records contain no backlink to State. An active writer keeps State alive;
 // retained records keep bytes charged after writer/source retirement.
 std::map<std::string,std::shared_ptr<FileRecord>>retained;
 std::atomic<uint64_t>written{0};
};
struct NativeRequestFiles::State {
 std::shared_ptr<NativeFiles::State>owner;std::shared_ptr<FileRecord>record;
 bool admitted=false;
 result_io::Fd directory{-1},trace{-1};
 ~State(){
  if(!admitted)return;
  // Last writer: close descriptors before returning unused write capacity.
  // Failed close keeps its original maximum quota, never claiming a seal.
  int close_error=0;for(auto*fd:{&trace,&directory})if(fd->value>=0){if(close(fd->value)&&!close_error)close_error=errno;fd->value=-1;}
  std::lock_guard<std::mutex>record_lock(record->mutex);
  std::lock_guard<std::mutex>index_lock(owner->mutex);
  const bool retained=owner->retained.count(record->directory)!=0;
  const auto keep=retained?(close_error?record->reserved:record->trace_written+record->output_written+8192):0;
  if(keep>record->reserved||!owner->account)std::terminate();
  owner->account->release_bytes(record->reserved-keep);record->reserved=keep;
  owner->live.erase(record->directory);
  if(close_error)dprintf(STDERR_FILENO,"NATIVE_FILES_SEAL_ERROR errno=%d original_quota_retained=1\n",close_error);
 }
 void ensure_directory(){
  if(directory.value>=0)return;
  {std::lock_guard<std::mutex>lock(owner->mutex);auto it=owner->retained.find(record->directory);if(it!=owner->retained.end()&&it->second!=record)throw IoFailure{EEXIST,"previous request evidence retained"};owner->retained.emplace(record->directory,record);}
  if(mkdirat(owner->root->fd.value,record->directory.c_str(),0700)&&errno!=EEXIST)throw IoFailure{errno,"request directory creation"};
  directory.value=openat(owner->root->fd.value,record->directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);private_directory(directory.value);check(!fsync(owner->root->fd.value),"request directory sync");
 }
 void write_bytes(int fd,vxnn::Bytes b,uint64_t&count){
  size_t done=0;while(done<b.size){auto n=write(fd,b.data+done,b.size-done);if(n<0&&errno==EINTR)continue;check(n>0,"write incomplete; partial original retained");done+=size_t(n);count+=uint64_t(n);owner->written.fetch_add(uint64_t(n));}
 }
 vxnn::Error fail(int code,const char*detail){if(!record->first_error)record->first_error=vxnn::Error{code,"NATIVE_FILES",detail};return *record->first_error;}
};
NativeRequestFiles::NativeRequestFiles(std::shared_ptr<State>s):state_(std::move(s)){}
NativeRequestFiles::~NativeRequestFiles()=default;
Result<void>NativeRequestFiles::append_trace(vxnn::Bytes b,bool sync){
 auto&s=*state_;auto&r=*s.record;std::lock_guard<std::mutex>lock(r.mutex);if(r.first_error)return Result<void>::failure(*r.first_error);
 try{
  if(!b.data||!b.size||r.trace_written>r.trace_limit||b.size>r.trace_limit-r.trace_written)throw IoFailure{EFBIG,"original trace exceeds admitted evidence budget"};
  s.ensure_directory();if(s.trace.value<0){s.trace.value=openat(s.directory.value,"trace.raw",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0400);check(s.trace.value>=0,"immutable trace creation; previous evidence retained");}
  s.write_bytes(s.trace.value,b,r.trace_written);if(sync)check(!fdatasync(s.trace.value)&&!fsync(s.directory.value),"first-error/final trace sync");return Result<void>::success();
 }catch(const IoFailure&e){return Result<void>::failure(s.fail(e.code,e.detail));}catch(const std::bad_alloc&){return Result<void>::failure(s.fail(ENOMEM,"trace allocation"));}catch(const std::exception&){return Result<void>::failure(s.fail(EINVAL,"trace metadata exception"));}
}
Result<ResultArtifact>NativeRequestFiles::output(const std::string&leaf,vxnn::Bytes b,const std::string&layout,const vxnn::Shape&shape){
 using R=Result<ResultArtifact>;auto&s=*state_;auto&r=*s.record;std::lock_guard<std::mutex>lock(r.mutex);if(r.first_error)return R::failure(*r.first_error);
 try{
  if(!leaf_ok(leaf)||layout.empty()||layout.size()>32||shape.empty()||shape.size()>8||!b.data||!b.size)throw IoFailure{EINVAL,"bounded output leaf/layout/shape/bytes required"};
  if(r.outputs.count(leaf))throw IoFailure{EEXIST,"previous immutable output retained"};
  if(r.outputs.size()>=16||r.output_written>r.output_limit||b.size>r.output_limit-r.output_written)throw IoFailure{EFBIG,"output exceeds admitted artifact16/byte bound"};
  uint64_t count=1;for(auto n:shape){if(!n||n>uint64_t(b.size)/count)throw IoFailure{EINVAL,"output shape exceeds byte extent"};count*=n;}
  auto unit=b.size/count;if(b.size%count||(unit!=1&&unit!=2&&unit!=4&&unit!=8))throw IoFailure{EINVAL,"output shape element extent"};
  ResultArtifact a;a.path=r.directory+"/"+leaf;a.layout=layout;a.shape=shape;a.bytes=b.size;a.sha=digest(b);
  // Allocate the immutable index before creating a file. Incomplete files are
  // retained but never become a readable completed artifact.
  auto inserted=r.outputs.emplace(leaf,FileRecord::Output{a,false}).first;s.ensure_directory();if(s.trace.value>=0)check(!fdatasync(s.trace.value),"trace sync before final output");
  result_io::Fd file(openat(s.directory.value,leaf.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0400));check(file.value>=0,"immutable output creation; previous evidence retained");
  s.write_bytes(file.value,b,r.output_written);check(!fsync(file.value)&&!fsync(s.directory.value),"original output and directory sync");inserted->second.complete=true;return R::success(std::move(a));
 }catch(const IoFailure&e){return R::failure(s.fail(e.code,e.detail));}catch(const std::bad_alloc&){return R::failure(s.fail(ENOMEM,"output metadata allocation"));}catch(const std::exception&){return R::failure(s.fail(EINVAL,"output metadata exception"));}
}
NativeFiles::NativeFiles(std::shared_ptr<State>s):state_(std::move(s)){}
Result<std::shared_ptr<NativeFiles>>NativeFiles::create(const StartupContext&c,const std::string&path){
 using R=Result<std::shared_ptr<NativeFiles>>;try{
  // This pure filesystem store does not independently measure PL contents.
  // CAPS/configuration admission keeps that marker false; require the exact
  // Runtime marker/scope without turning a declaration into verification.
  auto rt=c.runtime.lock();if(!rt||c.mode!=BackendMode::COMPUTE_V2||c.results.lock()!=rt->trusted_results()||c.scope!=rt->evidence_scope()||!same_hw(c.identity,rt->identity())||c.boot_id.empty())return R::failure({EXDEV,"NATIVE_FILES","same live Runtime/result roots/platform/scope and boot identity required"});
  if(path.size()<2||path[0]!='/'||path.size()>4096)return R::failure({EINVAL,"NATIVE_FILES","trusted absolute private directory required"});
  result_io::Fd slash(open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));result_io::Fd opened(result_io::child(slash.value,path.substr(1),true));private_directory(opened.value);
  // The descriptor must already have a stack owner if make_shared itself runs
  // out of memory. Transfer only after its owning control block exists.
  auto root=std::make_shared<OutputDirectory>(-1);root->fd.value=opened.value;opened.value=-1;
  auto state=std::make_shared<State>();state->runtime=rt;state->boot=c.boot_id;state->root=std::move(root);return R::success(std::shared_ptr<NativeFiles>(new NativeFiles(std::move(state))));
 }catch(const IoFailure&e){return R::failure({e.code,"NATIVE_FILES",e.detail});}catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_FILES","output root allocation"});}catch(const std::exception&){return R::failure({EINVAL,"NATIVE_FILES","private root path validation"});}
}
Result<std::shared_ptr<NativeRequestFiles>>NativeFiles::request(const RequestKey&key,uint64_t trace_limit,uint64_t output_limit){
 using R=Result<std::shared_ptr<NativeRequestFiles>>;try{
  auto&s=*state_;if(!identifier(key.boot_id)||!identifier(key.client_id)||!identifier(key.request_id)||key.boot_id!=s.boot||!output_limit||output_limit>(64ULL<<20)||trace_limit>(1ULL<<30)-8192-output_limit)return R::failure({EINVAL,"NATIVE_FILES","complete boot/request and bounded evidence/output extents required"});
  auto rt=s.runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"NATIVE_FILES","common Runtime expired"});auto directory=directory_name(key);
  auto check_index=[&]()->Result<void>{
   for(auto i=s.live.begin();i!=s.live.end();)if(i->second.expired())i=s.live.erase(i);else ++i;
   if(s.live.count(directory)||s.retained.count(directory))return Result<void>::failure({EEXIST,"NATIVE_FILES","previous request evidence/lifetime retained"});
   size_t indexed=s.retained.size();for(const auto&entry:s.live)if(!s.retained.count(entry.first))++indexed;
   if(indexed>=256)return Result<void>::failure({ENOSPC,"NATIVE_FILES","bounded request storage index256"});
   return Result<void>::success();
  };
  {std::lock_guard<std::mutex>lock(s.mutex);auto valid=check_index();if(!valid)return R::failure(*valid.error);}
  // Never enter Runtime while holding the file index mutex. Completion/ACK
  // holds the port before persisting its original trace; request retirement
  // holds Runtime before the port. Acquiring Runtime here closed that cycle.
  // Allocate before the final lock as well, so rollback cannot destroy an
  // owning quota while this index transaction remains locked.
  auto charge=rt->reserve_host_memory(trace_limit+output_limit+8192);if(!charge)return R::failure(*charge.error);
  auto record=std::make_shared<FileRecord>();record->reserved=trace_limit+output_limit+8192;record->directory=directory;record->trace_limit=trace_limit;record->output_limit=output_limit;
  auto state=std::make_shared<NativeRequestFiles::State>();state->owner=state_;state->record=std::move(record);auto files=std::shared_ptr<NativeRequestFiles>(new NativeRequestFiles(state));
  {std::lock_guard<std::mutex>lock(s.mutex);auto valid=check_index();if(!valid)return R::failure(*valid.error);s.live.emplace(directory,state);
   // The new quota already passed Runtime phase/CPU1GiB/host64 admission.
   // Only consolidate credits, with the separate storage index still256.
   // No Backing/Runtime mutex is entered while the file index is locked.
   if(s.account){auto merged=s.account->absorb(**charge.value);if(!merged){s.live.erase(directory);return R::failure(*merged.error);}}
   else s.account=*charge.value;
   state->admitted=true;
  }
  return R::success(std::move(files));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_FILES","request files allocation; no write"});}catch(const std::exception&){return R::failure({EINVAL,"NATIVE_FILES","request metadata validation"});}
}
Result<std::vector<uint8_t>>NativeFiles::read(const ResultArtifact&a)const{
 using R=Result<std::vector<uint8_t>>;try{
  const auto parts=result_io::parts(a.path);if(parts.size()!=2||a.bytes==0||a.bytes>(64ULL<<20))return R::failure({EINVAL,"NATIVE_FILES","bounded own completed artifact path required"});std::shared_ptr<FileRecord>record;
  {std::lock_guard<std::mutex>lock(state_->mutex);auto it=state_->retained.find(parts[0]);if(it==state_->retained.end())return R::failure({ENOENT,"NATIVE_FILES","artifact not in this owner output store"});record=it->second;}
  std::lock_guard<std::mutex>lock(record->mutex);auto it=record->outputs.find(parts[1]);if(it==record->outputs.end()||!it->second.complete)return R::failure({ENOENT,"NATIVE_FILES","artifact has no successful durable output proof"});const auto&bound=it->second.artifact;
  if(bound.path!=a.path||bound.layout!=a.layout||bound.shape!=a.shape||bound.sha!=a.sha||bound.bytes!=a.bytes)return R::failure({EBADMSG,"NATIVE_FILES","artifact identity/shape/extent differs stored original"});
  result_io::Fd file(result_io::child(state_->root->fd.value,a.path));auto raw=result_io::read(file.value,a.bytes,a.bytes);struct stat st{};check(!fstat(file.value,&st),"output stat");if(st.st_uid!=geteuid()||(st.st_mode&07777)!=0400||st.st_nlink!=1||result_io::hash(raw)!=a.sha)return R::failure({EBADMSG,"NATIVE_FILES","immutable original owner/mode/link/hash differs"});return R::success(std::move(raw));
 }catch(const IoFailure&e){return R::failure({e.code,"NATIVE_FILES",e.detail});}catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_FILES","artifact read allocation"});}catch(const std::exception&){return R::failure({EINVAL,"NATIVE_FILES","artifact path/file validation"});}
}
NativeFileStats NativeFiles::stats()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);NativeFileStats out;out.retained_requests=s.retained.size();out.written_bytes=s.written.load();for(const auto&entry:s.retained)out.reserved_bytes+=entry.second->reserved;return out;}
bool NativeFiles::owns_runtime(const Runtime*r)const{return r&&state_->runtime.lock().get()==r;}
bool NativeFiles::belongs_to_request(const RequestKey&key,const ResultArtifact&a)const{
 if(key.boot_id!=state_->boot||!identifier(key.boot_id)||!identifier(key.client_id)||!identifier(key.request_id))return false;
 const auto directory=directory_name(key);return a.path.size()>directory.size()+1&&a.path.compare(0,directory.size(),directory)==0&&a.path[directory.size()]=='/'&&a.path.find('/',directory.size()+1)==std::string::npos;
}
}

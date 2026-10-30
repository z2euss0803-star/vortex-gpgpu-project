#include <vxunified/display.hpp>
#include <cstring>
#include <vxunified/journal.hpp>
#include <vxunified/control.hpp>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <stdexcept>
namespace vxunified {
namespace {
using J=std::unique_ptr<json_object,decltype(&json_object_put)>;
J own(json_object*p){if(!p)throw std::bad_alloc();return J(p,json_object_put);}
J object(){return own(json_object_new_object());}
void put(json_object*p,const char*k,J v){if(json_object_object_add(p,k,v.get()))throw std::bad_alloc();v.release();}
void nil(json_object*p,const char*k){if(json_object_object_add(p,k,nullptr))throw std::bad_alloc();}
J text(const std::string&s){return own(json_object_new_string_len(s.data(),int(s.size())));}
J u64(uint64_t n){return own(json_object_new_uint64(n));}
J integer(int64_t n){return own(json_object_new_int64(n));}
J boolean(bool n){return own(json_object_new_boolean(n));}
void check(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
bool id(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
std::string hex(const void*raw,size_t n){static const char digits[]="0123456789abcdef";const auto*p=static_cast<const unsigned char*>(raw);std::string out;out.reserve(n*2);for(size_t i=0;i<n;i++){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;}
J request(const RequestKey&k){check(id(k.boot_id)&&id(k.client_id)&&id(k.request_id),"invalid request key");auto o=object();put(o.get(),"boot_id",text(k.boot_id));put(o.get(),"client_id",text(k.client_id));put(o.get(),"request_id",text(k.request_id));return o;}
J binding(const ExecutionBinding&b){
 BackendIdentity identity;identity.hardware_contract=b.hardware;identity.bit=b.bit;identity.xsa=b.xsa;identity.compatible=b.compatible;identity.device_generation=b.device_generation;identity.provider_flags=b.provider_flags;
 bool provider=(b.provider_flags==1&&(b.app==AppKind::NN||b.app==AppKind::IMAGE))||(b.provider_flags==2&&b.app==AppKind::P8)||(s8_joint_platform(identity)&&(b.app==AppKind::NN||b.app==AppKind::IMAGE||b.app==AppKind::P8));check(provider&&b.device_generation,"invalid admitted binding");auto o=object();put(o.get(),"app",integer(static_cast<unsigned>(b.app)));put(o.get(),"provider_flags",u64(b.provider_flags));put(o.get(),"device_generation",u64(b.device_generation));put(o.get(),"input_bytes",u64(b.input_bytes));
 const char*keys[]={"hardware_sha","bit_sha","xsa_sha","compatible_sha","model_sha","plan_sha","input_sha","source_sha"};const Digest*values[]={&b.hardware,&b.bit,&b.xsa,&b.compatible,&b.model,&b.plan,&b.input,&b.source};
 for(unsigned i=0;i<8;i++){check(nonzero_digest(*values[i]),"incomplete admitted binding");put(o.get(),keys[i],text(hex(values[i]->data(),32)));}return o;
}
J error(const vxnn::Error&e){check(e.code&&e.category.size()<=256&&e.detail.size()<=65536,"invalid/oversized error");auto o=object();put(o.get(),"code",integer(e.code));put(o.get(),"category",text(e.category));put(o.get(),"detail",text(e.detail));return o;}
const char*scope(vxnn::EvidenceScope s){switch(s){case vxnn::EvidenceScope::HOST_PROTOCOL_TEST:return "HOST_PROTOCOL_TEST";case vxnn::EvidenceScope::OFFLINE:return "OFFLINE";case vxnn::EvidenceScope::CROSS_COMPILE:return "CROSS_COMPILE";case vxnn::EvidenceScope::IMAGE:return "IMAGE";case vxnn::EvidenceScope::REAL_BOARD:return "REAL_BOARD";}throw std::invalid_argument("invalid evidence scope");}
J base(const char*kind,const RequestKey&k,const ExecutionBinding&b,vxnn::EvidenceScope s){auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text(kind));put(o.get(),"request",request(k));put(o.get(),"binding",binding(b));put(o.get(),"device_generation",u64(b.device_generation));put(o.get(),"scope",text(scope(s)));return o;}
std::string dump(json_object*p){auto*raw=json_object_to_json_string_ext(p,JSON_C_TO_STRING_PLAIN);if(!raw)throw std::bad_alloc();std::string s(raw);s+='\n';check(s.size()<=(1u<<20),"event exceeds 1MiB");return s;}
std::string work(const ExecutionReceipt&r,DeliveryStage stage){
 const char*names[]={"RAW_COMPLETION","REQUEST_STATE","EXECUTION_ERROR","USER_ACK_RETIREMENT"};auto n=static_cast<unsigned>(stage);check(n<4,"invalid delivery stage");check(static_cast<unsigned>(r.outcome.status)<=8&&static_cast<unsigned>(r.delivered_status)<=8,"invalid request state");if(stage==DeliveryStage::EXECUTION_ERROR)check(r.execution_error.has_value(),"missing original execution error");
 auto o=base("work",r.outcome.request,r.binding,r.scope);put(o.get(),"stage",text(n==3&&r.outcome.legacy?"LEGACY_RETIREMENT":names[n]));put(o.get(),"node",u64(r.outcome.node));put(o.get(),"work_index",u64(r.outcome.work_index));put(o.get(),"status",integer(static_cast<unsigned>(r.outcome.status)));put(o.get(),"delivered_status",integer(static_cast<unsigned>(r.delivered_status)));put(o.get(),"result",integer(r.outcome.result));put(o.get(),"acknowledged",boolean(r.outcome.acknowledged));put(o.get(),"retired",boolean(r.outcome.retired));
 const char*clock=nullptr;switch(r.clock_domain){case vxnn::ClockDomain::HOST_MONOTONIC:clock="HOST_MONOTONIC";break;case vxnn::ClockDomain::LINUX_MONOTONIC:clock="LINUX_MONOTONIC";break;case vxnn::ClockDomain::B_MONOTONIC:clock="B_MONOTONIC";break;default:throw std::invalid_argument("wall intervals cannot use GPU_COUNTER");}put(o.get(),"clock_domain",text(clock));
 const char*keys[]={"dispatch_begin","dispatch_end","execute_begin","execute_end","submit_begin","submit_end","wait_begin","wait_end","read_begin","read_end","user_retire_begin","user_retire_end","legacy_call_begin","legacy_call_end","prepare_begin","prepare_end"};uint64_t times[]={r.dispatch_begin_ns,r.dispatch_end_ns,r.execute_begin_ns,r.execute_end_ns,r.submit_begin_ns,r.submit_end_ns,r.wait_begin_ns,r.wait_end_ns,r.read_begin_ns,r.read_end_ns,r.retire_begin_ns,r.retire_end_ns,r.legacy_call_begin_ns,r.legacy_call_end_ns,r.prepare_begin_ns,r.prepare_end_ns};auto t=object();for(unsigned i=0;i<16;i++)put(t.get(),keys[i],u64(times[i]));put(o.get(),"timing_ns",std::move(t));
 put(o.get(),"submitted_cookie",u64(r.submitted_cookie));put(o.get(),"submitted_wire_cookie",u64(r.submitted_wire_cookie));put(o.get(),"submitted_sequence",u64(r.submitted_sequence));put(o.get(),"program_handle",u64(r.program_handle));put(o.get(),"program_generation",u64(r.program_generation));put(o.get(),"submitted_device_generation",u64(r.device_generation));
 if(nonzero_digest(r.program_entry_id))put(o.get(),"program_catalog_entry_id",text(hex(r.program_entry_id.data(),32)));else nil(o.get(),"program_catalog_entry_id");
 if(r.execution_error)put(o.get(),"execution_error",error(*r.execution_error));else nil(o.get(),"execution_error");if(r.evidence_error)put(o.get(),"evidence_error",error(*r.evidence_error));else nil(o.get(),"evidence_error");
 if(r.recovery_error)put(o.get(),"recovery_error",error(*r.recovery_error));else nil(o.get(),"recovery_error");put(o.get(),"recovery_attempts",u64(r.recovery_attempts));put(o.get(),"recovery_begin_ns",u64(r.recovery_begin_ns));put(o.get(),"recovery_end_ns",u64(r.recovery_end_ns));
 if(r.linux_detail){
  const auto&c=*r.linux_detail;put(o.get(),"linux_completion_hex",text(hex(&c,sizeof(c))));
  bool fast=c.version==4&&c.reserved0==VXL_V2_COMPLETION_FAST_UNCHECKED&&c.reserved[0]==VXL_V2_FAST_POLICY_VERSION&&c.reserved[1]==0&&c.reserved[2]==0&&c.reserved[3]==0&&c.reserved[4]==VXL_V2_FAST_OMITTED_CHECKS&&c.reserved[5]==0&&c.reserved[6]==0&&c.reserved[7]==0;
  bool strict=c.version==2&&c.reserved0==0;
  put(o.get(),"integrity_contract",text(fast?"FAST_RUNTIME_V1":strict?"STRICT_3PASS_V1":"UNKNOWN"));
  // A strict error/raw snapshot does not prove that every content gate ran.
  bool verified=strict&&c.result==0&&r.outcome.retired&&r.outcome.acknowledged;
  put(o.get(),"source_content_fresh",boolean(verified));put(o.get(),"device_args_content_fresh",boolean(verified));
 }else {nil(o.get(),"linux_completion_hex");nil(o.get(),"integrity_contract");nil(o.get(),"source_content_fresh");nil(o.get(),"device_args_content_fresh");}
 if(r.legacy_detail)put(o.get(),"legacy_info_hex",text(hex(&*r.legacy_detail,sizeof(*r.legacy_detail))));else nil(o.get(),"legacy_info_hex");
 if(r.outcome.legacy){const auto&p=*r.outcome.legacy;auto v=object();put(v.get(),"device_generation",u64(p.device_generation));put(v.get(),"profile_id",u64(p.profile_id));put(v.get(),"aggregate_complete",boolean(p.aggregate_complete));put(v.get(),"quiescent",boolean(p.quiescent));put(v.get(),"result_sha",text(hex(p.result_sha.data(),32)));put(v.get(),"info_sha",text(hex(p.info_sha.data(),32)));put(o.get(),"legacy_retirement",std::move(v));}else nil(o.get(),"legacy_retirement");
 // A post-check can fail after the old executor returned a result. Retain
 // those exact bytes even though publication is forbidden. Malformed adapter
 // output is bounded explicitly; it must not make error logging unbounded.
 if(r.output&&!r.output->raw_legacy_json.empty()){const auto&raw=r.output->raw_legacy_json;put(o.get(),"partial_result_legacy_json_hex",text(hex(raw.data(),std::min(raw.size(),size_t(65536)))));put(o.get(),"partial_result_legacy_json_bytes",u64(raw.size()));put(o.get(),"partial_result_legacy_json_truncated",boolean(raw.size()>65536));}else{nil(o.get(),"partial_result_legacy_json_hex");put(o.get(),"partial_result_legacy_json_bytes",u64(0));put(o.get(),"partial_result_legacy_json_truncated",boolean(false));}
 bool valid=r.scope==vxnn::EvidenceScope::REAL_BOARD&&r.outcome.completion&&r.outcome.completion->cycles_valid&&r.outcome.completion->raw_cycles.has_value();put(o.get(),"cycles_valid",boolean(valid));if(valid)put(o.get(),"gpu_raw_cycles",u64(*r.outcome.completion->raw_cycles));else nil(o.get(),"gpu_raw_cycles");
 if(r.outcome.completion){const auto&c=*r.outcome.completion;auto v=object();put(v.get(),"sequence",u64(c.sequence));put(v.get(),"cookie",u64(c.cookie));put(v.get(),"wire_cookie",u64(c.wire_cookie));put(v.get(),"status",integer(c.status));put(v.get(),"result",integer(c.result));put(v.get(),"reported_cycles_valid",boolean(c.cycles_valid));if(c.raw_cycles)put(v.get(),"reported_raw_cycles",u64(*c.raw_cycles));else nil(v.get(),"reported_raw_cycles");put(o.get(),"completion",std::move(v));}else nil(o.get(),"completion");return dump(o.get());
}
std::string result(const PublishedResult&p,bool staged=false){
 check(p.binding.has_value()&&p.execution_complete&&p.lease.artifacts.size()>0&&p.lease.artifacts.size()<=64&&p.raw_legacy_json.size()<=65536,"invalid result receipt");const auto&b=*p.binding;check(b.app==p.lease.app&&b.hardware==p.lease.hardware&&b.model==p.lease.model&&b.plan==p.lease.plan,"result binding differs");check(!p.gpu_raw_cycles||p.lease.scope==vxnn::EvidenceScope::REAL_BOARD,"offline result cycles must be null");check(static_cast<unsigned>(p.numeric)<=3,"invalid numeric state");
 auto o=base(staged?"result_stage":"result",p.lease.producer,b,p.lease.scope);if(staged)put(o.get(),"client_visible",boolean(false));put(o.get(),"execution_complete",boolean(p.execution_complete));put(o.get(),"displayed",boolean(p.displayed));put(o.get(),"numeric",integer(static_cast<unsigned>(p.numeric)));put(o.get(),"legacy_json_hex",text(hex(p.raw_legacy_json.data(),p.raw_legacy_json.size())));if(p.gpu_raw_cycles)put(o.get(),"gpu_raw_cycles",u64(*p.gpu_raw_cycles));else nil(o.get(),"gpu_raw_cycles");
 auto a=own(json_object_new_array());for(const auto&artifact:p.lease.artifacts){check(!artifact.path.empty()&&artifact.path.size()<=4096&&!artifact.layout.empty()&&artifact.layout.size()<=64&&nonzero_digest(artifact.sha)&&artifact.shape.size()>0&&artifact.shape.size()<=8&&artifact.bytes,"invalid artifact metadata");auto v=object();put(v.get(),"path",text(artifact.path));put(v.get(),"layout",text(artifact.layout));put(v.get(),"sha256",text(hex(artifact.sha.data(),32)));put(v.get(),"bytes",u64(artifact.bytes));auto shape=own(json_object_new_array());for(auto d:artifact.shape){check(d,"empty dimension");auto number=u64(d);if(json_object_array_add(shape.get(),number.get()))throw std::bad_alloc();number.release();}put(v.get(),"shape",std::move(shape));if(json_object_array_add(a.get(),v.get()))throw std::bad_alloc();v.release();}put(o.get(),"artifacts",std::move(a));return dump(o.get());
}
struct Fd {int n=-1;~Fd(){if(n>=0)::close(n);}int close_once(){int fd=n;n=-1;return ::close(fd);}};
Result<void> io_error(int e,const std::string&why){return Result<void>::failure({e,"JOURNAL",why});}
}
struct EvidenceJournal::Impl {
 Fd dir;std::mutex mutex;uint64_t serial=0;
 Result<void> existing(const std::string&name,const std::string&bytes){
  Fd f;f.n=openat(dir.n,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f.n<0)return io_error(errno,"open immutable event "+name);struct stat st{};if(fstat(f.n,&st))return io_error(errno,"stat immutable event "+name);
  if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0600||st.st_size<0||uint64_t(st.st_size)!=bytes.size())return io_error(ESTALE,"immutable event metadata differs "+name);
  std::string read(bytes.size(),'\0');size_t done=0;while(done<read.size()){ssize_t n=pread(f.n,&read[done],read.size()-done,off_t(done));if(n<0&&errno==EINTR)continue;if(n<=0)return io_error(n<0?errno:EIO,"read immutable event "+name);done+=size_t(n);}if(read!=bytes)return io_error(ESTALE,"immutable event content differs "+name);
  if(fsync(f.n)){return io_error(errno,"sync existing event "+name);}
  if(f.close_once()){return io_error(errno,"close existing event "+name);}
  if(fsync(dir.n)){return io_error(errno,"sync existing event directory "+name);}
  return Result<void>::success();
 }
 Result<void> save(const std::string&bytes){
  std::lock_guard<std::mutex>lock(mutex);Digest sha{};vxl_hunet_hash(bytes.data(),bytes.size(),sha.data());auto hash=hex(sha.data(),32);auto name=hash+".json";struct stat found{};
  if(fstatat(dir.n,name.c_str(),&found,AT_SYMLINK_NOFOLLOW)==0){return existing(name,bytes);}
  if(errno!=ENOENT){return io_error(errno,"inspect immutable event "+name);}
  Fd file;std::string partial;for(unsigned i=0;i<16;i++){if(serial==UINT64_MAX)return io_error(EOVERFLOW,"staging serial exhausted");partial=".partial-"+hash+"-"+std::to_string(getpid())+"-"+std::to_string(++serial);file.n=openat(dir.n,partial.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);if(file.n>=0)break;if(errno!=EEXIST)return io_error(errno,"create staging event "+partial);}if(file.n<0)return io_error(EEXIST,"staging collision bound");
  if(fchmod(file.n,0600)){return io_error(errno,"private staging permissions "+partial);}
  size_t done=0;while(done<bytes.size()){ssize_t n=write(file.n,bytes.data()+done,bytes.size()-done);if(n<0&&errno==EINTR)continue;if(n<=0)return io_error(n<0?errno:EIO,"write partial retained "+partial);done+=size_t(n);}
  if(fdatasync(file.n)){return io_error(errno,"sync staging retained "+partial);}
  if(file.close_once()){return io_error(errno,"close staging retained "+partial);}
  if(linkat(dir.n,partial.c_str(),dir.n,name.c_str(),0)){if(errno!=EEXIST)return io_error(errno,"publish staging retained "+partial);auto same=existing(name,bytes);if(!same)return same;}else if(fsync(dir.n))return io_error(errno,"directory sync failed; staged and final retained "+partial);
  // Final content is already synchronized. Staging cleanup is optional; an
  // unsuccessful unlink only leaves another reference to the same evidence.
  (void)unlinkat(dir.n,partial.c_str(),0);return Result<void>::success();
 }
};
EvidenceJournal::EvidenceJournal(std::unique_ptr<Impl>p):impl_(std::move(p)){}
EvidenceJournal::~EvidenceJournal()=default;
Result<std::shared_ptr<EvidenceJournal>> EvidenceJournal::open(const std::string&path){
 try{if(path.empty()||path[0]!='/'||path.find('\0')!=std::string::npos)return Result<std::shared_ptr<EvidenceJournal>>::failure({EINVAL,"JOURNAL","trusted absolute private directory required"});auto s=std::make_unique<Impl>();s->dir.n=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(s->dir.n<0)return Result<std::shared_ptr<EvidenceJournal>>::failure({errno,"JOURNAL","open private evidence directory"});struct stat st{};if(fstat(s->dir.n,&st))return Result<std::shared_ptr<EvidenceJournal>>::failure({errno,"JOURNAL","stat evidence directory"});if(st.st_uid!=geteuid()||(st.st_mode&07777)!=0700)return Result<std::shared_ptr<EvidenceJournal>>::failure({EACCES,"JOURNAL","evidence directory must be same uid mode0700"});if(flock(s->dir.n,LOCK_EX|LOCK_NB))return Result<std::shared_ptr<EvidenceJournal>>::failure({errno==EAGAIN||errno==EWOULDBLOCK?EBUSY:errno,"JOURNAL","single evidence writer"});return Result<std::shared_ptr<EvidenceJournal>>::success(std::shared_ptr<EvidenceJournal>(new EvidenceJournal(std::move(s))));}catch(const std::bad_alloc&){return Result<std::shared_ptr<EvidenceJournal>>::failure({ENOMEM,"JOURNAL","journal startup allocation"});}
}
Result<void> EvidenceJournal::save_work(const ExecutionReceipt&r,DeliveryStage s){try{return impl_->save(work(r,s));}catch(const std::bad_alloc&){return io_error(ENOMEM,"work serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}}
Result<void> EvidenceJournal::save_result(const PublishedResult&p){try{return impl_->save(result(p));}catch(const std::bad_alloc&){return io_error(ENOMEM,"result serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}}
Result<void> EvidenceJournal::save_result_stage(const PublishedResult&p){try{return impl_->save(result(p,true));}catch(const std::bad_alloc&){return io_error(ENOMEM,"private result-stage serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}}
Result<void>EvidenceJournal::save_preparation(const PreparationReceipt&r,vxnn::EvidenceScope s){try{check(r.begin_ns&&r.end_ns>=r.begin_ns&&id(r.entry)&&id(r.input_id),"preparation clocks/entry/input");auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text("preparation"));put(o.get(),"request",request(r.request));put(o.get(),"scope",text(scope(s)));put(o.get(),"clock_domain",text(s==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC"));put(o.get(),"entry",text(r.entry));put(o.get(),"input_id",text(r.input_id));put(o.get(),"begin_ns",u64(r.begin_ns));put(o.get(),"end_ns",u64(r.end_ns));put(o.get(),"cancelled",boolean(r.cancelled));const Digest*values[]={&r.hardware.hardware_contract,&r.hardware.bit,&r.hardware.xsa,&r.hardware.compatible,&r.model,&r.plan,&r.input};const char*keys[]={"hardware_contract","bit","xsa","compatible","model","plan","input"};for(unsigned n=0;n<7;++n){check(nonzero_digest(*values[n]),"preparation identity missing");put(o.get(),keys[n],text(hex(values[n]->data(),32)));}if(nonzero_digest(r.source))put(o.get(),"source",text(hex(r.source.data(),32)));else nil(o.get(),"source");if(r.error)put(o.get(),"error",error(*r.error));else nil(o.get(),"error");nil(o.get(),"gpu_raw_cycles");return impl_->save(dump(o.get()));}catch(const std::bad_alloc&){return io_error(ENOMEM,"preparation serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}}
Result<void>EvidenceJournal::save_ingress(const RequestKey&key,const BackendIdentity&hw,vxnn::EvidenceScope s,const IngressTiming&t){
 try{check(t.serial&&t.packets&&t.bytes&&t.begin_ns&&t.end_ns>=t.begin_ns&&nonzero_digest(hw.hardware_contract),"native ingress identity/clocks");auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text("native-ingress"));put(o.get(),"request",request(key));put(o.get(),"scope",text(scope(s)));put(o.get(),"clock_domain",text(s==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC"));put(o.get(),"hardware_contract",text(hex(hw.hardware_contract.data(),32)));put(o.get(),"serial",u64(t.serial));put(o.get(),"packets",u64(t.packets));put(o.get(),"wire_bytes",u64(t.bytes));put(o.get(),"protocol_ns",u64(t.protocol_ns));put(o.get(),"prepare_ns",u64(t.prepare_ns));put(o.get(),"complete_ns",u64(t.complete_ns));put(o.get(),"begin_ns",u64(t.begin_ns));put(o.get(),"end_ns",u64(t.end_ns));nil(o.get(),"gpu_raw_cycles");return impl_->save(dump(o.get()));}catch(const std::bad_alloc&){return io_error(ENOMEM,"ingress serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}
}
Result<void>EvidenceJournal::save_frame_timing(const RequestKey&key,const BackendIdentity&hw,vxnn::EvidenceScope s,const IngressTiming&t){
 try{
  check(t.serial&&t.begin_ns&&t.end_ns>=t.begin_ns&&!t.packets&&!t.bytes&&!t.protocol_ns&&t.prepare_ns<=t.end_ns-t.begin_ns&&t.complete_ns<=t.end_ns-t.begin_ns,"native frame zero-wire clocks");check((hw.provider_flags==1||hw.provider_flags==2||s8_joint_platform(hw))&&hw.device_generation,"native frame provider/epoch");
  auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text("native-frame"));put(o.get(),"request",request(key));put(o.get(),"scope",text(scope(s)));put(o.get(),"clock_domain",text(s==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC"));put(o.get(),"timing_boundary",text("before-scene-commit"));
  const Digest*values[]={&hw.hardware_contract,&hw.bit,&hw.xsa,&hw.compatible};const char*keys[]={"hardware_contract","bit","xsa","compatible"};for(unsigned n=0;n<4;++n){check(nonzero_digest(*values[n]),"native frame identity missing");put(o.get(),keys[n],text(hex(values[n]->data(),32)));}
  put(o.get(),"provider_flags",u64(hw.provider_flags));put(o.get(),"device_generation",u64(hw.device_generation));put(o.get(),"serial",u64(t.serial));put(o.get(),"packets",u64(0));put(o.get(),"wire_bytes",u64(0));put(o.get(),"protocol_ns",u64(0));put(o.get(),"prepare_ns",u64(t.prepare_ns));put(o.get(),"complete_precommit_ns",u64(t.complete_ns));put(o.get(),"begin_ns",u64(t.begin_ns));put(o.get(),"end_ns",u64(t.end_ns));nil(o.get(),"gpu_raw_cycles");return impl_->save(dump(o.get()));
 }catch(const std::bad_alloc&){return io_error(ENOMEM,"frame timing serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}
}
Result<void> EvidenceJournal::save_startup(const BackendIdentity&b,BackendMode mode,const std::string&boot,const Digest&config,vxnn::EvidenceScope s,const std::vector<std::string>&apps,std::optional<Digest>installation){
 try{
  check(id(boot)&&nonzero_digest(config)&&b.device_generation&&apps.size()<=64,"startup identity/bounds");check(mode==BackendMode::COMPUTE_V2||mode==BackendMode::LEGACY_COMPAT,"startup mode");
  auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text("startup"));put(o.get(),"boot_id",text(boot));put(o.get(),"scope",text(scope(s)));put(o.get(),"backend_mode",text(mode==BackendMode::COMPUTE_V2?"compute-v2":"legacy-compat"));put(o.get(),"config_sha256",text(hex(config.data(),32)));
  if(installation){check(nonzero_digest(*installation),"installation identity missing");put(o.get(),"installation_sha256",text(hex(installation->data(),32)));}
  const Digest*values[]={&b.hardware_contract,&b.bit,&b.xsa,&b.compatible};const char*keys[]={"hardware_contract","bit","xsa","compatible"};for(unsigned n=0;n<4;n++){check(nonzero_digest(*values[n]),"startup hash missing");put(o.get(),keys[n],text(hex(values[n]->data(),32)));}check(b.provider_flags==1||b.provider_flags==2||s8_joint_platform(b),"startup provider");put(o.get(),"device_generation",u64(b.device_generation));put(o.get(),"provider_flags",u64(b.provider_flags));put(o.get(),"board_identity_verified",boolean(b.board_identity_verified));nil(o.get(),"gpu_raw_cycles");
  auto entries=own(json_object_new_array());for(const auto&a:apps){check(id(a),"startup application ID");auto x=text(a);if(json_object_array_add(entries.get(),x.get()))throw std::bad_alloc();x.release();}put(o.get(),"applications",std::move(entries));return impl_->save(dump(o.get()));
 }catch(const std::bad_alloc&){return io_error(ENOMEM,"startup serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}
}
Result<void>EvidenceJournal::save_display(const BackendIdentity&hw,vxnn::EvidenceScope s,const DisplayObservation&t){
 try{
  check(hw.device_generation&&nonzero_digest(hw.hardware_contract)&&t.raw_events.size()<=256,"display identity/raw-event bound");
  auto o=object();put(o.get(),"format",text("vxgpu-evidence-v1"));put(o.get(),"kind",text("native-display"));put(o.get(),"scope",text(scope(s)));
  const Digest*values[]={&hw.hardware_contract,&hw.bit,&hw.xsa,&hw.compatible};const char*keys[]={"hardware_contract","bit","xsa","compatible"};for(unsigned i=0;i<4;++i)put(o.get(),keys[i],text(hex(values[i]->data(),32)));
  put(o.get(),"device_generation",u64(hw.device_generation));put(o.get(),"provider_flags",u64(hw.provider_flags));
  put(o.get(),"source_lease_token",u64(t.source_lease_token));put(o.get(),"cookie",u64(t.receipt.cookie));put(o.get(),"application_cookie",u64(t.application_cookie));put(o.get(),"result",integer(t.receipt.result));put(o.get(),"clock_domain",integer(static_cast<unsigned>(t.receipt.clock_domain)));
  put(o.get(),"presented",boolean(t.receipt.presented));put(o.get(),"had_old_front",boolean(t.receipt.had_old_front));put(o.get(),"old_retired",boolean(t.receipt.old_retired));
  if(t.receipt.submitted_ns)put(o.get(),"submitted_ns",u64(*t.receipt.submitted_ns));else nil(o.get(),"submitted_ns");if(t.receipt.presented_ns)put(o.get(),"presented_ns",u64(*t.receipt.presented_ns));else nil(o.get(),"presented_ns");if(t.receipt.old_retired_ns)put(o.get(),"old_retired_ns",u64(*t.receipt.old_retired_ns));else nil(o.get(),"old_retired_ns");
  const uint64_t times[]={t.compose_begin_ns,t.compose_end_ns,t.copy_begin_ns,t.copy_end_ns,t.poll_begin_ns,t.poll_end_ns};const char*names[]={"compose_begin_ns","compose_end_ns","copy_begin_ns","copy_end_ns","poll_begin_ns","poll_end_ns"};for(unsigned i=0;i<6;++i)put(o.get(),names[i],u64(times[i]));
  put(o.get(),"pending",boolean(t.pending));put(o.get(),"restored",boolean(t.restored));nil(o.get(),"gpu_raw_cycles");
  if(t.first_error){auto e=object();put(e.get(),"code",integer(t.first_error->code));put(e.get(),"category",text(t.first_error->category));put(e.get(),"detail",text(t.first_error->detail));put(o.get(),"first_error",std::move(e));}else nil(o.get(),"first_error");
  put(o.get(),"raster_cache_budget_bytes",u64(t.raster_cache_budget_bytes));auto cache=object();put(cache.get(),"mapping_builds",u64(t.raster_cache.mapping_builds));put(cache.get(),"mapping_hits",u64(t.raster_cache.mapping_hits));put(cache.get(),"buffer_allocations",u64(t.raster_cache.buffer_allocations));put(cache.get(),"buffer_reuses",u64(t.raster_cache.buffer_reuses));put(cache.get(),"identity_frames",u64(t.raster_cache.identity_frames));put(cache.get(),"scaled_frames",u64(t.raster_cache.scaled_frames));put(cache.get(),"retained_canvases",u64(t.raster_cache.retained_canvases));put(cache.get(),"retained_bytes",u64(t.raster_cache.retained_bytes));put(o.get(),"raster_cache",std::move(cache));
  auto events=own(json_object_new_array());for(const auto&r:t.raw_events){auto e=object();put(e.get(),"kind",integer(static_cast<unsigned>(r.kind)));put(e.get(),"cookie",u64(r.cookie));put(e.get(),"received_ns",u64(r.received_ns));put(e.get(),"kernel_ns",u64(r.kernel_ns));put(e.get(),"kernel_clock_known",boolean(r.kernel_clock_known));put(e.get(),"run_id",text(std::string(r.run_id,strnlen(r.run_id,sizeof(r.run_id)))));put(e.get(),"frame_sha",text(std::string(r.frame_sha,strnlen(r.frame_sha,sizeof(r.frame_sha)))));put(e.get(),"scanout_sha",text(std::string(r.scanout_sha,strnlen(r.scanout_sha,sizeof(r.scanout_sha)))));put(e.get(),"width",u64(r.width));put(e.get(),"height",u64(r.height));put(e.get(),"pitch",u64(r.pitch));put(e.get(),"conversion_ns",u64(r.conversion_ns));put(e.get(),"source_sample_bytes",u64(r.conversion.source_bytes));put(e.get(),"scanout_pixel_bytes",u64(r.conversion.pixel_bytes));put(e.get(),"scanout_clear_bytes",u64(r.conversion.clear_bytes));put(e.get(),"identity_copy",boolean(r.conversion.identity));check(!json_object_array_add(events.get(),e.get()),"display event allocation");e.release();}put(o.get(),"raw_events",std::move(events));return impl_->save(dump(o.get()));
 }catch(const std::bad_alloc&){return io_error(ENOMEM,"display serialization allocation");}catch(const std::exception&e){return io_error(EINVAL,e.what());}
}

}

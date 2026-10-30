#include <vxunified/backend.hpp>
#include "logical_cookie_history.hpp"
#include <sys/ioctl.h>
#include <time.h>
#include <map>
#include <mutex>
#include <cstring>
#include <set>
namespace vxunified {
int FdTransport::call(unsigned long cmd,void *p){int rc=::ioctl(fd_,cmd,p);return rc<0?-errno:rc;}
uint64_t FdTransport::now_ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
namespace {
template<class T> Result<T> failed(int rc,const char *detail){return Result<T>::failure({rc,"compute-v2",detail});}
BufferRef public_ref(const vxl_v2_ref& r){return {r.handle,r.object_generation,r.offset,r.length,r.access,r.device_generation};}
vxl_compute_v2_request request(uint64_t epoch){vxl_compute_v2_request r{};r.major=2;r.size=128;r.device_generation=epoch;return r;}
bool nonzero(const uint8_t *p){for(unsigned i=0;i<32;++i)if(p[i])return true;return false;}
}
struct ComputeV2Backend::Impl {
 IoctlTransport& io;std::recursive_mutex mutex;vxl_v2_caps caps{};bool uncertain=false,fast=false,submit_attempted=false;
 std::map<uint64_t,vxl_v2_ref> buffers;std::map<uint64_t,vxl_v2_program_desc> programs;
 std::map<uint64_t,uint32_t> program_users;
 struct StagedJob {BufferRef args;uint64_t wire_cookie=0,sequence=0;bool ambiguous_ack=false,acknowledged=false;};
 std::map<uint64_t,StagedJob> staging;uint64_t last_wire_cookie=0;
 detail::LogicalCookieHistory logical_history;
 // Bounded by the driver's 64-object pool. Allocate this binding before SUBMIT;
 // filling sequence/ACK flags afterwards never allocates or rewrites raw bytes.
 auto by_sequence(uint64_t sequence)->decltype(staging.begin()){
  if(sequence)for(auto i=staging.begin();i!=staging.end();++i)if(i->second.sequence==sequence)return i;
  return staging.end();
 }
 auto by_wire(uint64_t cookie)->decltype(staging.begin()){
  if(cookie)for(auto i=staging.begin();i!=staging.end();++i)if(i->second.wire_cookie==cookie)return i;
  return staging.end();
 }
 explicit Impl(IoctlTransport& i):io(i){}
 int call(unsigned long cmd,vxl_compute_v2_request& r){int rc=io.call(cmd,&r);return rc>0?-EPROTO:rc;}
 int ensure(){if(uncertain)return -EIO;return caps.device_generation?0:refresh();}
 int refresh(){
  vxl_v2_caps next{};auto r=request(0);r.user_ptr=reinterpret_cast<uintptr_t>(&next);r.payload_bytes=256;
  int rc=call(VXL_V2_CAPS,r);if(rc)return rc;
  if(next.version!=2 || next.bytes!=256 || next.contexts!=1 || next.max_objects!=64 || next.max_pending!=32 || next.max_completions!=64 || next.max_refs!=8 || next.max_args!=4096 || next.max_programs!=16 || next.cycle_bits!=32 || !next.device_generation || next.device_generation!=r.device_generation || (next.feature_mask!=1 && next.feature_mask!=2 && next.feature_mask!=4) || next.identity_source!=VXL_V2_ID_BOOT_MANIFEST_DECLARED || !nonzero(next.hardware_contract) || !nonzero(next.bit) || !nonzero(next.xsa) || !nonzero(next.compatible))return -EPROTO;
  if(caps.device_generation && (caps.device_generation!=next.device_generation || std::memcmp(caps.hardware_contract,next.hardware_contract,128) || caps.feature_mask!=next.feature_mask)){uncertain=true;return -ESTALE;}
  if(fast && !(next.flags&VXL_V2_CAP_FAST_RUNTIME))return -EOPNOTSUPP;
  caps=next;return 0;
 }
 Result<vxl_v2_ref> ref(BufferRef b,uint32_t access){
  auto i=buffers.find(b.handle);
  if(i==buffers.end())return failed<vxl_v2_ref>(-EBADF,"unknown buffer");
  const auto& stored=i->second;
  if(b.object_generation!=stored.object_generation || b.device_generation!=caps.device_generation)return failed<vxl_v2_ref>(-ESTALE,"buffer epoch");
  if(!b.bytes || b.offset>stored.length || b.bytes>stored.length-b.offset || !b.access || (b.access&~stored.access) || (access&~b.access))return failed<vxl_v2_ref>(-EINVAL,"buffer range/access");
  auto wire=stored;wire.offset=b.offset;wire.length=b.bytes;wire.access=b.access;return Result<vxl_v2_ref>::success(wire);
 }
 Result<BufferRef> create(uint64_t bytes,vxnn::AddressDomain domain,uint32_t access,uint32_t purpose){
  int rc=ensure();if(rc)return failed<BufferRef>(rc,"device unavailable");
  vxl_v2_create_desc desc{};desc.domain=uint32_t(domain);desc.purpose=purpose;desc.access=access;desc.bytes=bytes;
  auto r=request(caps.device_generation);r.user_ptr=reinterpret_cast<uintptr_t>(&desc);r.payload_bytes=64;rc=call(VXL_V2_BUFFER_CREATE,r);
  if(rc){if(rc==-EFAULT)uncertain=true;return failed<BufferRef>(rc,"create; any unknown allocation stays fd-owned");}
  vxl_v2_buffer_info info{};std::memcpy(&info,&desc,64);
  if(!info.ref.handle || !info.ref.object_generation || info.ref.offset || info.ref.length!=bytes || info.ref.domain!=uint32_t(domain) || info.ref.access!=access || info.ref.device_generation!=caps.device_generation || info.ref.handle!=r.handle || info.ref.object_generation!=r.object_generation || info.purpose!=purpose || info.sealed || info.allocated<bytes || (info.allocated&63)){uncertain=true;return failed<BufferRef>(-EPROTO,"invalid create result");}
  buffers.emplace(info.ref.handle,info.ref);return Result<BufferRef>::success(public_ref(info.ref));
 }
 int copy(vxl_v2_copy_desc& d){
  if(d.user_ptr && d.bytes>~uint64_t(0)-d.user_ptr)return -EOVERFLOW;
  if(d.direction==3 && d.source.handle==d.destination.handle && d.source.offset<d.destination.offset+d.bytes && d.destination.offset<d.source.offset+d.bytes)return -EINVAL;
  uint64_t done=0;
  while(done<d.bytes){auto part=d;uint64_t n=d.bytes-done;if(n>67108864)n=67108864;part.bytes=n;
   if(d.direction!=1){part.source.offset+=done;part.source.length=n;}
   if(d.direction!=2){part.destination.offset+=done;part.destination.length=n;}
   if(part.user_ptr)part.user_ptr+=done;
   auto r=request(caps.device_generation);r.user_ptr=reinterpret_cast<uintptr_t>(&part);r.payload_bytes=128;
   int rc=call(VXL_V2_BUFFER_COPY,r);if(rc)return rc;done+=n;
  }
  return 0;
 }
 Result<vxl_v2_completion> read(uint64_t sequence,uint64_t cookie,unsigned long cmd,uint64_t milliseconds=0){
  uint64_t wire=0;
  if(cookie){auto i=staging.find(cookie);if(i==staging.end())return failed<vxl_v2_completion>(-ENOENT,"unknown logical cookie");wire=i->second.wire_cookie;if(sequence&&i->second.sequence&&sequence!=i->second.sequence)return failed<vxl_v2_completion>(-ESTALE,"logical cookie/sequence differs");}
  vxl_v2_completion out{};auto r=request(caps.device_generation);r.sequence=sequence;r.cookie=wire;r.user_ptr=reinterpret_cast<uintptr_t>(&out);r.payload_bytes=256;r.length=milliseconds;
  if(cmd==VXL_V2_COMPLETE_REAP)r.flags=VXL_V2_READ;
  int rc=call(cmd,r);if(rc)return failed<vxl_v2_completion>(rc,"completion not consumed");
  const uint8_t fast_policy[8]={1,0,0,0,3,0,0,0},strict_policy[8]={};
  bool policy_ok=fast?(out.version==VXL_V2_COMPLETION_FAST_VERSION && out.reserved0==VXL_V2_COMPLETION_FAST_UNCHECKED && !std::memcmp(out.reserved,fast_policy,8)):(out.version==2 && !out.reserved0 && !std::memcmp(out.reserved,strict_policy,8));
  if(!policy_ok || out.bytes!=256 || !out.sequence || !out.cookie || out.device_generation!=caps.device_generation || (sequence && out.sequence!=sequence) || (wire && out.cookie!=wire) || out.sequence!=r.sequence || out.cookie!=r.cookie || out.cycles_valid>1 || (out.cycles_valid && (!out.elapsed_raw || out.elapsed_raw>=out.watchdog)))return failed<vxl_v2_completion>(-EPROTO,"completion identity/counter/integrity policy");
  auto i=by_wire(out.cookie),known=by_sequence(out.sequence);
  if(i==staging.end()||(cookie&&i->first!=cookie)||(i->second.sequence&&i->second.sequence!=out.sequence)||(known!=staging.end()&&known!=i))return failed<vxl_v2_completion>(-EPROTO,"unbound original completion");
  i->second.sequence=out.sequence;return Result<vxl_v2_completion>::success(out);
 }
};
ComputeV2Backend::ComputeV2Backend(IoctlTransport& io):impl_(new Impl(io)){}
ComputeV2Backend::~ComputeV2Backend()=default;
Result<void> ComputeV2Backend::set_fast_runtime(bool enabled){
 std::lock_guard<std::recursive_mutex>lock(impl_->mutex);
 if(impl_->submit_attempted && enabled!=impl_->fast)return failed<void>(-EBUSY,"integrity policy frozen after submit attempt");
 int rc=impl_->refresh();if(rc)return failed<void>(rc,"integrity capability query");
 if(enabled && !(impl_->caps.flags&VXL_V2_CAP_FAST_RUNTIME))return failed<void>(-EOPNOTSUPP,"FAST runtime capability absent; no fallback");
 impl_->fast=enabled;return Result<void>::success();
}
bool ComputeV2Backend::fast_runtime()const{std::lock_guard<std::recursive_mutex>lock(impl_->mutex);return impl_->fast;}
Result<BackendIdentity> ComputeV2Backend::identity(){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->refresh();if(rc)return failed<BackendIdentity>(rc,"identity query");
 BackendIdentity out;auto& c=impl_->caps;std::copy(c.hardware_contract,c.hardware_contract+32,out.hardware_contract.begin());std::copy(c.bit,c.bit+32,out.bit.begin());std::copy(c.xsa,c.xsa+32,out.xsa.begin());std::copy(c.compatible,c.compatible+32,out.compatible.begin());out.device_generation=c.device_generation;out.provider_flags=c.feature_mask;return Result<BackendIdentity>::success(out);
}
Result<DeviceCaps> ComputeV2Backend::capabilities(){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->refresh();if(rc)return failed<DeviceCaps>(rc,"caps query");
 DeviceCaps c;c.ps_base=impl_->caps.ps_base;c.ps_bytes=impl_->caps.ps_bytes;c.pl_base=impl_->caps.pl_base;c.pl_bytes=impl_->caps.pl_bytes;c.ps_dma_bits=32;c.gpu_address_bits=32;c.pl_address_bits=40;c.cycle_counter_bits=32;return Result<DeviceCaps>::success(c);
}
Result<BufferRef> ComputeV2Backend::allocate(uint64_t bytes,vxnn::AddressDomain domain,uint32_t access){std::lock_guard<std::recursive_mutex> lock(impl_->mutex);return impl_->create(bytes,domain,access,3);}
Result<void> ComputeV2Backend::copy(BufferRef b,vxnn::Bytes bytes){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->ensure();if(rc)return failed<void>(rc,"device unavailable");auto ref=impl_->ref(b,2);if(!ref)return failed<void>(ref.error->code,"destination");
 if(!bytes.data || bytes.size!=b.bytes)return failed<void>(-EINVAL,"copy bytes");
 vxl_v2_copy_desc d{};d.destination=*ref.value;d.bytes=bytes.size;d.user_ptr=reinterpret_cast<uintptr_t>(bytes.data);d.direction=1;rc=impl_->copy(d);return rc?failed<void>(rc,"upload"):Result<void>::success();
}
Result<void> ComputeV2Backend::copy(BufferRef b,vxnn::MutableBytes bytes){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->ensure();if(rc)return failed<void>(rc,"device unavailable");auto ref=impl_->ref(b,1);if(!ref)return failed<void>(ref.error->code,"source");
 if(!bytes.data || bytes.size!=b.bytes)return failed<void>(-EINVAL,"copy bytes");
 vxl_v2_copy_desc d{};d.source=*ref.value;d.bytes=bytes.size;d.user_ptr=reinterpret_cast<uintptr_t>(bytes.data);d.direction=2;rc=impl_->copy(d);return rc?failed<void>(rc,"download"):Result<void>::success();
}
Result<void> ComputeV2Backend::copy(BufferRef source,BufferRef destination){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->ensure();if(rc)return failed<void>(rc,"device unavailable");auto src=impl_->ref(source,1);auto dst=impl_->ref(destination,2);
 if(!src || !dst || source.bytes!=destination.bytes)return failed<void>(-EINVAL,"copy references");
 vxl_v2_copy_desc d{};d.source=*src.value;d.destination=*dst.value;d.bytes=source.bytes;d.direction=3;rc=impl_->copy(d);return rc?failed<void>(rc,"device copy"):Result<void>::success();
}
Result<void> ComputeV2Backend::release_buffer(BufferRef b){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);auto ref=impl_->ref(b,0);if(!ref)return failed<void>(ref.error->code,"release reference");
 if(b.offset || b.bytes!=impl_->buffers.at(b.handle).length)return failed<void>(-EINVAL,"release requires whole base object");
 auto r=request(impl_->caps.device_generation);r.handle=b.handle;r.object_generation=b.object_generation;int rc=impl_->call(VXL_V2_BUFFER_RELEASE,r);if(rc)return failed<void>(rc,"buffer retained");impl_->buffers.erase(b.handle);return Result<void>::success();
}
Result<ProgramRef> ComputeV2Backend::program_load(const Digest& entry){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);int rc=impl_->ensure();if(rc)return failed<ProgramRef>(rc,"device unavailable");vxl_v2_program_desc p{};std::copy(entry.begin(),entry.end(),p.entry_id);auto r=request(impl_->caps.device_generation);r.user_ptr=reinterpret_cast<uintptr_t>(&p);r.payload_bytes=64;rc=impl_->call(VXL_V2_PROGRAM_LOAD,r);if(rc)return failed<ProgramRef>(rc,"catalog load");
 if(!p.handle || !p.object_generation || p.device_generation!=impl_->caps.device_generation || std::memcmp(p.entry_id,entry.data(),32))return failed<ProgramRef>(-EPROTO,"program identity");
 impl_->programs[p.handle]=p;impl_->program_users[p.handle]++;return Result<ProgramRef>::success({p.handle,p.object_generation,p.device_generation,entry});
}
Result<ProgramRef> ComputeV2Backend::program_identity(const JobDesc&job){
 std::lock_guard<std::recursive_mutex>lock(impl_->mutex);int rc=impl_->ensure();if(rc)return failed<ProgramRef>(rc,"program identity unavailable");
 auto it=impl_->programs.find(job.program_handle);
 if(it==impl_->programs.end()||!impl_->program_users.count(job.program_handle)||job.device_generation!=impl_->caps.device_generation||it->second.device_generation!=job.device_generation||it->second.object_generation!=job.program_generation)return failed<ProgramRef>(-ESTALE,"admitted program generation differs");
 ProgramRef p;p.handle=it->second.handle;p.program_generation=it->second.object_generation;p.device_generation=it->second.device_generation;std::copy(it->second.entry_id,it->second.entry_id+32,p.program_sha.begin());return Result<ProgramRef>::success(p);
}
Result<void> ComputeV2Backend::release_program(ProgramRef p){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);auto i=impl_->programs.find(p.handle);if(i==impl_->programs.end() || i->second.object_generation!=p.program_generation || i->second.device_generation!=p.device_generation || std::memcmp(i->second.entry_id,p.program_sha.data(),32))return failed<void>(-ESTALE,"program reference");
 auto r=request(p.device_generation);r.handle=p.handle;r.object_generation=p.program_generation;int rc=impl_->call(VXL_V2_PROGRAM_RELEASE,r);if(rc)return failed<void>(rc,"program retained");if(!--impl_->program_users[p.handle]){impl_->program_users.erase(p.handle);impl_->programs.erase(i);}return Result<void>::success();
}
Result<uint64_t> ComputeV2Backend::submit(const JobDesc& input){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);impl_->submit_attempted=true;int rc=impl_->ensure();if(rc)return failed<uint64_t>(rc,"device unavailable");
 if(!input.cookie || impl_->staging.count(input.cookie))return failed<uint64_t>(-EALREADY,"cookie retained; no resubmit");
 if(impl_->logical_history.contains(input.cookie))return failed<uint64_t>(-ESTALE,"logical cookie already attempted in this owner epoch");
 if(impl_->last_wire_cookie==UINT64_MAX)return failed<uint64_t>(-EOVERFLOW,"submit-order cookie exhausted; owner must close");
 auto program=impl_->programs.find(input.program_handle);
 if(input.device_generation!=impl_->caps.device_generation || program==impl_->programs.end() || program->second.object_generation!=input.program_generation || input.references.size()>8 || !input.args.bytes || input.args.bytes>4096 || (input.args.bytes&3))return failed<uint64_t>(-EINVAL,"job identity/size");
 vxl_v2_job job{};job.major=2;job.size=512;job.flags=impl_->fast?VXL_V2_JOB_FAST_RUNTIME:0;job.program_handle=input.program_handle;job.program_generation=input.program_generation;job.device_generation=input.device_generation;job.num_refs=uint32_t(input.references.size());
 job.cookie=input.cookie>impl_->last_wire_cookie?input.cookie:impl_->last_wire_cookie+1;
 for(size_t i=0;i<input.references.size();++i){auto ref=impl_->ref(input.references[i],input.references[i].access);if(!ref)return failed<uint64_t>(ref.error->code,"data reference");job.refs[i]=*ref.value;}
 std::vector<uint8_t> arguments(size_t(input.args.bytes));auto read=copy(input.args,vxnn::MutableBytes{arguments.data(),arguments.size()});if(!read)return failed<uint64_t>(read.error->code,"args download");
 if(input.wire_cookie_arg_offset){uint32_t offset=*input.wire_cookie_arg_offset;if((offset&3)||arguments.size()<8||offset>arguments.size()-8)return failed<uint64_t>(-EINVAL,"private cookie scalar range");uint64_t logical=0;for(unsigned i=0;i<8;++i)logical|=uint64_t(arguments[offset+i])<<(8*i);if(logical!=input.cookie)return failed<uint64_t>(-ESTALE,"private cookie scalar differs from logical work");for(unsigned i=0;i<8;++i)arguments[offset+i]=uint8_t(job.cookie>>(8*i));}
 auto stage=impl_->create(input.args.bytes,vxnn::AddressDomain::PL,3,2);if(!stage)return failed<uint64_t>(stage.error->code,"ARGS staging allocation");
 auto uploaded=copy(*stage.value,vxnn::Bytes{arguments.data(),arguments.size()});uint64_t now=impl_->io.now_ns();
 if(!uploaded || !now || input.deadline_ns<=now || input.deadline_ns-now>60000000000ULL){auto released=release_buffer(*stage.value);if(!released)impl_->uncertain=true;return failed<uint64_t>(!uploaded?uploaded.error->code:-ETIMEDOUT,"args upload/deadline");}
 job.args_handle=stage.value->handle;job.args_generation=stage.value->object_generation;job.args_bytes=stage.value->bytes;job.execution_budget_ns=input.deadline_ns-now;
 // Logical namespaces describe preparation order, not dispatch order. Preserve
 // the usual logical value when increasing; otherwise reserve the next wire
 // identity at this serialized ioctl boundary. Never roll back consumed values.
 rc=impl_->logical_history.record_attempt(input.cookie);
 if(rc){auto released=release_buffer(*stage.value);if(!released)impl_->uncertain=true;return failed<uint64_t>(rc,"logical identity history full; no submission or reset");}
 auto retained=impl_->staging.emplace(input.cookie,Impl::StagedJob{*stage.value,job.cookie,0,false,false}).first;
 impl_->last_wire_cookie=job.cookie;auto r=request(impl_->caps.device_generation);r.user_ptr=reinterpret_cast<uintptr_t>(&job);r.payload_bytes=512;r.length=1;rc=impl_->call(VXL_V2_SUBMIT_BATCH,r);
 if(rc){if(rc!=-EFAULT && rc!=-EIO && rc!=-ETIMEDOUT){auto released=release_buffer(*stage.value);if(released)impl_->staging.erase(input.cookie);}return failed<uint64_t>(rc,"submission error retained; never automatically retried");}
 if(!r.sequence || r.cookie!=job.cookie || impl_->by_sequence(r.sequence)!=impl_->staging.end())return failed<uint64_t>(-EPROTO,"accepted submission identity");
 retained->second.sequence=r.sequence;return Result<uint64_t>::success(r.sequence);
}
Result<uint64_t> ComputeV2Backend::submitted_wire_cookie(uint64_t logical){std::lock_guard<std::recursive_mutex>lock(impl_->mutex);auto i=impl_->staging.find(logical);return i==impl_->staging.end()?failed<uint64_t>(-ENOENT,"no retained logical submission"):Result<uint64_t>::success(i->second.wire_cookie);}
Result<void> ComputeV2Backend::wait(uint64_t cookie,uint64_t timeout){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);if(timeout>60000000000ULL)return failed<void>(-EINVAL,"wait limit");auto out=impl_->read(0,cookie,VXL_V2_WAIT,(timeout+999999)/1000000);return out?Result<void>::success():failed<void>(out.error->code,"wait does not cancel");
}
Result<vxl_v2_completion> ComputeV2Backend::detail(uint64_t sequence,uint64_t cookie){std::lock_guard<std::recursive_mutex> lock(impl_->mutex);return impl_->read(sequence,cookie,VXL_V2_COMPLETE_REAP);}
Result<Completion> ComputeV2Backend::read_completion(uint64_t sequence){
 std::lock_guard<std::recursive_mutex>lock(impl_->mutex);auto raw=impl_->read(sequence,0,VXL_V2_COMPLETE_REAP);if(!raw)return failed<Completion>(raw.error->code,"completion retained");auto& r=*raw.value;auto binding=impl_->by_wire(r.cookie);if(binding==impl_->staging.end())return failed<Completion>(-EPROTO,"completion binding lost");Completion out;out.cookie=binding->first;out.wire_cookie=r.cookie;out.sequence=r.sequence;out.status=r.state;out.result=r.result;out.cycles_valid=r.cycles_valid;if(out.cycles_valid)out.raw_cycles=r.elapsed_raw;out.timing_events={r.submit_ns,r.start_ns,r.done_seen_ns,r.ack_ns,r.complete_ns};return Result<Completion>::success(out);
}
Result<void> ComputeV2Backend::ack_reap(uint64_t sequence){
 std::lock_guard<std::recursive_mutex> lock(impl_->mutex);auto i=impl_->by_sequence(sequence);if(i==impl_->staging.end())return failed<void>(-ENOENT,"unknown sequence");auto&stage=i->second;
 if(!stage.acknowledged){auto r=request(impl_->caps.device_generation);r.flags=VXL_V2_ACK;r.sequence=sequence;r.cookie=stage.wire_cookie;int rc=impl_->call(VXL_V2_COMPLETE_REAP,r);
  if(rc && !(rc==-ESTALE && stage.ambiguous_ack)){if(rc==-EFAULT)stage.ambiguous_ack=true;return failed<void>(rc,"ACK failure retains staging");}stage.acknowledged=true;
 }
 auto released=release_buffer(stage.args);if(!released)return failed<void>(released.error->code,"retired job staging release");impl_->staging.erase(i);return Result<void>::success();
}
}

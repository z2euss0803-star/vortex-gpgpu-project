#include <vxunified/image_request.hpp>
#include <vxunified/native_admission.hpp>
#include <vxunified/detail/image_fill.hpp>
#include <vxl/hunet.h>
#include "generated/image/programs.h"
#include <cstring>
#include <mutex>
#include <time.h>
namespace vxunified {namespace {
uint64_t now(){timespec t{};
 if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
 return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
 }
vxnn::Error err(int n,const char*s){return {n,"IMAGE_OWNER",s};
 }
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;
 }
Digest hash(vxnn::Bytes b){Digest d{};
 vxl_hunet_hash(b.data,b.size,d.data());
 return d;
 }
template<class F>auto timed(uint64_t&sum,F f)->decltype(f()){
 using R=decltype(f());
 auto begin=now();
 if(!begin)return R::failure(err(EIO,"clock unavailable"));
 auto r=f();
 auto end=now();
 if(end<begin||!end||end-begin>UINT64_MAX-sum)return R::failure(err(EOVERFLOW,"clock order/overflow"));
 sum+=end-begin;
 return r;
}
constexpr uint64_t root_bytes=VX_IMAGE_RESERVED_BYTES+192ULL;
uint32_t payload(uint32_t slot,uint32_t width,uint32_t height){return slot==10?65536:width*height*4;
 }
uint8_t guard_value(unsigned slot){return uint8_t(0x91u^slot);
 }
}
struct ImageSession::Impl {
 mutable std::mutex mutex;
 std::shared_ptr<ImageAdapter>adapter;
 std::shared_ptr<const InstalledCatalog>catalog;
 std::shared_ptr<ProgramResidency>residency;
 std::shared_ptr<UnifiedBackend>backend;
 std::shared_ptr<ResidentLease>native_charge;
 std::weak_ptr<Runtime>native_runtime;
 DeviceCaps caps;
 Digest implementation{},reference{};
 vxnn::EvidenceScope scope;
 std::vector<uint8_t>shadow,table;
 std::optional<vxnn::Error>fault;
 bool pending=false;
 uint32_t input_id=0,width=0,height=0;
};
struct ImageWorkRequest::Impl {
 std::shared_ptr<ImageSession>session;
 std::shared_ptr<HostMemoryLease>input_charge;
 vxnn::Plan plan;
 vx_image_plan recipe{};
 ImageRequest input;
 WorkRequest metadata;
 Digest source{};
 ImageTraceSink trace;
 ImageOutputSink output;
 ImageOwnerReceipt receipt;
 ImageJobTrace row;
 uint64_t budget=0;
 std::optional<ResidencyLease>lease;
 std::optional<BufferRef>root;
 std::optional<WorkOutcome>previous;
 std::optional<vxl_v2_completion>detail;
 bool active=false,shadow_initialized=false,publishing=false;
 uint32_t current_op=0;
 ImageSession::Impl&s(){return *session->impl_;
 }const ImageSession::Impl&s()const{return *session->impl_;
 }
 Result<void>fail(vxnn::Error e,bool report=true){
  if(!receipt.first_error)receipt.first_error=e;
 if(!s().fault)s().fault=e;
  if(row.begin_ns&&report){row.error=e.code;
 row.end_ns=now();
 try{auto saved=trace(metadata.request,row);
 if(!saved&&!receipt.evidence_error)receipt.evidence_error=*saved.error;
 }catch(...){if(!receipt.evidence_error)receipt.evidence_error=err(EIO,"trace exception while saving error");
 }}
  return Result<void>::failure(*receipt.first_error);
 }
 void initialize_shadow(){
 if(shadow_initialized)return;
 auto&shared=s();
 if(shared.input_id!=input.input_id){
  detail::clear_image_shadow(shared.shadow.data(),shared.shadow.size());
 for(unsigned slot=0;slot<33;slot++){auto*base=shared.shadow.data()+uint64_t(slot)*IMAGE_SLOT_SIZE;
 const auto size=payload(slot,input.width,input.height);
 std::fill_n(base,64,guard_value(slot));
 std::fill_n(base+64+size,64,guard_value(slot));
 if(slot!=10)detail::fill_image_background(base+64,size);
 }
  std::memcpy(shared.shadow.data()+64,input.rgba->data(),input.rgba->size());
 std::memcpy(shared.shadow.data()+10ULL*IMAGE_SLOT_SIZE+64,shared.table.data(),shared.table.size());
 shared.input_id=input.input_id;
 shared.width=input.width;
 shared.height=input.height;
 }
 shadow_initialized=true;
 }
 ProgramKey key(unsigned op)const{ProgramKey p;
 p.namespace_id=3;
 p.logical_profile=256;
 p.logical_program=512+op;
 p.hardware_contract=plan.policy.hardware.hardware_contract;
 std::copy_n(vx_image_code_sha[op],32,p.code.begin());
 std::copy_n(vx_image_schema_sha[op],32,p.schema.begin());
 return p;
 }
 BufferRef view(uint64_t offset,uint64_t n,uint32_t access)const{auto v=*root;
 v.offset=offset;
 v.bytes=n;
 v.access=access;
 return v;
 }
 Result<void>guard_check(){
  std::array<uint8_t,64>bytes{};
  for(unsigned i=0;i<35;i++){
   unsigned slot=i<33?i:0;
 uint64_t offsets[2]={uint64_t(slot)*IMAGE_SLOT_SIZE+64,uint64_t(slot)*IMAGE_SLOT_SIZE+128+payload(slot,input.width,input.height)};
 unsigned count=i<33?2:1;
 uint8_t value=i<33?guard_value(slot):0x53;
   if(i>=33)offsets[0]=i==33?0:64ULL+VX_IMAGE_RESERVED_BYTES;
   for(unsigned j=0;j<count;j++){auto copied=s().backend->copy(view(offsets[j],64,1),vxnn::MutableBytes{bytes.data(),bytes.size()});
 if(!copied)return copied;
 if(!std::all_of(bytes.begin(),bytes.end(),[&](uint8_t v){return v==value;
 }))return Result<void>::failure(err(EBADMSG,"image dual/outer guard corrupted"));
 }
  }
  std::vector<uint8_t>table(65536);
 auto copied=s().backend->copy(view(64ULL+10ULL*IMAGE_SLOT_SIZE+64,65536,1),vxnn::MutableBytes{table.data(),table.size()});
 if(!copied)return copied;
 if(hash({table.data(),table.size()})!=hash({s().table.data(),s().table.size()}))return Result<void>::failure(err(EBADMSG,"GPU read-only table changed"));
 return Result<void>::success();
 }
 Result<void>materialize(unsigned op){
  initialize_shadow();
  auto loaded=timed(row.program_ns,[&](){
   if(!lease){auto r=s().residency->acquire(metadata.request,key(op));
 if(!r)return Result<void>::failure(*r.error);
 lease=*r.value;
 current_op=op;
 return Result<void>::success();
 }
   if(current_op==op)return Result<void>::success();
 if(!previous||!detail)return Result<void>::failure(err(EBUSY,"program switch without original previous ACK"));
   auto r=s().residency->switch_after_ack(*lease,key(op),*previous,*detail);
 if(!r)return Result<void>::failure(*r.error);
 lease=*r.value;
 current_op=op;
 return Result<void>::success();
  });
 if(!loaded)return loaded;
  if(receipt.materialized)return Result<void>::success();
  auto allocated=timed(row.allocate_ns,[&](){auto r=s().backend->allocate(root_bytes,vxnn::AddressDomain::PL,3);
 if(!r)return Result<void>::failure(*r.error);
 root=*r.value;
 return Result<void>::success();
 });
 if(!allocated)return allocated;
  auto copied=timed(row.upload_ns,[&](){
   std::array<uint8_t,64>guard;
 guard.fill(0x53);
 auto r=s().backend->copy(view(0,64,2),vxnn::Bytes{guard.data(),64});
 if(!r)return r;
 auto tail=s().backend->copy(view(64ULL+VX_IMAGE_RESERVED_BYTES,64,2),vxnn::Bytes{guard.data(),64});
 if(!tail)return tail;
   return s().backend->copy(view(64,VX_IMAGE_RESERVED_BYTES,2),vxnn::Bytes{s().shadow.data(),s().shadow.size()});
  });
 if(!copied)return copied;
 receipt.materialized=true;
 return Result<void>::success();
 }
 Result<void>check(const WorkItem&w,UnifiedBackend&b,bool gpu,bool finishing){
  if(receipt.first_error)return Result<void>::failure(*receipt.first_error);
  auto expected=receipt.completed_jobs;
  if(&b!=s().backend.get()||receipt.retired||!same(w.request,metadata.request)||w.app!=AppKind::IMAGE||w.node!=w.work_index+1||w.work_index!=expected||active!=finishing||w.kind!=(gpu?WorkKind::GPU:WorkKind::TRANSFER)||(gpu?w.work_index>=recipe.count:w.work_index!=recipe.count))return Result<void>::failure(err(ESTALE,"exact image request/node/backend/retirement binding required"));
  if(gpu){auto*p=std::get_if<DeferredGpuRef>(&w.operation);
 if(!p||p->registered_name!="image.controlled.rgba.v1"||p->node!=w.node||p->device_generation!=plan.policy.hardware.device_generation||p->cookie!=(uint64_t(input.cookie_namespace)<<32)+w.work_index+1)return Result<void>::failure(err(ESTALE,"image deferred cookie/program epoch differs"));
 }
  return Result<void>::success();
 }
 Result<void>release(){
  if(receipt.retired)return Result<void>::success();
  if(root){auto r=s().backend->release_buffer(*root);
 if(!r)return r;
 root.reset();
 }
  if(lease){auto r=s().residency->release(*lease,true);
 if(!r)return r;
 lease.reset();
 }
  receipt.retired=true;
 s().pending=false;
 return Result<void>::success();
 }
 Result<void>publish(ExecutionReceipt&r,std::unique_lock<std::mutex>&session_lock){
  if(!session_lock.owns_lock()||session_lock.mutex()!=&s().mutex)return fail(err(EPERM,"publication requires the exact session lock"),false);
  if(!receipt.render_begin_ns)receipt.render_begin_ns=now();
  if(!receipt.render_begin_ns)return fail(err(EIO,"render begin clock"),false);
  initialize_shadow();
  if(root){auto downloaded=timed(receipt.download_ns,[&](){auto checked=guard_check();
 if(!checked)return checked;
 return s().backend->copy(view(64,VX_IMAGE_RESERVED_BYTES,1),vxnn::MutableBytes{s().shadow.data(),s().shadow.size()});
 });
 if(!downloaded)return fail(*downloaded.error,false);
 }
  uint32_t slot=0,offset=0;
 int rc=vx_image_slot(recipe.output_token,input.width*input.height*4,0,&slot,&offset);
 if(rc)return fail(err(-rc,"bounded final logical output"),false);
  const vxnn::Bytes bytes{s().shadow.data()+uint64_t(slot)*IMAGE_SLOT_SIZE+offset,size_t(input.width)*input.height*4};
 auto sha=hash(bytes);
 receipt.render_end_ns=now();
 if(!receipt.render_end_ns||receipt.render_end_ns<receipt.render_begin_ns)return fail(err(EIO,"render interval clock"),false);
  // The common display may retire an older source from THIS ImageSession.
  // Its retirement callback must acquire this mutex. Keep the pending recipe
  // and immutable shadow pinned, but do not hold the mutex across that trusted
  // callback. Snapshot timing metadata; restore the lock on success or throw.
  const auto snapshot=receipt;
  struct OutputWindow {
   Impl&self;std::unique_lock<std::mutex>&lock;
   OutputWindow(Impl&s,std::unique_lock<std::mutex>&l):self(s),lock(l){self.publishing=true;lock.unlock();}
   ~OutputWindow(){lock.lock();self.publishing=false;}
  };
  auto saved=timed(receipt.publish_ns,[&](){OutputWindow window(*this,session_lock);return output(metadata,bytes,snapshot);
 });
 if(!saved)return fail(*saved.error,false);
 auto p=*saved.value;
 auto valid=validate_published_result(p.output);
 if(!valid)return fail(*valid.error,false);
 const auto&l=p.output.lease;
  if(p.output.binding||!p.output.execution_complete||!same(l.producer,metadata.request)||l.app!=AppKind::IMAGE||l.hardware!=plan.policy.hardware.hardware_contract||l.model!=metadata.model||l.plan!=metadata.plan||l.scope!=s().scope||!l.lease_tokens.empty()||p.output.displayed)return fail(err(EPROTO,"durable output binding/scope/display differs"),false);
  bool exact=false;
 for(const auto&a:l.artifacts)exact|=a.sha==sha&&a.bytes==bytes.size&&a.layout=="RGBA8888"&&a.shape==vxnn::Shape{input.height,input.width,4};
 if(!exact)return fail(err(EBADMSG,"durable raw RGBA extent/SHA missing"),false);
  if(s().scope!=vxnn::EvidenceScope::REAL_BOARD&&(p.output.numeric!=vxnn::NumericClass::NOT_EVALUATED||p.output.gpu_raw_cycles))return fail(err(EPROTO,"host fixture cannot claim GPU/numerical results"),false);
  auto accepted=p.display.presented?s().adapter->present(bytes,p.display):s().adapter->presentation_failed(p.display,p.failed_display_restored);
 if(!accepted)return fail(*accepted.error,false);
  receipt.display=p.display;
 receipt.image_result=*accepted.value;
 p.output.gpu_raw_cycles=accepted.value->gpu_raw_cycles;
 r.output=std::move(p.output);
 receipt.published=true;
  auto released=timed(receipt.release_ns,[&](){return release();
 });
 if(!released)return fail(*released.error,false);
 r.outcome.retired=true;
 r.outcome.status=RequestStatus::COMPLETED;
 r.outcome.result=0;
 return Result<void>::success();
 }
};
ImageSession::ImageSession(std::unique_ptr<Impl>p):impl_(std::move(p)){}ImageSession::~ImageSession()=default;
ImageWorkRequest::ImageWorkRequest(std::unique_ptr<Impl>p):impl_(std::move(p)){}ImageWorkRequest::~ImageWorkRequest()=default;
Result<std::shared_ptr<ImageSession>>ImageSession::create(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<ProgramResidency>residency,Digest implementation,Digest reference,vxnn::Bytes table,vxnn::EvidenceScope scope,bool diagnostic){
 return create_bound(std::move(catalog),caps,std::move(residency),implementation,reference,table,scope,diagnostic,nullptr,{});
}
Result<std::shared_ptr<ImageSession>>ImageSession::create_native(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<ProgramResidency>residency,std::shared_ptr<ResidentLease>charge,Digest implementation,Digest reference,vxnn::Bytes table,bool diagnostic){
 using R=Result<std::shared_ptr<ImageSession>>;auto admitted=admit_native_startup(c,1);if(!admitted)return R::failure(*admitted.error);
 if(!residency||residency->borrowed_backend()!=c.compute_backend||!native_identity_equal(residency->identity(),c.identity))return R::failure(err(EXDEV,"same common IMAGE residency/backend required"));
 auto caps=c.compute_backend->capabilities();if(!caps)return R::failure(*caps.error);return create_bound(std::move(catalog),*caps.value,std::move(residency),implementation,reference,table,c.scope,diagnostic,&c,std::move(charge));
}
Result<std::shared_ptr<ImageSession>>ImageSession::create_bound(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<ProgramResidency>residency,Digest implementation,Digest reference,vxnn::Bytes table,vxnn::EvidenceScope scope,bool diagnostic,const StartupContext*native,std::shared_ptr<ResidentLease>charge){
 using R=Result<std::shared_ptr<ImageSession>>;
 try{
 if(!catalog||!residency||catalog->identity().hardware_contract!=residency->identity().hardware_contract||catalog->identity().bit!=residency->identity().bit||catalog->identity().xsa!=residency->identity().xsa||catalog->identity().compatible!=residency->identity().compatible||catalog->identity().device_generation!=residency->identity().device_generation||catalog->identity().provider_flags!=residency->identity().provider_flags)return R::failure(err(EXDEV,"shared residency identity differs"));
 if(!table.data||table.size!=65536||vxnn::digest_hex(hash(table))!="116e71ef50cc30196546d2f8e53440d60942c8b438872d102e4aed74785aebca")return R::failure(err(EBADMSG,"exact frozen65536 read table required"));
 auto adapter=native?ImageAdapter::create_native(*native,catalog,charge,implementation,reference,diagnostic):ImageAdapter::create(catalog,caps,implementation,reference,scope,diagnostic);
 if(!adapter)return R::failure(*adapter.error);
 auto p=std::make_unique<Impl>();
 p->adapter=*adapter.value;
 p->catalog=std::move(catalog);
 p->caps=caps;
 p->residency=std::move(residency);
 p->native_charge=std::move(charge);
 if(native)p->native_runtime=native->runtime;
 p->backend=p->residency->borrowed_backend();
 p->implementation=implementation;
 p->reference=reference;
 p->scope=scope;
 p->table.assign(table.data,table.data+table.size);
 p->shadow.resize(VX_IMAGE_RESERVED_BYTES);
 return R::success(std::shared_ptr<ImageSession>(new ImageSession(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure(err(ENOMEM,"bounded shadow allocation"));
 }
}
Result<std::shared_ptr<ImageWorkRequest>>ImageSession::prepare(const ImageRequest&r,uint64_t budget,ImageTraceSink trace,ImageOutputSink output){
 using R=Result<std::shared_ptr<ImageWorkRequest>>;
 auto&s=*impl_;
 std::shared_ptr<HostMemoryLease>input_charge;
 if(s.native_charge){auto rt=s.native_runtime.lock();if(!rt)return R::failure(err(ESHUTDOWN,"common native Runtime expired"));auto reserved=rt->reserve_host_memory(16ULL<<20);if(!reserved)return R::failure(*reserved.error);input_charge=*reserved.value;}
 std::lock_guard<std::mutex>lock(s.mutex);
 if(s.fault)return R::failure(*s.fault);
 if(s.pending)return R::failure(err(EBUSY,"one immutable active image recipe"));
 if(!budget||budget>s.caps.max_execution_budget_ns||budget>60000000000ULL||!trace||!output)return R::failure(err(EINVAL,"bounded execution deadline and sinks required"));
 try{
 auto p=std::make_unique<ImageWorkRequest::Impl>();
 // Keep an outer owner until session_lock has unwound. Rejected metadata can
 // destroy p while that lock is held; its last budget release must occur only
 // after the session lock is released (budget -> scheduler -> ready uses it).
 p->input_charge=input_charge;
 p->session=shared_from_this();
 p->input=r;
 p->budget=budget;
 p->trace=std::move(trace);
 p->output=std::move(output);
 auto plan=s.adapter->prepare(r);
 if(!plan)return R::failure(*plan.error);
 s.pending=true;
 p->plan=*plan.value;
 auto recipe=s.adapter->recipe();
 if(!recipe){s.fault=*recipe.error;
 return R::failure(*recipe.error);
 }p->recipe=*recipe.value;
 auto bytes=s.adapter->input_snapshot();
 if(!bytes){s.fault=*bytes.error;
 return R::failure(*bytes.error);
 }p->input.rgba=*bytes.value;
 p->metadata.request=r.request;
 p->metadata.app=AppKind::IMAGE;
 p->metadata.model=p->plan.model_id;
 p->metadata.plan=p->plan.plan_id;
 p->metadata.input_sha=hash({p->input.rgba->data(),p->input.rgba->size()});
 p->metadata.input_bytes=p->input.rgba->size();
 p->metadata.cpu_bytes=s.native_charge?(16ULL<<20):VX_IMAGE_RESERVED_BYTES+(8u<<20);
 p->metadata.resident_bytes=s.native_charge?s.native_charge->bytes():VX_IMAGE_RESERVED_BYTES;
 p->source=vxnn::content_digest("IMAGE_NATIVE_OWNER_V1:"+vxnn::digest_hex(p->plan.plan_id)+":"+std::to_string(r.cookie_namespace));
 return R::success(std::shared_ptr<ImageWorkRequest>(new ImageWorkRequest(std::move(p))));
 }catch(const std::bad_alloc&){if(s.pending&&!s.fault)s.fault=err(ENOMEM,"post-admission metadata allocation retained");
 return R::failure(err(ENOMEM,"image source allocation"));
 }
}
Result<void>ImageWorkRequest::discard_unsubmitted(){
 auto&p=*impl_;auto&s=p.s();std::lock_guard<std::mutex>lock(s.mutex);
 if(p.receipt.retired)return Result<void>::success();
 if(p.active||p.lease||p.root||p.shadow_initialized||p.receipt.materialized||p.receipt.completed_jobs||p.receipt.first_error||p.receipt.evidence_error)return Result<void>::failure(err(EBUSY,"unsubmitted rollback proof is unavailable; backing retained"));
 auto restored=s.adapter->cancel_pending(p.metadata.request,true);if(!restored)return restored;s.pending=false;p.receipt.retired=true;return Result<void>::success();
}
uint64_t ImageWorkRequest::size()const{return impl_->recipe.count+1;
 }Digest ImageWorkRequest::identity()const{return impl_->source;
 }
Result<WorkItem>ImageWorkRequest::get(uint64_t i)const{auto&s=*impl_;
 if(i>=size())return Result<WorkItem>::failure(err(ERANGE,"source tail"));
 try{WorkItem w;
 w.request=s.metadata.request;
 w.app=AppKind::IMAGE;
 w.node=i+1;
 w.work_index=i;
 if(i)w.dependencies={i-1};
 w.kind=i<s.recipe.count?WorkKind::GPU:WorkKind::TRANSFER;
 if(w.kind==WorkKind::GPU)w.operation=DeferredGpuRef{"image.controlled.rgba.v1",w.node,s.plan.policy.hardware.device_generation,(uint64_t(s.input.cookie_namespace)<<32)+i+1};
 else w.operation=CpuTaskRef{"image.transfer.present.v1",w.node};
 return Result<WorkItem>::success(std::move(w));
 }catch(const std::bad_alloc&){return Result<WorkItem>::failure(err(ENOMEM,"source metadata"));
 }}
Result<bool>ImageWorkRequest::ready(uint64_t i)const{auto&s=*impl_;
 std::lock_guard<std::mutex>session_lock(s.s().mutex);
 if(s.receipt.first_error)return Result<bool>::failure(*s.receipt.first_error);
 if(s.publishing)return Result<bool>::success(false);
 if(i>=size()||s.receipt.retired)return Result<bool>::failure(err(ESTALE,"retired source"));
 if(i==s.recipe.count)return Result<bool>::success(true);
 auto key=s.key(s.recipe.ops[i]);
 return s.lease?s.s().residency->ready_switch(*s.lease,key):s.s().residency->ready(s.metadata.request,key);
 }
WorkRequest ImageWorkRequest::request()const{auto w=impl_->metadata;
 w.source=shared_from_this();
 return w;
 }const vxnn::Plan&ImageWorkRequest::plan()const{return impl_->plan;
 }const ImageOwnerReceipt&ImageWorkRequest::receipt()const{return impl_->receipt;
 }
bool ImageWorkRequest::matches_input(const ImageRequest&r)const{
 const auto&i=impl_->input;
 return same(i.request,r.request)&&i.width==r.width&&i.height==r.height&&i.input_id==r.input_id&&i.crc==r.crc&&i.display_cookie==r.display_cookie&&i.cookie_namespace&&i.rectangle.x==r.rectangle.x&&i.rectangle.y==r.rectangle.y&&i.rectangle.w==r.rectangle.w&&i.rectangle.h==r.rectangle.h&&i.params.style==r.params.style&&i.params.preset==r.params.preset&&i.params.block==r.params.block;
}
Result<ImageOwnerReceipt>ImageWorkRequest::receipt_snapshot()const{
 std::lock_guard<std::mutex>lock(impl_->s().mutex);
 try{return Result<ImageOwnerReceipt>::success(impl_->receipt);}catch(const std::bad_alloc&){return Result<ImageOwnerReceipt>::failure(err(ENOMEM,"receipt snapshot allocation"));}
}
ExecutionHooks ImageWorkRequest::handlers(){auto self=shared_from_this();
 ExecutionHooks h;
 h.prepare_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&)->Result<GPUJobDesc>{auto&s=*self->impl_;
 std::lock_guard<std::mutex>session_lock(s.s().mutex);
 auto valid=s.check(w,b,true,false);
 if(!valid)return Result<GPUJobDesc>::failure(*s.fail(*valid.error,false).error);
 s.active=true;
 s.row={};
 s.row.index=w.work_index;
 s.row.op=s.recipe.ops[w.work_index];
 s.row.begin_ns=now();
 if(!s.receipt.render_begin_ns)s.receipt.render_begin_ns=s.row.begin_ns;
 s.row.scope=s.s().scope;
 try{auto materialized=s.materialize(s.row.op);
 if(!materialized)return Result<GPUJobDesc>::failure(*s.fail(*materialized.error).error);
 image_job wire{};
 vx_image_view refs[5];
 int rc=vx_image_lower(s.row.op,&s.recipe.jobs[w.work_index],&wire,refs);
 if(rc)return Result<GPUJobDesc>::failure(*s.fail(err(-rc,"checked argument lowering")).error);
 auto args=s.view(128ULL+VX_IMAGE_RESERVED_BYTES,64,1);
 auto writable=args;
 writable.access=2;
 auto copied=timed(s.row.args_ns,[&](){return b.copy(writable,vxnn::Bytes{reinterpret_cast<const uint8_t*>(&wire),64});
 });
 if(!copied)return Result<GPUJobDesc>::failure(*s.fail(*copied.error).error);
 GPUJobDesc job;
 job.program_handle=s.lease->program.handle;
 job.program_generation=s.lease->program.program_generation;
 job.device_generation=s.lease->program.device_generation;
 job.cookie=gpu_work_cookie(w);
 job.args=args;
 for(const auto&ref:refs)job.references.push_back(s.view(64+uint64_t(ref.slot)*IMAGE_SLOT_SIZE+ref.offset,ref.length,ref.access));
 auto time=now();
 if(!time||time>UINT64_MAX-s.budget)return Result<GPUJobDesc>::failure(*s.fail(err(EIO,"deadline clock")).error);
 job.deadline_ns=time+s.budget;
 return Result<GPUJobDesc>::success(std::move(job));
 }catch(const std::exception&e){return Result<GPUJobDesc>::failure(*s.fail({EIO,"IMAGE_OWNER",e.what()}).error);
 }
 };
 h.finish_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto&s=*self->impl_;
 std::lock_guard<std::mutex>session_lock(s.s().mutex);
 auto valid=s.check(w,b,true,true);
 if(!valid)return s.fail(*valid.error,false);
 try{
 if(!r.outcome.retired||!r.outcome.acknowledged||!r.outcome.completion||!r.linux_detail||!matching_submission(r,gpu_work_cookie(w))||r.linux_detail->program_handle!=s.lease->program.handle||r.linux_detail->program_generation!=s.lease->program.program_generation||r.linux_detail->device_generation!=s.plan.policy.hardware.device_generation)return s.fail(err(EPROTO,"matching original ACK/program snapshot required"));
 s.row.linux_detail=r.linux_detail;
 if(s.s().scope==vxnn::EvidenceScope::REAL_BOARD)s.row.gpu_raw_cycles=r.outcome.completion->raw_cycles;
 auto guarded=timed(s.row.guard_ns,[&](){return s.guard_check();
 });
 if(!guarded)return s.fail(*guarded.error);
 s.row.end_ns=now();
 auto saved=s.trace(s.metadata.request,s.row);
 if(!saved){s.receipt.evidence_error=*saved.error;
 return s.fail(*saved.error,false);
 }auto accepted=s.s().adapter->accept(r.outcome);
 if(!accepted)return s.fail(*accepted.error,false);
 s.receipt.completed_jobs=accepted.value->completed_gpu;
 s.receipt.gpu_raw_cycles=accepted.value->gpu_raw_cycles;
 s.previous=r.outcome;
 s.detail=r.linux_detail;
 s.active=false;
 return Result<void>::success();
 }catch(const std::exception&e){return s.fail({EIO,"IMAGE_OWNER",e.what()},false);
 }
 };
 h.non_gpu=[self](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*,ExecutionReceipt&r){auto&s=*self->impl_;
 std::unique_lock<std::mutex>session_lock(s.s().mutex);
 auto valid=s.check(w,b,false,false);
 if(!valid)return s.fail(*valid.error,false);
 try{return s.publish(r,session_lock);
 }catch(const std::exception&e){return s.fail({EIO,"IMAGE_OWNER",e.what()},false);
 }};
 h.retire_request=[self](RequestKey k,UnifiedBackend&b){auto&s=*self->impl_;
 std::lock_guard<std::mutex>session_lock(s.s().mutex);
 if(&b!=s.s().backend.get()||!same(k,s.metadata.request))return Result<void>::failure(err(ESTALE,"retire binding"));
 if(s.publishing)return Result<void>::failure(err(EBUSY,"current output callback active; backing retained"));
 if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);
 if(s.active)return Result<void>::failure(err(EBUSY,"active image retained"));
 if(!s.receipt.retired&&!s.receipt.published){auto cancelled=s.s().adapter->cancel_pending(k,!s.shadow_initialized);if(!cancelled)return s.fail(*cancelled.error,false);}
 return s.release();
 };
 return h;
}
Result<std::array<uint8_t,512>>encode_image_job_trace(const ImageJobTrace&t){
 if(t.op>9||!t.begin_ns||t.end_ns<t.begin_ns||(t.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&t.scope!=vxnn::EvidenceScope::REAL_BOARD)||(t.scope!=vxnn::EvidenceScope::REAL_BOARD&&t.gpu_raw_cycles))return Result<std::array<uint8_t,512>>::failure(err(EINVAL,"typed trace scope/clock differs"));
 static_assert(sizeof(vxl_v2_completion)==256,"raw completion size");
 std::array<uint8_t,512>out{};
 auto put=[&](size_t at,uint64_t n){for(unsigned i=0;i<8;i++)out[at+i]=uint8_t(n>>(i*8));
 };
 put(0,0x00000001494a5452ULL);
 put(8,512);
 put(16,t.index);
 put(24,t.op);
 put(32,uint32_t(t.error));
 put(40,t.begin_ns);
 put(48,t.end_ns);
 put(56,t.program_ns);
 put(64,t.allocate_ns);
 put(72,t.upload_ns);
 put(80,t.args_ns);
 put(88,t.guard_ns);
 put(96,uint64_t(t.scope));
 put(104,t.gpu_raw_cycles.has_value());
 put(112,t.gpu_raw_cycles.value_or(0));
 if(t.linux_detail)std::memcpy(out.data()+256,&*t.linux_detail,256);
 return Result<std::array<uint8_t,512>>::success(out);
}
}

#include <vxunified/mosaic_request.hpp>
#include <vxunified/mosaic_core_probe.hpp>
#include <cstdlib>
#include <vxnn/mosaic_lowering.hpp>
#include <vxnn/mosaic_coverage.hpp>
#include <vxl/hunet.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <time.h>
namespace vxunified {
namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
bool same(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.provider_flags==b.provider_flags&&a.device_generation==b.device_generation;}
template<class F>auto timed(uint64_t&total,F fn)->decltype(fn()){
 using R=decltype(fn());auto begin=now();if(!begin)return R::failure({EIO,"MOSAIC_CLOCK","monotonic clock"});auto close=[&](){auto end=now();if(end<begin||!end||total>UINT64_MAX-(end-begin))return false;total+=end-begin;return true;};
 try{auto result=fn();if(!close())return R::failure({EIO,"MOSAIC_CLOCK","clock order/overflow"});return result;}catch(...){close();throw;}
}
}
struct MosaicRequest::Impl {
 vxnn::ModelPackage package;std::shared_ptr<const vxnn::RequestInput>request_input;vxnn::Plan plan;vxnn::MosaicFamily family;vxnn::ArenaPlan arena;
 std::shared_ptr<ProgramResidency>residency;std::shared_ptr<UnifiedBackend>backend;std::optional<ResidencyLease>lease;
 std::shared_ptr<vxnn::ArenaStorage>storage;std::shared_ptr<vxnn::ArenaGuards>guards;std::optional<BufferRef>args;
 WorkRequest metadata;Digest source{};vxnn::EvidenceScope scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;uint32_t cookie_ns=0;uint64_t budget_ns=0;
 std::vector<uint64_t>prefix;std::vector<uint32_t>items,budgets;MosaicTraceSink trace;MosaicOutputSink output;std::string core_probe_dir;MosaicRequestReceipt receipt;MosaicJobTrace row;std::optional<std::pair<VxnnMosaicArgs,BufferRef>>profile_work;bool active=false;
 struct Position{size_t phase=0;uint64_t batch=0;bool first=false,last=false;};
 Impl(vxnn::ModelPackage p,vxnn::Plan g):package(std::move(p)),plan(std::move(g)){prefix.resize(plan.nodes.size()+1);items.resize(plan.nodes.size());budgets.resize(plan.nodes.size());receipt.phases.resize(plan.nodes.size());}
 Result<Position>position(uint64_t index)const{if(index>=prefix.back())return Result<Position>::failure({ERANGE,"MOSAIC_OWNER","source index"});size_t phase=size_t(std::upper_bound(prefix.begin(),prefix.end(),index)-prefix.begin()-1);return Result<Position>::success({phase,index-prefix[phase],index==prefix[phase],index+1==prefix[phase+1]});}
 Result<void>fail(vxnn::Error e,bool report=true){
  if(!receipt.first_error)receipt.first_error=e;
  if(guards&&guards->receipt().fault)receipt.guard_fault=guards->receipt().fault;
  if(row.node){row.error=e.code;row.end_ns=now();if(report)try{auto saved=trace(metadata.request,row);if(!saved&&!receipt.evidence_error)receipt.evidence_error=*saved.error;}catch(...){if(!receipt.evidence_error)receipt.evidence_error=vxnn::Error{EIO,"MOSAIC_TRACE","error evidence callback exception"};}}
  return Result<void>::failure(*receipt.first_error);
 }
 Result<Position>check(const WorkItem&w,UnifiedBackend&b,bool finishing){
  if(receipt.first_error)return Result<Position>::failure(*receipt.first_error);
  auto p=position(w.work_index);if(!p)return p;const auto*d=std::get_if<DeferredGpuRef>(&w.operation);
  if(&b!=backend.get()||receipt.retired||active!=finishing||w.work_index!=receipt.completed_jobs||!same(w.request,metadata.request)||w.app!=AppKind::NN||w.kind!=WorkKind::GPU||w.node!=plan.nodes[p.value->phase].node.id||!d||d->registered_name!="mosaic.controlled.int16.v1"||d->node!=w.node||d->device_generation!=plan.policy.hardware.device_generation||d->cookie!=(uint64_t(cookie_ns)<<32)+w.work_index+1)return Result<Position>::failure({ESTALE,"MOSAIC_OWNER","request/batch/backend/epoch/cookie binding"});
  return p;
 }
 Result<void>materialize(ExecutionReceipt&r){
  if(receipt.materialized)return Result<void>::success();
  auto loaded=timed(row.program_ns,[&](){auto p=residency->acquire(metadata.request,plan.programs[0]);if(!p)return Result<void>::failure(*p.error);lease=*p.value;r.program_handle=lease->program.handle;r.program_generation=lease->program.program_generation;r.program_entry_id=lease->program.program_sha;return Result<void>::success();});if(!loaded)return loaded;
  auto allocated=timed(row.allocate_ns,[&](){auto s=vxnn::ArenaStorage::create(plan,arena,plan.policy.hardware.device_generation,*backend);if(!s)return Result<void>::failure(*s.error);storage=*s.value;auto a=storage->allocate();if(!a)return a;auto g=vxnn::ArenaGuards::create(plan,storage->layout(),storage->allocations(),plan.policy.hardware.device_generation,*backend);if(!g)return Result<void>::failure(*g.error);guards=*g.value;auto ref=backend->allocate(128,vxnn::AddressDomain::PL,3);if(!ref)return Result<void>::failure(*ref.error);args=*ref.value;return Result<void>::success();});if(!allocated)return allocated;
  auto uploaded=timed(row.upload_ns,[&](){auto seeded=guards->initialize();if(!seeded)return seeded;const auto&refs=*storage->bindings();if(!refs.readonly_placeholder)return Result<void>::failure({EPROTO,"MOSAIC_OWNER","missing readonly placeholder"});auto ro=*refs.readonly_placeholder;ro.access=2;std::array<uint8_t,64>zero{};auto initialized=backend->copy(ro,vxnn::Bytes{zero.data(),zero.size()});if(!initialized)return initialized;for(const auto&t:plan.tensors)if(t.desc.constant){const auto&a=package.assets().at(t.desc.asset);auto ref=refs.tensors.at(t.desc.id);ref.access=2;auto copied=backend->copy(ref,vxnn::Bytes{a.content->data(),a.content->size()});if(!copied)return copied;}const auto&input=request_input?*request_input->content:*package.assets().at("input.f32").content;return backend->copy(refs.tensors.at(plan.inputs[0]),vxnn::Bytes{input.data(),input.size()});});if(!uploaded)return uploaded;receipt.materialized=true;return Result<void>::success();
 }
 Result<void>release_backing(){
  if(receipt.retired)return Result<void>::success();
  if(args){auto r=backend->release_buffer(*args);if(!r)return r;args.reset();}if(storage&&storage->receipt().phase==vxnn::ArenaStoragePhase::READY){auto r=storage->release();if(!r)return r;}if(lease){auto r=residency->release(*lease,true);if(!r)return r;lease.reset();}receipt.retired=true;return Result<void>::success();
 }
 Result<void>publish(ExecutionReceipt&r){
  auto final=timed(receipt.download_ns,[&](){return guards->verify_final();});if(!final)return fail(*final.error,false);
  const vxnn::TensorDesc*desc=nullptr;for(const auto&t:plan.tensors)if(t.desc.id==plan.outputs[0])desc=&t.desc;
  if(!desc||desc->dtype!=vxnn::DType::FP32||desc->bytes>14745600)return fail({EINVAL,"MOSAIC_OUTPUT","bounded final FP32 output"},false);
  std::vector<uint8_t>bytes(size_t(desc->bytes));auto copied=timed(receipt.download_ns,[&](){return backend->copy(storage->bindings()->tensors.at(desc->id),vxnn::MutableBytes{bytes.data(),bytes.size()});});if(!copied)return fail(*copied.error,false);
  for(size_t i=0;i<bytes.size();i+=4){float value;std::memcpy(&value,bytes.data()+i,4);if(!std::isfinite(value))return fail({EDOM,"MOSAIC_OUTPUT","nonfinite final output"},false);}
  Digest sum{};vxl_hunet_hash(bytes.data(),bytes.size(),sum.data());auto saved=timed(receipt.publish_ns,[&](){return output(metadata,vxnn::Bytes{bytes.data(),bytes.size()},receipt);});if(!saved)return fail(*saved.error,false);auto result=*saved.value;
  auto valid=validate_published_result(result);if(!valid)return fail(*valid.error,false);const auto&l=result.lease;
  if(result.binding||!result.execution_complete||result.displayed||!same(l.producer,metadata.request)||l.app!=AppKind::NN||l.hardware!=plan.policy.hardware.hardware_contract||l.model!=metadata.model||l.plan!=metadata.plan||l.scope!=scope||!l.lease_tokens.empty())return fail({EPROTO,"MOSAIC_OUTPUT","durable result binding/scope/backing differs"},false);
  bool exact=false;for(const auto&a:l.artifacts)exact|=a.sha==sum&&a.bytes==bytes.size()&&a.layout==desc->layout&&a.shape==desc->shape;if(!exact)return fail({EBADMSG,"MOSAIC_OUTPUT","exact durable raw output digest/extent missing"},false);
  if(scope!=vxnn::EvidenceScope::REAL_BOARD&&(result.gpu_raw_cycles||result.numeric!=vxnn::NumericClass::NOT_EVALUATED))return fail({EPROTO,"MOSAIC_OUTPUT","host protocol fixture cannot claim numeric/GPU result"},false);
  if(scope==vxnn::EvidenceScope::REAL_BOARD){uint64_t total=0;for(const auto&phase:receipt.phases){if(!phase.gpu_raw_cycles||total>UINT64_MAX-*phase.gpu_raw_cycles)return fail({ENODATA,"MOSAIC_OUTPUT","missing/overflow raw GPU counters"},false);total+=*phase.gpu_raw_cycles;}result.gpu_raw_cycles=total;}
  r.output=std::move(result);receipt.published=true;auto retired=timed(receipt.release_ns,[&](){return release_backing();});if(!retired)return fail(*retired.error,false);return Result<void>::success();
 }
};
MosaicRequest::MosaicRequest(std::unique_ptr<Impl>p):impl_(std::move(p)){}MosaicRequest::~MosaicRequest()=default;
Result<std::shared_ptr<MosaicRequest>>MosaicRequest::prepare(vxnn::ModelPackage package,const vxnn::MosaicFamily&family,const DeviceCaps&caps,const BackendIdentity&hw,RequestKey key,std::shared_ptr<ProgramResidency>residency,vxnn::EvidenceScope scope,uint32_t cookie_ns,uint64_t budget,MosaicTraceSink trace,MosaicOutputSink output,std::shared_ptr<const vxnn::RequestInput> request_input){
 using R=Result<std::shared_ptr<MosaicRequest>>;if(!residency||!cookie_ns||!budget||budget>caps.max_execution_budget_ns||budget>60000000000ULL||!trace||!output||key.boot_id.empty()||key.client_id.empty()||key.request_id.empty()||(scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD))return R::failure({EINVAL,"MOSAIC_OWNER","scope/key/cookie/deadline/sinks"});if(!same(hw,residency->identity()))return R::failure({EXDEV,"MOSAIC_OWNER","residency identity"});
 try{
  vxnn::OperatorRegistry registry;auto registered=vxnn::register_mosaic_operators(registry,family);if(!registered)return R::failure(*registered.error);auto base_plan=vxnn::prepare_mosaic(package,registry,caps,hw);if(!base_plan)return R::failure(*base_plan.error);auto plan=request_input?vxnn::bind_request_input(*base_plan.value,*request_input):vxnn::Result<vxnn::Plan>::success(*base_plan.value);if(!plan)return R::failure(*plan.error);auto ready=residency->ready(key,plan.value->programs[0]);if(!ready)return R::failure(*ready.error);
  vxnn::MemoryBudget memory;memory.ps_bytes=caps.ps_bytes;memory.pl_bytes=caps.pl_bytes;memory.pl_reserved=8u<<20;auto arena=vxnn::plan_arenas(*plan.value,memory);if(!arena)return R::failure(*arena.error);
  auto p=std::make_unique<Impl>(std::move(package),*plan.value);p->request_input=std::move(request_input);p->family=family;p->arena=*arena.value;p->residency=std::move(residency);p->backend=p->residency->borrowed_backend();p->scope=scope;p->cookie_ns=cookie_ns;p->budget_ns=budget;p->trace=std::move(trace);p->output=std::move(output);
  const vxnn::TensorDesc*input=nullptr;for(const auto&t:p->plan.tensors)if(t.desc.id==p->plan.inputs[0])input=&t.desc;if(!input||input->shape.size()!=3)return R::failure({EINVAL,"MOSAIC_OWNER","input geometry"});
  if(p->plan.nodes.size()!=47&&p->plan.nodes.size()!=77)return R::failure({EINVAL,"MOSAIC_OWNER","fixed original or fused physical graph required"});
  std::map<vxnn::TensorId,const vxnn::TensorDesc*>descriptors;for(const auto&t:p->plan.tensors)descriptors.emplace(t.desc.id,&t.desc);
  for(size_t i=0;i<p->plan.nodes.size();i++){const auto&node=p->plan.nodes[i];uint32_t opcode=0;for(uint32_t op=1;op<=8;op++){auto key=vxnn::mosaic_operator_key(op,family);if(!(key<node.node.key)&&!(node.node.key<key))opcode=op;}std::vector<vxnn::TensorDesc>in;for(auto id:node.node.inputs)in.push_back(*descriptors.at(id));auto invocation=vxnn::inspect_mosaic_operator(opcode,in,node.node.parameters,caps);if(!invocation||!node.job_count||p->prefix[i]>0xfffffffeULL-node.job_count)return R::failure({EOVERFLOW,"MOSAIC_OWNER","bounded lazy prefix sum"});p->prefix[i+1]=p->prefix[i]+node.job_count;p->items[i]=invocation.value->shape.items;p->budgets[i]=uint32_t(std::get<uint64_t>(node.node.parameters.at("budget")));}
  if(p->prefix.back()!=p->plan.total_jobs)return R::failure({EINVAL,"MOSAIC_OWNER","job count"});
  p->metadata.request=std::move(key);p->metadata.app=AppKind::NN;p->metadata.model=p->plan.model_id;p->metadata.plan=p->plan.plan_id;const auto&asset=p->package.assets().at("input.f32");p->metadata.input_sha=p->request_input?p->request_input->tensor_sha:asset.sha;p->metadata.input_bytes=p->request_input?p->request_input->content->size():asset.content->size();p->metadata.resident_bytes=p->package.resident_bytes();p->metadata.cpu_bytes=64u<<20;
  if(const char*probe=std::getenv("S8_MOSAIC_CORE_PROBE_DIR")){if(*probe)p->core_probe_dir=probe;}
  p->source=vxnn::content_digest("VX_MOSAIC_REQUEST_V1:"+vxnn::digest_hex(p->plan.plan_id)+":"+vxnn::digest_hex(family.implementation_sha)+":"+std::to_string(cookie_ns));return R::success(std::shared_ptr<MosaicRequest>(new MosaicRequest(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"MOSAIC_OWNER","metadata allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"MOSAIC_OWNER",e.what()});}
}
uint64_t MosaicRequest::size()const{return impl_->prefix.back();}Digest MosaicRequest::identity()const{return impl_->source;}
Result<WorkItem>MosaicRequest::get(uint64_t i)const{auto&s=*impl_;auto p=s.position(i);if(!p)return Result<WorkItem>::failure(*p.error);try{WorkItem w;w.request=s.metadata.request;w.app=AppKind::NN;w.node=s.plan.nodes[p.value->phase].node.id;w.kind=WorkKind::GPU;w.work_index=i;if(i)w.dependencies={i-1};w.operation=DeferredGpuRef{"mosaic.controlled.int16.v1",w.node,s.plan.policy.hardware.device_generation,(uint64_t(s.cookie_ns)<<32)+i+1};return Result<WorkItem>::success(std::move(w));}catch(const std::bad_alloc&){return Result<WorkItem>::failure({ENOMEM,"MOSAIC_OWNER","lazy work allocation"});}}
Result<bool>MosaicRequest::ready(uint64_t i)const{auto&s=*impl_;if(s.receipt.first_error)return Result<bool>::failure(*s.receipt.first_error);if(i>=size()||s.receipt.retired)return Result<bool>::failure({ESTALE,"MOSAIC_OWNER","retired source"});return s.residency->ready(s.metadata.request,s.plan.programs[0]);}
WorkRequest MosaicRequest::request()const{auto w=impl_->metadata;w.source=shared_from_this();return w;}const vxnn::Plan&MosaicRequest::plan()const{return impl_->plan;}const MosaicRequestReceipt&MosaicRequest::receipt()const{return impl_->receipt;}
ExecutionHooks MosaicRequest::handlers(){auto self=shared_from_this();ExecutionHooks h;
 h.prepare_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r)->Result<GPUJobDesc>{auto&s=*self->impl_;auto pos=s.check(w,b,false);if(!pos)return Result<GPUJobDesc>::failure(*s.fail(*pos.error,false).error);const auto p=*pos.value;s.row={};s.profile_work.reset();auto&row=s.row;row.index=w.work_index;row.node=w.node;row.first=uint32_t(p.batch*s.budgets[p.phase]);row.count=std::min(s.budgets[p.phase],s.items[p.phase]-row.first);row.scope=s.scope;row.begin_ns=now();s.active=true;
  try{
   auto loaded=s.materialize(r);if(!loaded)return Result<GPUJobDesc>::failure(*s.fail(*loaded.error).error);if(p.first){auto begun=timed(row.guard_ns,[&](){return s.guards->begin_node(w.node);});if(!begun)return Result<GPUJobDesc>::failure(*s.fail(*begun.error).error);}
   auto lower=vxnn::lower_mosaic_batch(s.plan,s.family,*s.storage,w.node,p.batch);if(!lower)return Result<GPUJobDesc>::failure(*s.fail(*lower.error).error);if(!s.core_probe_dir.empty()&&row.node==2&&row.first==0){VxnnMosaicShape shape{};if((lower.value->args.abi==4?vxnn_mosaic_validate_reuse_v4(&lower.value->args,false,&shape):lower.value->args.abi==3?vxnn_mosaic_validate_coop_v3(&lower.value->args,false,&shape):vxnn_mosaic_validate_fused(&lower.value->args,false,&shape)))return Result<GPUJobDesc>::failure(*s.fail({EINVAL,"MOSAIC_CORE_PROBE","trusted first convolution layout"}).error);auto probe=mosaic_core_probe_capture(s.core_probe_dir,w.work_index,false,lower.value->references[VXM_WORK],lower.value->args.kpad,shape.core_work_bytes,[&b](BufferRef ref,vxnn::MutableBytes data){return b.copy(ref,data);});if(!probe)return Result<GPUJobDesc>::failure(*s.fail(*probe.error).error);}
   if((lower.value->args.abi==3||lower.value->args.abi==4)&&lower.value->args.reserved==1){
    s.profile_work=std::make_pair(lower.value->args,lower.value->references[VXM_WORK]);
    VxnnMosaicShape shape{};MosaicWorkLayoutV1 layout{};
    if(mosaic_profile_layout(lower.value->args,false,&shape,&layout))return Result<GPUJobDesc>::failure(*s.fail({EPROTO,"MOSAIC_PROFILE","instrumented prejob layout"}).error);
    // Invalidate only profiling headers after the prior owned job has retired.
    // Thus a skipped core cannot inherit a previous matching metadata record.
    auto cleared=timed(row.profile_prepare_ns,[&](){std::array<uint8_t,64>zero{};for(unsigned core=0;core<4;++core){auto ref=lower.value->references[VXM_WORK];ref.offset+=64+uint64_t(core)*layout.core_stride+layout.profile_offset;ref.bytes=64;ref.access=2;auto wrote=b.copy(ref,vxnn::Bytes{zero.data(),zero.size()});if(!wrote)return wrote;}return Result<void>::success();});if(!cleared)return Result<GPUJobDesc>::failure(*s.fail(*cleared.error).error);
   }
   auto copied=timed(row.args_ns,[&](){return b.copy(*s.args,vxnn::Bytes{reinterpret_cast<const uint8_t*>(&lower.value->args),128});});if(!copied)return Result<GPUJobDesc>::failure(*s.fail(*copied.error).error);
   GPUJobDesc job;job.program_handle=s.lease->program.handle;job.program_generation=s.lease->program.program_generation;job.device_generation=s.lease->program.device_generation;job.cookie=gpu_work_cookie(w);job.args=*s.args;job.references.assign(lower.value->references.begin(),lower.value->references.end());auto start=now();if(!start||start>UINT64_MAX-s.budget_ns)return Result<GPUJobDesc>::failure(*s.fail({EIO,"MOSAIC_OWNER","deadline clock"}).error);job.deadline_ns=start+s.budget_ns;return Result<GPUJobDesc>::success(std::move(job));
  }catch(const std::bad_alloc&){return Result<GPUJobDesc>::failure(*s.fail({ENOMEM,"MOSAIC_OWNER","prepare allocation; backing retained"}).error);}catch(const std::exception&e){return Result<GPUJobDesc>::failure(*s.fail({EIO,"MOSAIC_OWNER",e.what()}).error);}
 };
 h.finish_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r)->Result<void>{auto&s=*self->impl_;auto pos=s.check(w,b,true);if(!pos)return s.fail(*pos.error,false);const auto p=*pos.value;auto&row=s.row;
  try{
   if(!r.outcome.retired||!r.outcome.acknowledged||r.outcome.result||!r.outcome.completion||r.outcome.completion->result||r.outcome.completion->cookie!=gpu_work_cookie(w)||!r.linux_detail||!matching_submission(r,gpu_work_cookie(w))||r.linux_detail->device_generation!=s.plan.policy.hardware.device_generation||r.linux_detail->program_handle!=s.lease->program.handle||r.linux_detail->program_generation!=s.lease->program.program_generation)return s.fail({EBUSY,"MOSAIC_OWNER","matching ACK and original snapshot required"});
   row.linux_detail=r.linux_detail;if(s.scope==vxnn::EvidenceScope::REAL_BOARD){if(!r.outcome.completion->cycles_valid||!r.outcome.completion->raw_cycles)return s.fail({ENODATA,"MOSAIC_OWNER","raw GPU counter missing"});row.gpu_raw_cycles=r.outcome.completion->raw_cycles;}
   auto checked=timed(row.guard_ns,[&](){return p.last?s.guards->verify_retired_node(w.node):s.guards->verify_retired_subjob(w.node);});if(!checked)return s.fail(*checked.error);if(!s.core_probe_dir.empty()&&row.node==2&&row.first==0){auto lower=vxnn::lower_mosaic_batch(s.plan,s.family,*s.storage,w.node,p.batch);if(!lower)return s.fail(*lower.error);VxnnMosaicShape shape{};if((lower.value->args.abi==4?vxnn_mosaic_validate_reuse_v4(&lower.value->args,false,&shape):lower.value->args.abi==3?vxnn_mosaic_validate_coop_v3(&lower.value->args,false,&shape):vxnn_mosaic_validate_fused(&lower.value->args,false,&shape)))return s.fail({EINVAL,"MOSAIC_CORE_PROBE","retired first convolution layout"});auto probe=mosaic_core_probe_capture(s.core_probe_dir,w.work_index,true,lower.value->references[VXM_WORK],lower.value->args.kpad,shape.core_work_bytes,[&b](BufferRef ref,vxnn::MutableBytes data){return b.copy(ref,data);});if(!probe)return s.fail(*probe.error);}
   if(s.profile_work){auto start=now();auto read=mosaic_profile_read(s.profile_work->first,s.profile_work->second,[&b](BufferRef ref,vxnn::MutableBytes bytes){return b.copy(ref,bytes);});row.profile_read_ns=now()-start;if(!read)return s.fail(*read.error);row.stage_args=s.profile_work->first;row.stage_records=*read.value;}
   row.phase_complete=p.last;row.end_ns=now();if(!row.begin_ns||row.end_ns<row.begin_ns)return s.fail({EIO,"MOSAIC_OWNER","job clock order"});
   // Persist every raw job BEFORE output publication or backing retirement.
   auto persisted=s.trace(s.metadata.request,row);if(!persisted){s.receipt.evidence_error=*persisted.error;return s.fail(*persisted.error,false);}
   auto&phase=s.receipt.phases[p.phase];phase.node=w.node;if(!phase.completed_jobs)phase.begin_ns=row.begin_ns;phase.end_ns=row.end_ns;++phase.completed_jobs;
   if(row.gpu_raw_cycles){uint64_t previous=phase.gpu_raw_cycles.value_or(0);if(previous>UINT64_MAX-*row.gpu_raw_cycles)return s.fail({EOVERFLOW,"MOSAIC_OWNER","phase GPU counter sum"},false);phase.gpu_raw_cycles=previous+*row.gpu_raw_cycles;}
   ++s.receipt.completed_jobs;if(p.last)++s.receipt.completed_phases;s.active=false;s.profile_work.reset();if(w.work_index+1==s.prefix.back())return s.publish(r);return Result<void>::success();
  }catch(const std::bad_alloc&){return s.fail({ENOMEM,"MOSAIC_OWNER","postACK allocation; backing retained"},false);}catch(const std::exception&e){return s.fail({EIO,"MOSAIC_OWNER",e.what()},false);}
 };
 h.retire_request=[self](RequestKey key,UnifiedBackend&b){auto&s=*self->impl_;if(&b!=s.backend.get()||!same(key,s.metadata.request))return Result<void>::failure({ESTALE,"MOSAIC_OWNER","retire binding"});if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);if(s.active)return Result<void>::failure({EBUSY,"MOSAIC_OWNER","active GPU retained"});try{auto result=s.release_backing();if(!result)return s.fail(*result.error,false);return result;}catch(...){return s.fail({EIO,"MOSAIC_OWNER","retirement exception"},false);}};
 return h;
}
Result<std::array<uint8_t,512>>encode_mosaic_job_trace(const MosaicJobTrace&t){
 static_assert(sizeof(vxl_v2_completion)==256,"original completion wire size");if(!t.node||!t.count||t.count>4096||!t.begin_ns||t.end_ns<t.begin_ns||(t.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&t.scope!=vxnn::EvidenceScope::REAL_BOARD)||(t.scope!=vxnn::EvidenceScope::REAL_BOARD&&t.gpu_raw_cycles))return Result<std::array<uint8_t,512>>::failure({EINVAL,"MOSAIC_TRACE","bounded typed raw evidence"});
 std::array<uint8_t,512>out{};auto u32=[&](size_t n,uint32_t x){for(unsigned i=0;i<4;i++)out[n+i]=uint8_t(x>>(8*i));};auto u64=[&](size_t n,uint64_t x){for(unsigned i=0;i<8;i++)out[n+i]=uint8_t(x>>(8*i));};u32(0,0x4d4a5452);u32(4,1);u32(8,512);u32(12,uint32_t(t.scope));u64(16,t.index);u32(24,t.node);u32(28,t.first);u32(32,t.count);u32(36,uint32_t(t.error));u64(40,t.begin_ns);u64(48,t.end_ns);u64(56,t.program_ns);u64(64,t.allocate_ns);u64(72,t.upload_ns);u64(80,t.args_ns);u64(88,t.guard_ns);u32(96,uint32_t(t.phase_complete)|(t.gpu_raw_cycles?2u:0u)|(t.linux_detail?4u:0u));u64(104,t.gpu_raw_cycles.value_or(0));
 if(t.linux_detail){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Completion snapshot requires the supported little endian host/target
#endif
  std::memcpy(out.data()+256,&*t.linux_detail,256);
 }return Result<std::array<uint8_t,512>>::success(out);
}
Result<std::vector<uint8_t>>encode_mosaic_extended_trace(const MosaicJobTrace&t){
 using R=Result<std::vector<uint8_t>>;auto legacy=encode_mosaic_job_trace(t);if(!legacy)return R::failure(*legacy.error);
 if(bool(t.stage_args)!=bool(t.stage_records))return R::failure({EINVAL,"MOSAIC_TRACE","complete stage args/records pair required"});
 std::vector<uint8_t>out(legacy.value->begin(),legacy.value->end());
 if(t.stage_records){
  if(!t.linux_detail||t.error||(t.stage_args->abi!=3&&t.stage_args->abi!=4)||t.stage_args->reserved!=1||t.stage_args->first!=t.first||t.stage_args->count!=t.count)return R::failure({EINVAL,"MOSAIC_TRACE","stages require successful matching Linux completion and argument range"});
  out.resize(1536);auto u32=[&](size_t n,uint32_t x){for(unsigned i=0;i<4;i++)out[n+i]=uint8_t(x>>(8*i));};auto u64=[&](size_t n,uint64_t x){for(unsigned i=0;i<8;i++)out[n+i]=uint8_t(x>>(8*i));};u32(4,2);u32(8,1536);u32(96,uint32_t(t.phase_complete)|(t.gpu_raw_cycles?2u:0u)|4u|8u);u64(112,t.profile_prepare_ns);u64(120,t.profile_read_ns);std::memcpy(out.data()+128,&*t.stage_args,128);std::memcpy(out.data()+512,t.stage_records->data(),1024);
 }
 return R::success(std::move(out));
}

}

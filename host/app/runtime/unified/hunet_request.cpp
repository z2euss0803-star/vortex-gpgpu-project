#include <vxunified/hunet_request.hpp>
#include <vxnn/hunet_lowering.hpp>
#include <vxl/hunet.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <time.h>
namespace vxunified {
namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
bool same(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags;}
bool input_metadata(const vxnn::RequestInput&a,const vxnn::RequestInput&b){return a.id==b.id&&a.source_format==b.source_format&&a.preprocessing==b.preprocessing&&a.source_sha==b.source_sha&&a.tensor_sha==b.tensor_sha&&a.preprocessing_id==b.preprocessing_id&&a.dtype==b.dtype&&a.layout==b.layout&&a.shape==b.shape;}
bool word_identity(const HunetWordIdentity&a,const HunetWordIdentity&b){return a.word_index==b.word_index&&a.input_id==b.input_id&&a.source_sha==b.source_sha&&a.input_sha==b.input_sha&&a.preprocessing_id==b.preprocessing_id&&a.plan==b.plan;}
template<class F>auto timed(uint64_t&duration,F fn)->decltype(fn()){
 using R=decltype(fn());uint64_t begin=now();if(!begin)return R::failure({EIO,"HUNET_OWNER","phase clock"});
 auto close=[&](){uint64_t end=now();if(end<begin||!end||duration>UINT64_MAX-(end-begin))return false;duration+=end-begin;return true;};
 try{auto result=fn();if(!close())return R::failure({EIO,"HUNET_OWNER","phase clock order/overflow"});return result;}catch(...){close();throw;}
}
}
struct HunetRequest::Impl {
 vxnn::ModelPackage package;std::shared_ptr<const vxnn::RequestInput>request_input;vxnn::Plan base_plan,plan;vxnn::HunetFamily family;vxnn::ArenaPlan arena;
 std::shared_ptr<ProgramResidency>residency;std::shared_ptr<UnifiedBackend>backend;
 std::optional<ResidencyLease>lease;std::shared_ptr<vxnn::ArenaStorage>root_storage,storage;std::shared_ptr<vxnn::ArenaGuards>guards;std::optional<BufferRef>args;
 WorkRequest metadata;Digest source{};vxnn::EvidenceScope scope;uint32_t cookie_namespace=0;uint64_t budget=0;HunetTraceSink trace;HunetOutputSink publish;HunetRequestReceipt receipt;bool active=false,gn_compare=false,page=false,word_loaded=false;
 std::vector<vxnn::RequestInput>words;HunetInputLoader loader;HunetWordSink word_sink;HunetPageOutputSink page_sink;std::vector<HunetWordCommit>commits;vxl_hunet_sha page_hash{};uint64_t gpu_completed=0;
 Impl(vxnn::ModelPackage p,vxnn::Plan g):package(std::move(p)),base_plan(g),plan(std::move(g)){}
 uint64_t size()const{return page?uint64_t(words.size())*base_plan.nodes.size():plan.nodes.size();}
 size_t local(uint64_t i)const{return page?size_t(i%base_plan.nodes.size()):size_t(i);}
 uint32_t word(uint64_t i)const{return page?uint32_t(i/base_plan.nodes.size()):0;}
 Result<void>fail(vxnn::Error e,HunetNodeTrace*row=nullptr,bool report=true){
  if(!receipt.first_error)receipt.first_error=e;
  if(guards&&guards->receipt().fault)receipt.guard_fault=guards->receipt().fault;
  if(row){row->error=e.code;row->end_ns=now();if(report){try{auto saved=trace(metadata.request,*row);if(!saved&&!receipt.evidence_error)receipt.evidence_error=*saved.error;}catch(...){if(!receipt.evidence_error)receipt.evidence_error=vxnn::Error{EIO,"HUNET_TRACE","error trace callback exception"};}}}
  return Result<void>::failure(*receipt.first_error);
 }
 Result<void>check(const WorkItem&w,UnifiedBackend&b,bool cpu,bool finishing){
  if(receipt.first_error)return Result<void>::failure(*receipt.first_error);
  if(&b!=backend.get()||receipt.retired||w.work_index!=receipt.completed||w.work_index>=size()||!same(w.request,metadata.request)||w.app!=AppKind::NN||active!=finishing)return Result<void>::failure({ESTALE,"HUNET_OWNER","request/index/backend/lifetime binding"});
  const size_t at=local(w.work_index);const auto&pn=base_plan.nodes[at];
  if(w.node!=pn.node.id||bool(!pn.job_count)!=cpu)return Result<void>::failure({EINVAL,"HUNET_OWNER","node/CPU/GPU dependency kind differs"});
  if(page&&((!word_loaded&&(at||receipt.words_completed!=word(w.work_index)||finishing))||(word_loaded&&row_word()!=word(w.work_index))))return Result<void>::failure({ESTALE,"HUNET_PAGE","word boundary or loaded word differs"});
  if(cpu){auto*t=std::get_if<CpuTaskRef>(&w.operation);const char*name=pn.node.key.name=="Hunet.GnStatsCpuOrdered.v1"?"hunet.gn_stats.ordered.v1":"hunet.gelu.erf4.v1";if(w.kind!=WorkKind::CPU||!t||t->node!=w.node||t->registered_name!=name)return Result<void>::failure({EOPNOTSUPP,"HUNET_OWNER","unregistered CPU work"});}
  else{auto*d=std::get_if<DeferredGpuRef>(&w.operation);if(w.kind!=WorkKind::GPU||!d||d->node!=w.node||d->registered_name!="hunet.controlled.fp16.v1"||d->device_generation!=base_plan.policy.hardware.device_generation||d->cookie!=(uint64_t(cookie_namespace)<<32)+w.work_index+1)return Result<void>::failure({ESTALE,"HUNET_OWNER","deferred program/global cookie binding"});}
  return Result<void>::success();
 }
 uint32_t row_word()const{return receipt.nodes[0].word_index;}
 Result<void>load_program(HunetNodeTrace&row,ExecutionReceipt&r){return timed(row.program_ns,[&](){auto p=residency->acquire(metadata.request,base_plan.programs[0]);if(!p)return Result<void>::failure(*p.error);lease=*p.value;r.program_handle=lease->program.handle;r.program_generation=lease->program.program_generation;r.program_entry_id=lease->program.program_sha;return Result<void>::success();});}
 Result<void>constants(){const auto&refs=*storage->bindings();for(const auto&t:plan.tensors){if(!t.desc.constant)continue;const auto&a=package.assets().at(t.desc.asset);auto ref=refs.tensors.at(t.desc.id);ref.access=2;auto uploaded=backend->copy(ref,vxnn::Bytes{a.content->data(),a.content->size()});if(!uploaded)return uploaded;++receipt.constant_uploads;receipt.constant_upload_bytes+=a.content->size();}return Result<void>::success();}
 Result<void>input_copy(){const auto&in=request_input?*request_input->content:*package.assets().at("input.f32").content;auto copied=backend->copy(storage->bindings()->tensors.at(plan.inputs[0]),vxnn::Bytes{in.data(),in.size()});if(copied){++receipt.input_uploads;receipt.input_upload_bytes+=in.size();}return copied;}
 Result<void>start_word(HunetNodeTrace&row,ExecutionReceipt&r){
  if(word_loaded)return Result<void>::success();
  const auto ordinal=word(row.global_work_index);
  if(ordinal>=words.size()||ordinal!=receipt.words_completed||commits.size()!=ordinal)return Result<void>::failure({ESTALE,"HUNET_PAGE","previous durable word boundary missing"});
  auto loaded=loader(ordinal);if(!loaded)return Result<void>::failure(*loaded.error);
  if(!*loaded.value||!input_metadata(words[ordinal],**loaded.value)||!(*loaded.value)->content)return Result<void>::failure({ESTALE,"HUNET_PAGE","loader returned different metadata or no bytes"});
  request_input=*loaded.value;
  if(!receipt.materialized){
   auto code=load_program(row,r);if(!code)return code;
   auto allocated=timed(row.allocate_ns,[&](){auto s=vxnn::ArenaStorage::create(base_plan,arena,base_plan.policy.hardware.device_generation,*backend);if(!s)return Result<void>::failure(*s.error);root_storage=*s.value;auto a=root_storage->allocate();if(!a)return a;++receipt.data_allocations;auto arg=backend->allocate(128,vxnn::AddressDomain::PL,3);if(!arg)return Result<void>::failure(*arg.error);args=*arg.value;++receipt.args_allocations;return Result<void>::success();});if(!allocated)return allocated;
  }
  auto view=vxnn::ArenaStorage::borrow_input(root_storage,*request_input);if(!view)return Result<void>::failure(*view.error);storage=*view.value;plan=storage->plan();
  if(plan.plan_id!=vxnn::request_input_plan_id(base_plan.plan_id,words[ordinal]))return Result<void>::failure({ESTALE,"HUNET_PAGE","actual binder Plan differs metadata identity"});
  auto g=vxnn::ArenaGuards::create(plan,storage->layout(),storage->allocations(),plan.policy.hardware.device_generation,*backend);if(!g)return Result<void>::failure(*g.error);guards=*g.value;
  auto seeded=timed(row.seed_upload_ns,[&](){auto init=guards->initialize();if(!init)return init;if(!receipt.materialized){auto c=constants();if(!c)return c;}return input_copy();});if(!seeded)return seeded;
  receipt.materialized=true;word_loaded=true;return Result<void>::success();
 }
 Result<void>materialize(HunetNodeTrace&row,ExecutionReceipt&r){
  if(page)return start_word(row,r);
  if(receipt.materialized)return Result<void>::success();
  auto loaded=load_program(row,r);if(!loaded)return loaded;
  auto allocated=timed(row.allocate_ns,[&](){auto s=vxnn::ArenaStorage::create(plan,arena,plan.policy.hardware.device_generation,*backend);if(!s)return Result<void>::failure(*s.error);storage=*s.value;auto a=storage->allocate();if(!a)return a;++receipt.data_allocations;auto g=vxnn::ArenaGuards::create(plan,storage->layout(),storage->allocations(),plan.policy.hardware.device_generation,*backend);if(!g)return Result<void>::failure(*g.error);guards=*g.value;auto x=backend->allocate(128,vxnn::AddressDomain::PL,3);if(!x)return Result<void>::failure(*x.error);args=*x.value;++receipt.args_allocations;return Result<void>::success();});if(!allocated)return allocated;
  auto seeded=timed(row.seed_upload_ns,[&](){auto init=guards->initialize();if(!init)return init;auto c=constants();if(!c)return c;return input_copy();});if(!seeded)return seeded;receipt.materialized=true;return Result<void>::success();
 }
 Result<void>retire_word_view(){if(storage&&storage->receipt().borrowed_input&&storage->receipt().phase==vxnn::ArenaStoragePhase::READY){auto done=storage->retire_input_view(true);if(!done)return done;}guards.reset();storage.reset();request_input.reset();word_loaded=false;return Result<void>::success();}
 Result<void>release_backing(){
  if(receipt.retired)return Result<void>::success();
  if(args){auto x=backend->release_buffer(*args);if(!x)return x;args.reset();}
  if(page){auto view=retire_word_view();if(!view)return view;if(root_storage&&root_storage->receipt().phase==vxnn::ArenaStoragePhase::READY){auto released=root_storage->release();if(!released)return released;}}
  else if(storage&&storage->receipt().phase==vxnn::ArenaStoragePhase::READY){auto x=storage->release();if(!x)return x;}
  if(lease){auto x=residency->release(*lease,true);if(!x)return x;lease.reset();}
  receipt.retired=true;return Result<void>::success();
 }
 Result<void>validate_output(PublishedResult&out,const Digest&sum,uint64_t bytes){
  auto&l=out.lease;if(out.binding)return Result<void>::failure({EPROTO,"HUNET_OWNER","publisher cannot forge Runtime execution binding"});auto valid=validate_published_result(out);if(!valid)return valid;
  if(!out.execution_complete||out.displayed||!same(l.producer,metadata.request)||l.app!=AppKind::NN||l.hardware!=base_plan.policy.hardware.hardware_contract||l.model!=metadata.model||l.plan!=metadata.plan||l.scope!=scope||l.artifacts.empty()||!l.lease_tokens.empty())return Result<void>::failure({EPROTO,"HUNET_OWNER","durable result identity/scope or device-backed lease differs"});
  bool bound=false;for(const auto&a:l.artifacts)bound|=a.sha==sum&&a.bytes==bytes&&a.layout==(page?"HUNET_PAGE_F32":"CHW_F32")&&a.shape==(page?vxnn::Shape{words.size(),8105,1}:vxnn::Shape{8105,1,1});if(!bound)return Result<void>::failure({EBADMSG,"HUNET_OWNER","published raw logits/bundle digest/extent differs"});
  if(scope!=vxnn::EvidenceScope::REAL_BOARD&&(out.gpu_raw_cycles||out.numeric!=vxnn::NumericClass::NOT_EVALUATED))return Result<void>::failure({EPROTO,"HUNET_OWNER","host control fixture cannot claim GPU/numeric result"});
  if(scope==vxnn::EvidenceScope::REAL_BOARD){if(!receipt.gpu_raw_cycles||gpu_completed!=base_plan.total_jobs*receipt.words_planned)return Result<void>::failure({ENODATA,"HUNET_OWNER","incomplete GPU cycle accounting"});out.gpu_raw_cycles=receipt.gpu_raw_cycles;}else out.gpu_raw_cycles.reset();return Result<void>::success();
 }
 Result<void>finish(const WorkItem&w,ExecutionReceipt&r){
  auto&row=receipt.nodes[local(w.work_index)];auto checked=timed(row.guard_ns,[&](){return guards->verify_retired_node(w.node);});if(!checked)return fail(*checked.error,&row);
  if(!row.cpu){++gpu_completed;if(scope==vxnn::EvidenceScope::REAL_BOARD){if(!row.gpu_raw_cycles)return fail({ENODATA,"HUNET_OWNER","missing node cycles"},&row);uint64_t before=receipt.gpu_raw_cycles.value_or(0);if(before>UINT64_MAX-*row.gpu_raw_cycles)return fail({EOVERFLOW,"HUNET_OWNER","page GPU cycle sum"},&row);receipt.gpu_raw_cycles=before+*row.gpu_raw_cycles;}}
  if(local(w.work_index)+1==plan.nodes.size()){
   auto final=timed(row.guard_ns,[&](){return guards->verify_final();});if(!final)return fail(*final.error,&row);
   std::vector<uint8_t>output(32420);const auto&ref=storage->bindings()->tensors.at(plan.outputs[0]);auto copied=timed(row.download_ns,[&](){return backend->copy(ref,vxnn::MutableBytes{output.data(),output.size()});});if(!copied)return fail(*copied.error,&row);
   for(size_t i=0;i<output.size();i+=4){float v;std::memcpy(&v,output.data()+i,4);if(!std::isfinite(v))return fail({EDOM,"HUNET_OWNER","nonfinite final output"},&row);}Digest sum{};vxl_hunet_hash(output.data(),output.size(),sum.data());
   if(page){
    const auto ordinal=word(w.work_index);const auto&input=words[ordinal];HunetWordIdentity id{ordinal,input.id,input.source_sha,input.tensor_sha,input.preprocessing_id,plan.plan_id};
    auto saved=timed(row.publish_ns,[&](){return word_sink(metadata,id,{output.data(),output.size()},receipt);});if(!saved)return fail(*saved.error,&row);const auto&commit=*saved.value;
    if(!same(commit.producer,metadata.request)||!word_identity(commit.word,id)||commit.logits_sha!=sum||commit.logits_bytes!=32420||commit.durable_record.empty()||commit.durable_record.size()>512||std::any_of(commit.durable_record.begin(),commit.durable_record.end(),[](unsigned char c){return c<32||c>126;}))return fail({EPROTO,"HUNET_PAGE","durable word receipt identity/SHA/extent/reference differs"},&row);
    commits.push_back(commit);vxl_hunet_sha_update(&page_hash,output.data(),output.size());++receipt.words_completed;
    if(receipt.words_completed==words.size()){
     auto hash=page_hash;vxl_hunet_sha_final(&hash,receipt.page_logits_sha.data());auto published=timed(row.publish_ns,[&](){return page_sink(metadata,commits,receipt);});if(!published)return fail(*published.error,&row);auto out=*published.value;auto valid=validate_output(out,receipt.page_logits_sha,uint64_t(words.size())*32420);if(!valid)return fail(*valid.error,&row);r.output=std::move(out);receipt.published=true;auto retired=timed(row.release_ns,[&](){return release_backing();});if(!retired)return fail(*retired.error,&row);
    }
   }else{
    auto published=timed(row.publish_ns,[&](){return publish(metadata,{output.data(),output.size()},receipt);});if(!published)return fail(*published.error,&row);auto out=*published.value;auto valid=validate_output(out,sum,output.size());if(!valid)return fail(*valid.error,&row);r.output=std::move(out);receipt.published=true;receipt.words_completed=1;auto retired=timed(row.release_ns,[&](){return release_backing();});if(!retired)return fail(*retired.error,&row);
   }
  }
  row.complete=true;row.end_ns=now();if(!row.begin_ns||row.end_ns<row.begin_ns)return fail({EIO,"HUNET_OWNER","node clock order"},&row);auto saved=trace(metadata.request,row);if(!saved){receipt.evidence_error=*saved.error;return fail(*saved.error,&row,false);}
  ++receipt.completed;active=false;
  if(page&&!receipt.retired&&local(w.work_index)+1==plan.nodes.size()){auto done=retire_word_view();if(!done)return fail(*done.error,&row);}
  return Result<void>::success();
 }
};
HunetRequest::HunetRequest(std::unique_ptr<Impl>p):impl_(std::move(p)){}HunetRequest::~HunetRequest()=default;
Result<std::shared_ptr<HunetRequest>>HunetRequest::prepare(vxnn::ModelPackage package,const vxnn::HunetFamily&family,const DeviceCaps&caps,const BackendIdentity&hw,RequestKey key,std::shared_ptr<ProgramResidency>residency,vxnn::EvidenceScope scope,uint32_t cookie_ns,uint64_t budget,HunetTraceSink trace,HunetOutputSink output,std::shared_ptr<const vxnn::RequestInput> request_input){
 using R=Result<std::shared_ptr<HunetRequest>>;
 if(!residency||!cookie_ns||!budget||budget>caps.max_execution_budget_ns||budget>60000000000ULL||!trace||!output||(scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD))return R::failure({EINVAL,"HUNET_OWNER","trusted scope/cookie/deadline/sinks required"});
 if(!same(hw,residency->identity()))return R::failure({EXDEV,"HUNET_OWNER","residency hardware binding differs"});
 try{
  vxnn::OperatorRegistry registry;auto registered=vxnn::register_hunet_operators(registry,family);if(!registered)return R::failure(*registered.error);
  auto base_plan=vxnn::prepare_hunet(package,registry,caps,hw);if(!base_plan)return R::failure(*base_plan.error);
  auto plan=request_input?vxnn::bind_request_input(*base_plan.value,*request_input):vxnn::Result<vxnn::Plan>::success(*base_plan.value);if(!plan)return R::failure(*plan.error);
  auto gate=residency->ready(key,plan.value->programs[0]);if(!gate)return R::failure(*gate.error); // false is allowed: queue metadata only
  vxnn::MemoryBudget memory;memory.ps_bytes=caps.ps_bytes;memory.pl_bytes=caps.pl_bytes;memory.pl_reserved=8u<<20;auto arena=vxnn::plan_arenas(*plan.value,memory);if(!arena)return R::failure(*arena.error);
  auto p=std::make_unique<Impl>(std::move(package),*plan.value);p->base_plan=*base_plan.value;p->request_input=std::move(request_input);p->arena=*arena.value;p->family=family;p->residency=std::move(residency);p->backend=p->residency->borrowed_backend();p->scope=scope;p->cookie_namespace=cookie_ns;p->budget=budget;p->trace=std::move(trace);p->publish=std::move(output);if(const char*mode=std::getenv("S8_HUNET_GN9_COMPARE")){if(std::strcmp(mode,"1"))return R::failure({EINVAL,"HUNET_OWNER","GN9 compare accepts explicit trusted1 only"});p->gn_compare=true;}
  p->metadata.request=std::move(key);p->metadata.app=AppKind::NN;p->metadata.model=p->plan.model_id;p->metadata.plan=p->plan.plan_id;const auto&input=p->package.assets().at("input.f32");p->metadata.input_sha=p->request_input?p->request_input->tensor_sha:input.sha;p->metadata.input_bytes=p->request_input?p->request_input->content->size():input.content->size();p->metadata.resident_bytes=p->package.resident_bytes();p->metadata.cpu_bytes=16u<<20;
  p->source=vxnn::content_digest("VX_HUNET_REQUEST_V1:"+vxnn::digest_hex(p->plan.plan_id)+":"+vxnn::digest_hex(family.implementation_sha)+":"+std::to_string(cookie_ns));
  return R::success(std::shared_ptr<HunetRequest>(new HunetRequest(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"HUNET_OWNER","metadata allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"HUNET_OWNER",e.what()});}
}
Result<std::shared_ptr<HunetRequest>>HunetRequest::prepare_page(vxnn::ModelPackage package,const vxnn::HunetFamily&family,const DeviceCaps&caps,const BackendIdentity&hw,RequestKey key,std::shared_ptr<ProgramResidency>residency,vxnn::EvidenceScope scope,uint32_t cookie_ns,uint64_t budget,std::vector<vxnn::RequestInput>words,HunetInputLoader loader,HunetTraceSink trace,HunetWordSink sink,HunetPageOutputSink final){
 using R=Result<std::shared_ptr<HunetRequest>>;
 auto input=hunet_page_input_id(words);if(!input)return R::failure(*input.error);
 if(!loader||!trace||!sink||!final)return R::failure({EINVAL,"HUNET_PAGE","trusted loader/trace/durable word/final sinks required"});
 try{
  auto prepared=prepare(std::move(package),family,caps,hw,std::move(key),std::move(residency),scope,cookie_ns,budget,std::move(trace),[](const WorkRequest&,vxnn::Bytes,const HunetRequestReceipt&){return Result<PublishedResult>::failure({EPROTO,"HUNET_PAGE","single-word publisher cannot run in a page"});});if(!prepared)return prepared;
  auto app=*prepared.value;auto&s=*app->impl_;auto page_plan=hunet_page_plan_id(s.base_plan.plan_id,words);if(!page_plan)return R::failure(*page_plan.error);
  s.page=true;s.words=std::move(words);s.loader=std::move(loader);s.word_sink=std::move(sink);s.page_sink=std::move(final);s.commits.reserve(s.words.size());s.receipt.words_planned=uint32_t(s.words.size());vxl_hunet_sha_init(&s.page_hash);
  s.metadata.plan=*page_plan.value;s.metadata.input_sha=*input.value;s.metadata.input_bytes=uint64_t(s.words.size())*196608;s.metadata.cpu_bytes=(16u<<20)+s.metadata.input_bytes;
  s.source=vxnn::content_digest("VX_HUNET_PAGE_SOURCE_V1:"+vxnn::digest_hex(s.metadata.model)+":"+vxnn::digest_hex(s.base_plan.plan_id)+":"+vxnn::digest_hex(s.metadata.plan)+":"+vxnn::digest_hex(s.metadata.input_sha)+":"+vxnn::digest_hex(family.implementation_sha)+":"+std::to_string(cookie_ns));
  return R::success(std::move(app));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"HUNET_PAGE","bounded page metadata allocation"});}
 catch(const std::exception&e){return R::failure({EINVAL,"HUNET_PAGE",e.what()});}
}
uint64_t HunetRequest::size()const{return impl_->size();}Digest HunetRequest::identity()const{return impl_->source;}
Result<WorkItem>HunetRequest::get(uint64_t i)const{
 auto&s=*impl_;if(i>=s.size())return Result<WorkItem>::failure({ERANGE,"HUNET_OWNER","source index"});
 try{const auto&n=s.base_plan.nodes[s.local(i)];WorkItem w;w.request=s.metadata.request;w.app=AppKind::NN;w.node=n.node.id;w.work_index=i;if(i)w.dependencies={i-1};if(n.job_count){w.kind=WorkKind::GPU;w.operation=DeferredGpuRef{"hunet.controlled.fp16.v1",w.node,s.base_plan.policy.hardware.device_generation,(uint64_t(s.cookie_namespace)<<32)+i+1};}else{w.kind=WorkKind::CPU;w.operation=CpuTaskRef{n.node.key.name=="Hunet.GnStatsCpuOrdered.v1"?"hunet.gn_stats.ordered.v1":"hunet.gelu.erf4.v1",w.node};}return Result<WorkItem>::success(std::move(w));}catch(const std::bad_alloc&){return Result<WorkItem>::failure({ENOMEM,"HUNET_OWNER","logical work allocation"});}
}
Result<bool>HunetRequest::ready(uint64_t i)const{auto&s=*impl_;if(s.receipt.first_error)return Result<bool>::failure(*s.receipt.first_error);if(i>=s.size()||s.receipt.retired)return Result<bool>::failure({ESTALE,"HUNET_OWNER","retired source"});return s.residency->ready(s.metadata.request,s.base_plan.programs[0]);}
WorkRequest HunetRequest::request()const{auto w=impl_->metadata;w.source=shared_from_this();return w;}
const vxnn::Plan&HunetRequest::plan()const{return impl_->plan;}
const vxnn::Plan&HunetRequest::base_plan()const{return impl_->base_plan;}
const HunetRequestReceipt&HunetRequest::receipt()const{return impl_->receipt;}
ExecutionHooks HunetRequest::hooks(){auto self=shared_from_this();ExecutionHooks h;
 h.prepare_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r)->Result<GPUJobDesc>{auto&s=*self->impl_;auto valid=s.check(w,b,false,false);if(!valid)return Result<GPUJobDesc>::failure(*valid.error);if(s.page&&!s.word_loaded)s.receipt.nodes={};auto&row=s.receipt.nodes[s.local(w.work_index)];row.node=w.node;row.word_index=s.word(w.work_index);row.global_work_index=w.work_index;row.begin_ns=now();s.active=true;
  try{if(s.page&&!s.word_loaded&&w.cancel_control&&w.cancel_control->cancelled.load()){auto x=s.fail({ECANCELED,"HUNET_PAGE","cancelled before next word loader"},&row);return Result<GPUJobDesc>::failure(*x.error);}auto init=s.materialize(row,r);if(!init){auto x=s.fail(*init.error,&row);return Result<GPUJobDesc>::failure(*x.error);}auto begun=timed(row.guard_ns,[&](){return s.guards->begin_node(w.node);});if(!begun){auto x=s.fail(*begun.error,&row);return Result<GPUJobDesc>::failure(*x.error);}auto lowered=vxnn::lower_hunet_node(s.plan,s.family,*s.storage,w.node);if(!lowered){auto x=s.fail(*lowered.error,&row);return Result<GPUJobDesc>::failure(*x.error);}auto copied=timed(row.args_ns,[&](){return b.copy(*s.args,vxnn::Bytes{reinterpret_cast<const uint8_t*>(&lowered.value->args),128});});if(!copied){auto x=s.fail(*copied.error,&row);return Result<GPUJobDesc>::failure(*x.error);}
   GPUJobDesc job;job.program_handle=s.lease->program.handle;job.program_generation=s.lease->program.program_generation;job.device_generation=s.lease->program.device_generation;job.cookie=gpu_work_cookie(w);job.args=*s.args;job.references.assign(lowered.value->references.begin(),lowered.value->references.end());auto clock=now();if(!clock||clock>UINT64_MAX-s.budget){auto x=s.fail({EIO,"HUNET_OWNER","deadline clock"},&row);return Result<GPUJobDesc>::failure(*x.error);}job.deadline_ns=clock+s.budget;return Result<GPUJobDesc>::success(std::move(job));
  }catch(const std::bad_alloc&){auto x=s.fail({ENOMEM,"HUNET_OWNER","prepare allocation; backing retained"},&row);return Result<GPUJobDesc>::failure(*x.error);}catch(const std::exception&e){auto x=s.fail({EIO,"HUNET_OWNER",e.what()},&row);return Result<GPUJobDesc>::failure(*x.error);}
 };
 h.finish_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto&s=*self->impl_;auto checked=s.check(w,b,false,true);if(!checked)return checked;auto&row=s.receipt.nodes[s.local(w.work_index)];try{if(!r.outcome.retired||!r.outcome.acknowledged||!r.outcome.completion||r.outcome.completion->cookie!=gpu_work_cookie(w)||!r.linux_detail||!matching_submission(r,gpu_work_cookie(w)))return s.fail({EBUSY,"HUNET_OWNER","no matching ACK retirement snapshot"},&row);row.linux_detail=r.linux_detail;if(s.scope==vxnn::EvidenceScope::REAL_BOARD){if(!r.outcome.completion->cycles_valid||!r.outcome.completion->raw_cycles)return s.fail({ENODATA,"HUNET_OWNER","GPU raw counter missing"},&row);row.gpu_raw_cycles=r.outcome.completion->raw_cycles;}if(s.gn_compare&&w.node==9){std::array<uint32_t,2>bits{};auto ref=s.storage->bindings()->tensors.at(s.plan.nodes[s.local(w.work_index)].node.outputs[0]);ref.access=1;auto copied=timed(row.download_ns,[&](){return b.copy(ref,vxnn::MutableBytes{reinterpret_cast<uint8_t*>(bits.data()),8});});if(!copied)return s.fail(*copied.error,&row);row.gn_gpu_bits=bits;}return s.finish(w,r);}catch(const std::bad_alloc&){return s.fail({ENOMEM,"HUNET_OWNER","postACK allocation"},&row);}catch(const std::exception&e){return s.fail({EIO,"HUNET_OWNER",e.what()},&row);}};
 h.non_gpu=[self](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*pool,ExecutionReceipt&r){auto&s=*self->impl_;auto checked=s.check(w,b,true,false);if(!checked)return checked;auto&row=s.receipt.nodes[s.local(w.work_index)];row.node=w.node;row.word_index=s.word(w.work_index);row.global_work_index=w.work_index;row.cpu=true;row.begin_ns=now();s.active=true;
  try{if(!s.receipt.materialized||!pool||pool->workers()!=4||!w.pending_token||!w.cancel_control)return s.fail({EINVAL,"HUNET_OWNER","shared CPU4 reservation and materialized predecessor required"},&row);auto begun=timed(row.guard_ns,[&](){return s.guards->begin_node(w.node);});if(!begun)return s.fail(*begun.error,&row);const auto&n=s.plan.nodes[s.local(w.work_index)].node;auto input=s.storage->bindings()->tensors.at(n.inputs[0]),output=s.storage->bindings()->tensors.at(n.outputs[0]);input.access=1;output.access=3;
   if(n.key.name=="Hunet.GnStatsCpuOrdered.v1"){const auto&shape=std::get<vxnn::Shape>(n.parameters.at("shape"));row.gn_stats.emplace();auto computed=vxnn::execute_hunet_stats({w.node,input,output,size_t(input.bytes/4),uint32_t(shape.at(11))},b,*pool,w.cancel_control,w.pending_token,*row.gn_stats);if(!computed)return s.fail(*computed.error,&row);if(!row.gn_stats->committed)return s.fail({EPROTO,"HUNET_OWNER","CPU stats has no complete COMMIT"},&row);}
   else{row.gelu.emplace();auto computed=vxnn::execute_hunet_gelu({w.node,input,output,size_t(input.bytes/4)},b,*pool,w.cancel_control,w.pending_token,*row.gelu);if(!computed)return s.fail(*computed.error,&row);if(!row.gelu->committed)return s.fail({EPROTO,"HUNET_OWNER","CPU result has no complete COMMIT"},&row);}
   auto done=s.finish(w,r);if(!done)return done;r.outcome.status=RequestStatus::COMPLETED;r.outcome.result=0;r.outcome.retired=true;return Result<void>::success();
  }catch(const std::bad_alloc&){return s.fail({ENOMEM,"HUNET_OWNER","CPU allocation; backing retained"},&row);}catch(const std::exception&e){return s.fail({EIO,"HUNET_OWNER",e.what()},&row);}
 };
 h.retire_request=[self](RequestKey k,UnifiedBackend&b){auto&s=*self->impl_;if(&b!=s.backend.get()||!same(k,s.metadata.request))return Result<void>::failure({ESTALE,"HUNET_OWNER","retire request binding"});if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);if(s.active)return Result<void>::failure({EBUSY,"HUNET_OWNER","active node retained"});try{auto retired=s.release_backing();if(!retired)return s.fail(*retired.error);return retired;}catch(...){return s.fail({EIO,"HUNET_OWNER","retirement exception"});}};
 return h;
}
}

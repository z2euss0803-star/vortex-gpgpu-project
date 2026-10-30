#include <vxunified/p8_request.hpp>
#include <vxunified/native_admission.hpp>
#include <vxunified/texture.hpp>
#include <vxl/hunet.h>
#include <cstring>
#include <mutex>
#include <tuple>
#include <time.h>
namespace vxunified {namespace {
vxnn::Error err(int c,const char*s){return {c,"P8_OWNER",s};}
bool same(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
bool valid(const RequestKey&r){for(const auto*s:{&r.boot_id,&r.client_id,&r.request_id}){if(s->empty()||s->size()>128||!std::all_of(s->begin(),s->end(),[](unsigned char c){return c>=33&&c<127;})){return false;}}return true;}
uint64_t ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t)){return 0;}return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
Digest hash(vxnn::Bytes b){Digest d{};vxl_hunet_hash(b.data,b.size,d.data());return d;}
uint32_t frame_crc(vxnn::Bytes bytes){uint32_t table[256];for(unsigned i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}uint32_t c=~0u;for(size_t i=0;i<bytes.size;i++)c=table[(c^bytes.data[i])&255]^(c>>8);return ~c;}
template<class F>auto timed(uint64_t&sum,F f)->decltype(f()){
 using R=decltype(f());auto begin=ns();if(!begin){return R::failure(err(EIO,"clock unavailable"));}auto r=f();auto end=ns();if(!end||end<begin||end-begin>UINT64_MAX-sum){return R::failure(err(EOVERFLOW,"clock order/overflow"));}sum+=end-begin;return r;
}
bool same_hw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
}
struct P8WorkRequest::Impl {
 mutable std::mutex mutex;
 std::shared_ptr<const P8Assets>assets;std::shared_ptr<ProgramResidency>residency;std::shared_ptr<UnifiedBackend>device;
 std::shared_ptr<ResidentLease>asset_charge;std::shared_ptr<HostMemoryLease>frame_charge;
 std::shared_ptr<P8FrameSession>session;std::optional<P8SessionLease>cached;uint64_t mode_sequence=0;bool guards_verified=false;
 P8Request input;P8Frame frame;vxnn::Plan plan;WorkRequest meta;Digest source{};vxnn::EvidenceScope scope;uint64_t budget=0,stage=0,total=0,args_offset=0;
 std::array<uint64_t,10>offset{};std::optional<BufferRef>data;std::optional<ResidencyLease>lease;std::optional<WorkOutcome>previous;std::optional<vxl_v2_completion>detail;
 P8TraceSink trace;P8OutputSink output;P8OwnerReceipt receipt;bool active=false,publishing=false;
 Result<void>fail(vxnn::Error e){if(!receipt.first_error){receipt.first_error=std::move(e);if(session&&cached)session->poison(cached->serial,*receipt.first_error);}return Result<void>::failure(*receipt.first_error);}
 BufferRef view(uint64_t at,uint64_t bytes,uint32_t access)const{auto v=*data;v.offset+=at;v.bytes=bytes;v.access=access;return v;}
 BufferRef slot(unsigned i,uint32_t access)const{return view(offset[i]+64,frame.buffers[i].size()*4,access);}
 Result<void>bind(const WorkItem&w,UnifiedBackend&b,bool finishing)const{
  if(publishing){return Result<void>::failure(err(EBUSY,"original frame output callback active; backing retained"));}
  if(receipt.first_error){return Result<void>::failure(*receipt.first_error);}
  auto kind=stage==0||stage==2?WorkKind::GPU:stage==1?WorkKind::CPU:WorkKind::TRANSFER;
  if(stage>=4||receipt.retired||&b!=device.get()||!same(w.request,meta.request)||w.app!=AppKind::P8||w.node!=stage+1||w.work_index!=stage||w.kind!=kind||active!=finishing){return Result<void>::failure(err(ESTALE,"exact frame request/node/backend/stage binding required"));}
  if(kind==WorkKind::GPU){auto*p=std::get_if<DeferredGpuRef>(&w.operation);if(!p||p->registered_name!="p8.controlled.frame.v1"||p->node!=w.node||p->device_generation!=plan.policy.hardware.device_generation||p->cookie!=(uint64_t(input.cookie_namespace)<<32)+stage+1){return Result<void>::failure(err(ESTALE,"deferred cookie/program epoch differs"));}}
  else{auto*p=std::get_if<CpuTaskRef>(&w.operation);if(!p||p->node!=w.node||p->registered_name!=(stage==1?"p8.cpu.bins.v1":"p8.transfer.present.v1")){return Result<void>::failure(err(ESTALE,"CPU/transfer operator binding differs"));}}
  return Result<void>::success();
 }
 Result<void>guards(){
  guards_verified=false;
  if(!data){return Result<void>::failure(err(EBADF,"missing DATA"));}std::array<uint8_t,64>got{};
  for(unsigned i=0;i<10;i++){for(unsigned side=0;side<2;side++){auto r=device->copy(view(offset[i]+(side?64+frame.buffers[i].size()*4:0),64,1),vxnn::MutableBytes{got.data(),64});if(!r){return r;}for(auto b:got){if(b!=uint8_t(0xd1^i)){return Result<void>::failure(err(EBADMSG,"frame sub-buffer head/tail guard"));}}}}
  guards_verified=true;return Result<void>::success();
 }
 Result<void>materialize(){
  if(data){return Result<void>::success();}
  auto allocated=timed(receipt.allocate_ns,[&](){
   if(session){P8SessionKey key;key.total_bytes=total;key.mode_sequence=mode_sequence;vxl_hunet_sha layout,statics;vxl_hunet_sha_init(&layout);vxl_hunet_sha_init(&statics);vxl_hunet_sha_update(&layout,&total,sizeof(total));vxl_hunet_sha_update(&layout,offset.data(),sizeof(offset));
    const auto assets_id=assets->identity();vxl_hunet_sha_update(&statics,assets_id.data(),32);const uint32_t geometry[]={uint32_t(input.scene.object),frame.geometry.vertices,frame.geometry.faces,frame.geometry.active_slots,frame.geometry.width,frame.geometry.texture_width};vxl_hunet_sha_update(&statics,geometry,sizeof(geometry));
    if(input.texture){auto id=input.texture->identity();auto source=input.texture->source_result_sha();auto tensor=input.texture->source_tensor_sha();auto scope=uint32_t(input.texture->source_scope());vxl_hunet_sha_update(&statics,id.data(),32);vxl_hunet_sha_update(&statics,source.data(),32);vxl_hunet_sha_update(&statics,tensor.data(),32);vxl_hunet_sha_update(&statics,&scope,sizeof(scope));}
    for(unsigned i=0;i<10;++i){const uint64_t bytes=frame.buffers[i].size()*4;vxl_hunet_sha_update(&layout,&bytes,sizeof(bytes));if(i==2||i==3||i==5||i==9)vxl_hunet_sha_update(&statics,frame.buffers[i].data(),bytes);}
    vxl_hunet_sha_final(&layout,key.layout.data());vxl_hunet_sha_final(&statics,key.statics.data());auto r=session->acquire(meta.request,key);if(!r)return Result<void>::failure(*r.error);cached=*r.value;data=cached->root;receipt.cache_fast=true;receipt.cache_root_reused=cached->root_reused;receipt.cache_static_reused=cached->static_reuse;
   }else{auto r=device->allocate(total,vxnn::AddressDomain::PL,3);if(!r)return Result<void>::failure(*r.error);data=*r.value;}return Result<void>::success();});if(!allocated){return allocated;}
  // Validate the retired bank before any incremental write can conceal a guard
  // fault. Strict mode never enters this session path and keeps its old upload.
  if(cached&&cached->static_reuse){auto checked=timed(receipt.guard_ns,[&](){return guards();});if(!checked)return checked;}
  auto uploaded=timed(receipt.upload_ns,[&](){
   if(cached){
    auto copy=[&](BufferRef ref,const void*bytes,uint64_t n){ref.bytes=n;ref.access=2;auto r=device->copy(ref,vxnn::Bytes{static_cast<const uint8_t*>(bytes),size_t(n)});if(r)receipt.upload_bytes+=n;return r;};
    if(!cached->static_reuse){
     // Seed every sub-buffer guard, including derived bins. No GPU operation
     // can consume their data until the CPU bins dependency has committed it.
     std::array<uint8_t,64>guard{};
     for(unsigned i=0;i<10;++i){guard.fill(uint8_t(0xd1^i));for(unsigned side=0;side<2;++side){auto r=copy(view(offset[i]+(side?64+frame.buffers[i].size()*4:0),64,2),guard.data(),64);if(!r)return r;}}
     for(unsigned i:{2u,3u,5u,9u}){auto r=copy(slot(i,2),frame.buffers[i].data(),frame.buffers[i].size()*4);if(!r)return r;}
    }else{
     // The exclusive retired bank was checked above. Keep its guards and
     // immutable data intact; rewriting valid canaries is unnecessary.
     for(unsigned i:{2u,3u,5u,9u})receipt.static_reused_bytes+=frame.buffers[i].size()*4;
    }
    const uint64_t active_words=uint64_t(frame.geometry.active_slots)*8;
    if(frame.buffers[0].size()!=frame.buffers[1].size()||active_words>frame.buffers[4].size()||active_words>frame.buffers[0].size())return Result<void>::failure(err(ERANGE,"active projection/poison range"));
    std::vector<uint32_t>poison(frame.buffers[0].size(),0xdeadbeef);
    for(unsigned i:{0u,1u}){auto r=copy(slot(i,2),poison.data(),poison.size()*4);if(!r)return r;}
    if(cached->static_reuse){auto r=copy(slot(4,2),poison.data(),active_words*4);if(!r)return r;}
    else{std::vector<uint32_t>projection(frame.buffers[4].size(),0);std::fill_n(projection.begin(),size_t(active_words),0xdeadbeef);auto r=copy(slot(4,2),projection.data(),projection.size()*4);if(!r)return r;}
    return Result<void>::success();
   }
   std::vector<uint8_t>shadow(total,0);const uint32_t poison=0xdeadbeef;
   for(unsigned i=0;i<10;i++){auto*p=shadow.data()+offset[i];const auto bytes=frame.buffers[i].size()*4;std::fill_n(p,64,uint8_t(0xd1^i));std::fill_n(p+64+bytes,64,uint8_t(0xd1^i));
    // Never seed a GPU result from the CPU oracle. Inactive projected padding
    // remains zero, exactly as in the frozen initial upload.
    if(i<2||i==4){const auto written=i==4?32ULL*frame.geometry.active_slots:bytes;for(uint64_t at=0;at<written;at+=4){std::memcpy(p+64+at,&poison,4);}}
    else if(!(cached&&cached->static_reuse&&(i==2||i==3||i==5||i==9))){std::memcpy(p+64,frame.buffers[i].data(),bytes);}
   }
   auto copy=[&](uint64_t first,uint64_t bytes){auto r=device->copy(view(first,bytes,2),vxnn::Bytes{shadow.data()+first,size_t(bytes)});if(r)receipt.upload_bytes+=bytes;return r;};
   if(cached&&cached->static_reuse){for(const auto&span:{std::pair<uint64_t,uint64_t>{0,offset[2]},std::pair<uint64_t,uint64_t>{offset[4],offset[5]-offset[4]},std::pair<uint64_t,uint64_t>{offset[6],offset[9]-offset[6]}}){auto r=copy(span.first,span.second);if(!r)return r;}for(unsigned i:{2u,3u,5u,9u})receipt.static_reused_bytes+=frame.buffers[i].size()*4;return Result<void>::success();}
   return copy(0,total);
  });if(uploaded){if(cached){auto committed=session->initialized(cached->serial);if(!committed)return committed;}receipt.materialized=true;}return uploaded;
 }
 Result<void>load(){
  return timed(receipt.program_ns,[&](){const auto&key=plan.programs[stage==2?1:0];if(!lease){auto r=residency->acquire(meta.request,key);if(!r){return Result<void>::failure(*r.error);}lease=*r.value;return Result<void>::success();}
   if(!previous||!detail){return Result<void>::failure(err(EBUSY,"switch without original previous ACK"));}
   auto r=residency->switch_after_ack(*lease,key,*previous,*detail);if(!r){return Result<void>::failure(*r.error);}lease=*r.value;return Result<void>::success();
  });
 }
 Result<void>release(){
  if(publishing){return Result<void>::failure(err(EBUSY,"original frame output callback active; backing retained"));}
  if(receipt.retired){return Result<void>::success();}if(active){return Result<void>::failure(err(EBUSY,"active frame retained"));}
  if(cached){
   if(lease){auto r=residency->release(*lease,true);if(!r)return r;lease.reset();}
   auto done=[&]()->Result<void>{
   if(receipt.published){P8SessionProof proof;proof.completed_gpu=receipt.completed_gpu;proof.acknowledged=previous&&previous->acknowledged&&previous->retired&&detail&&detail->state==3;proof.guards=guards_verified&&!receipt.first_error;proof.durable_output=receipt.output_crc_valid&&receipt.published;proof.presented_retired=receipt.display&&receipt.display->presented&&(!receipt.display->had_old_front||receipt.display->old_retired);proof.program_released=!lease;return session->retire(cached->serial,proof);}
   else{const uint64_t completed=stage==0?0:stage<3?1:2;return session->discard(cached->serial,!active&&receipt.completed_gpu==completed&&(completed==0||(previous&&previous->acknowledged&&previous->retired&&guards_verified)));}
   }();
   if(!done){return done;}cached.reset();data.reset();
  }else{if(data){auto r=device->release_buffer(*data);if(!r){return r;}data.reset();}if(lease){auto r=residency->release(*lease,true);if(!r){return r;}lease.reset();}}
  // The immutable output and KMS canvas own their independent backing. Once
  // original device/program retirement succeeds, no CPU frame array is read
  // again. Free capacities before returning their common budget reservation;
  // a retained display/result receipt must not pin a whole preparation arena.
  for(auto&buffer:frame.buffers)std::vector<uint32_t>().swap(buffer);
  frame.texture.reset();frame_charge.reset();
  receipt.retired=true;return Result<void>::success();
 }
 Result<GPUJobDesc>prepare(const WorkItem&w,UnifiedBackend&b){
  auto checked=bind(w,b,false);if(!checked){return Result<GPUJobDesc>::failure(*checked.error);}active=true;guards_verified=false;
  auto program=load();if(!program){return Result<GPUJobDesc>::failure(*fail(*program.error).error);}auto initialized=materialize();if(!initialized){return Result<GPUJobDesc>::failure(*fail(*initialized.error).error);}
  GPUJobDesc job;job.program_handle=lease->program.handle;job.program_generation=lease->program.program_generation;job.device_generation=lease->program.device_generation;job.cookie=gpu_work_cookie(w);job.args=view(args_offset,64,1);std::array<uint32_t,16>args{};
  if(stage==0){args[2]=frame.geometry.active_slots;std::copy(frame.matrix.begin(),frame.matrix.end(),args.begin()+3);job.references={slot(3,1),slot(4,2)};}
  else{args[6]=0xffece4dc;args[8]=frame.geometry.faces;for(unsigned i:{0u,1u,8u,6u,2u,7u,9u}){job.references.push_back(slot(i,i<2?2:1));}}
  auto copied=timed(receipt.args_ns,[&](){return device->copy(view(args_offset,64,2),vxnn::Bytes{reinterpret_cast<const uint8_t*>(args.data()),64});});if(!copied){return Result<GPUJobDesc>::failure(*fail(*copied.error).error);}receipt.args_upload_bytes+=64;auto now=ns();if(!now||now>UINT64_MAX-budget){return Result<GPUJobDesc>::failure(*fail(err(EOVERFLOW,"deadline clock")).error);}job.deadline_ns=now+budget;return Result<GPUJobDesc>::success(std::move(job));
 }
 Result<void>finish(const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){
  auto checked=bind(w,b,true);if(!checked){return fail(*checked.error);}
  if(!r.outcome.completion||!r.linux_detail||!r.outcome.acknowledged||!r.outcome.retired||r.outcome.status!=RequestStatus::COMPLETED||r.outcome.result){return fail(err(EPROTO,"original successful ACK required"));}
  const auto&c=*r.outcome.completion;const auto&d=*r.linux_detail;
  if(!same(r.outcome.request,meta.request)||r.outcome.node!=w.node||r.outcome.work_index!=stage||c.cookie!=gpu_work_cookie(w)||!c.sequence||c.status!=3||c.result||!matching_submission(r,gpu_work_cookie(w))||d.sequence!=c.sequence||d.state!=3||d.result||d.program_handle!=lease->program.handle||d.program_generation!=lease->program.program_generation||d.device_generation!=plan.policy.hardware.device_generation||(previous&&c.sequence<=previous->completion->sequence)){return fail(err(ESTALE,"original frame completion identity"));}
  if(scope==vxnn::EvidenceScope::REAL_BOARD){if(!d.cycles_valid||!c.cycles_valid||!c.raw_cycles||!*c.raw_cycles||*c.raw_cycles!=d.elapsed_raw||*c.raw_cycles>UINT64_MAX-receipt.gpu_raw_cycles.value_or(0)){return fail(err(ENODATA,"physical GPU raw cycles missing/overflow"));}receipt.gpu_raw_cycles=receipt.gpu_raw_cycles.value_or(0)+*c.raw_cycles;}
  else if(d.cycles_valid||c.cycles_valid||c.raw_cycles){return fail(err(EPROTO,"host cycles must stay null"));}
  auto guarded=timed(receipt.guard_ns,[&](){return guards();});if(!guarded){return fail(*guarded.error);}auto saved=trace(meta.request,r,receipt);if(!saved){receipt.evidence_error=*saved.error;return fail(*saved.error);}
  previous=r.outcome;detail=r.linux_detail;receipt.completed_gpu++;active=false;stage++;return Result<void>::success();
 }
 Result<void>bins(vxnn::CpuPool*pool,const WorkItem&w){
  const bool active_only=bool(cached);const size_t projected_words=active_only?size_t(frame.geometry.active_slots)*8:frame.buffers[4].size();if(!projected_words||projected_words>frame.buffers[4].size())return Result<void>::failure(err(ERANGE,"projected readback range"));std::vector<uint32_t>projected(projected_words);auto downloaded=timed(receipt.download_ns,[&](){auto ref=slot(4,1);ref.bytes=projected.size()*4;return device->copy(ref,vxnn::MutableBytes{reinterpret_cast<uint8_t*>(projected.data()),projected.size()*4});});if(!downloaded){return downloaded;}
  auto fn=[&](size_t begin,size_t end){if(begin==end){return Result<void>::success();}if(begin!=0||end!=1){return Result<void>::failure(err(EPROTO,"one canonical bin transaction"));}return assets->rebin(frame,{reinterpret_cast<const uint8_t*>(projected.data()),projected.size()*4},active_only);};
  auto prepared=timed(receipt.bins_ns,[&](){if(!pool){return Result<void>::failure(err(ENODEV,"shared CPU pool required"));}auto r=w.pending_token?pool->run_ranges_reserved(1,fn,w.cancel_control,w.pending_token):pool->run_ranges(1,fn,w.cancel_control);return r?Result<void>::success():Result<void>::failure(*r.error);});if(!prepared){return prepared;}
  return timed(receipt.upload_ns,[&](){for(unsigned i:{6u,7u,8u}){uint64_t bytes=frame.buffers[i].size()*4;if(active_only&&i==7)bytes=uint64_t(frame.active_refs)*4;if(active_only&&i==8)bytes=uint64_t(frame.geometry.faces)*64*4;if(bytes>frame.buffers[i].size()*4)return Result<void>::failure(err(ERANGE,"active bin upload range"));if(!bytes)continue;auto ref=slot(i,2);ref.bytes=bytes;auto r=device->copy(ref,vxnn::Bytes{reinterpret_cast<const uint8_t*>(frame.buffers[i].data()),size_t(bytes)});if(!r){return r;}receipt.upload_bytes+=bytes;}return Result<void>::success();});
 }
 Result<void>publish(ExecutionReceipt&r,std::unique_lock<std::mutex>&owner_lock){
  auto guarded=timed(receipt.guard_ns,[&](){return guards();});if(!guarded){return guarded;}std::array<std::vector<uint32_t>,2>actual;
  for(unsigned i=0;i<2;i++){actual[i].resize(frame.buffers[i].size());auto copied=timed(receipt.download_ns,[&](){return device->copy(slot(i,1),vxnn::MutableBytes{reinterpret_cast<uint8_t*>(actual[i].data()),actual[i].size()*4});});if(!copied){return copied;}if(input.numeric_reference&&actual[i]!=frame.buffers[i]){return Result<void>::failure(err(EBADMSG,"exact color/depth mismatch"));}}
  for(uint32_t pixel:actual[0]){if((pixel>>24)!=255){return Result<void>::failure(err(EBADMSG,"incomplete RGBA output"));}}
  for(uint32_t depth:actual[1]){if(depth!=UINT32_MAX&&depth>4096){return Result<void>::failure(err(EBADMSG,"invalid inverse-depth output"));}}
  vxnn::Bytes color{reinterpret_cast<const uint8_t*>(actual[0].data()),actual[0].size()*4},depth{reinterpret_cast<const uint8_t*>(actual[1].data()),actual[1].size()*4};Digest sha{};auto digested=timed(receipt.digest_ns,[&](){sha=hash(color);receipt.rgba_crc=frame_crc(color);receipt.depth_crc=frame_crc(depth);receipt.output_crc_valid=true;return Result<void>::success();});if(!digested){return digested;}
  // Common output/display callbacks may enter Runtime and retire an older
  // producer. Pin this frame, but never carry its owner lock into that graph.
  // The window reacquires before timed() updates receipt or an exception exits.
  const auto snapshot=receipt;
  struct OutputWindow {Impl&self;std::unique_lock<std::mutex>&lock;OutputWindow(Impl&s,std::unique_lock<std::mutex>&l):self(s),lock(l){self.publishing=true;lock.unlock();}~OutputWindow(){lock.lock();self.publishing=false;}};
  auto saved=timed(receipt.publish_ns,[&](){OutputWindow window(*this,owner_lock);return output(meta,color,depth,snapshot);});if(!saved){return Result<void>::failure(*saved.error);}if(receipt.first_error){return Result<void>::failure(*receipt.first_error);}if(stage!=3||active||receipt.retired){return Result<void>::failure(err(ESTALE,"frame changed during pinned output callback"));}auto p=*saved.value;auto checked=validate_published_result(p.output);if(!checked){return checked;}const auto&l=p.output.lease;const auto&d=p.display;
  if(p.output.binding||p.output.displayed||!p.output.execution_complete||!same(l.producer,meta.request)||l.app!=AppKind::P8||l.hardware!=plan.policy.hardware.hardware_contract||l.model!=meta.model||l.plan!=meta.plan||l.scope!=scope||!l.lease_tokens.empty()){return Result<void>::failure(err(EPROTO,"durable frame binding/scope differs"));}
  if(cached){const auto depth_sha=hash(depth);bool durable_depth=false;for(const auto&a:l.artifacts)durable_depth|=a.sha==depth_sha&&a.bytes==depth.size&&a.layout=="P8_DEPTH32"&&a.shape==vxnn::Shape{frame.geometry.width,frame.geometry.width};if(!durable_depth)return Result<void>::failure(err(EBADMSG,"cached frame requires durable exact depth artifact"));}
  bool exact=false;for(const auto&a:l.artifacts){exact|=a.sha==sha&&a.bytes==color.size&&a.layout=="RGBA8888"&&a.shape==vxnn::Shape{frame.geometry.width,frame.geometry.width,4};}if(!exact){return Result<void>::failure(err(EBADMSG,"durable RGBA extent/SHA missing"));}
  auto clock=scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::ClockDomain::LINUX_MONOTONIC:vxnn::ClockDomain::HOST_MONOTONIC;
  if(d.cookie!=input.display_cookie||d.result||!d.presented||d.clock_domain!=clock||!d.submitted_ns||!d.presented_ns||*d.presented_ns<*d.submitted_ns||(d.had_old_front&&(!d.old_retired||!d.old_retired_ns||*d.old_retired_ns<*d.presented_ns))){return Result<void>::failure(err(EPROTO,"presentation cookie/clock/old retirement differs"));}
  if(scope!=vxnn::EvidenceScope::REAL_BOARD&&(p.output.numeric!=vxnn::NumericClass::NOT_EVALUATED||p.output.gpu_raw_cycles)){return Result<void>::failure(err(EPROTO,"host cannot claim GPU numeric or performance"));}
  auto advanced=scene_after_frame(input.scene,frame.scene.pose,input.texture_redraw,*d.presented_ns);if(!advanced){return Result<void>::failure(*advanced.error);}receipt.next_scene=*advanced.value;receipt.display=d;receipt.published=true;p.output.gpu_raw_cycles=receipt.gpu_raw_cycles;r.output=std::move(p.output);
  // These download temporaries are covered by frame_charge too. They must
  // actually die before release() makes that reservation available elsewhere.
  for(auto&buffer:actual)std::vector<uint32_t>().swap(buffer);
  return timed(receipt.release_ns,[&](){return release();});
 }
};
P8WorkRequest::P8WorkRequest(std::unique_ptr<Impl>p):impl_(std::move(p)){}P8WorkRequest::~P8WorkRequest()=default;
Result<std::shared_ptr<P8WorkRequest>>P8WorkRequest::create(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<const P8Assets>assets,std::shared_ptr<ProgramResidency>residency,const P8Request&input,vxnn::EvidenceScope scope,uint64_t budget,P8TraceSink trace,P8OutputSink output){
 return create_bound(std::move(catalog),caps,std::move(assets),std::move(residency),input,scope,budget,std::move(trace),std::move(output),nullptr,{},{},{},0);
}
Result<std::shared_ptr<P8WorkRequest>>P8WorkRequest::create_native(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<const P8Assets>assets,std::shared_ptr<ProgramResidency>residency,const P8Request&input,uint64_t budget,P8TraceSink trace,P8OutputSink output,std::shared_ptr<ResidentLease>asset_charge,std::shared_ptr<HostMemoryLease>frame_charge,std::shared_ptr<P8FrameSession>session){
 using R=Result<std::shared_ptr<P8WorkRequest>>;auto admitted=admit_native_startup(c,2);if(!admitted)return R::failure(*admitted.error);auto rt=c.runtime.lock();
 if(!catalog||!assets||!residency||!native_identity_equal(catalog->identity(),c.identity)||!native_identity_equal(residency->identity(),c.identity)||residency->borrowed_backend()!=c.compute_backend||!rt->owns_resident(asset_charge)||asset_charge->app()!=AppKind::P8||asset_charge->model()!=assets->identity()||asset_charge->bytes()<(128ULL<<20)||!rt->owns_host_memory(frame_charge)||frame_charge->bytes()<(256ULL<<20))return R::failure(err(EXDEV,"common P8 assets/frame allocation must precede native preparation"));
 if(session&&(!c.fast_runtime||!c.compute_backend->fast_runtime()))return R::failure(err(EOPNOTSUPP,"explicit negotiated FAST session only"));
 auto caps=c.compute_backend->capabilities();if(!caps)return R::failure(*caps.error);return create_bound(std::move(catalog),*caps.value,std::move(assets),std::move(residency),input,c.scope,budget,std::move(trace),std::move(output),&c,std::move(asset_charge),std::move(frame_charge),std::move(session),rt->mode_snapshot().sequence);
}
Result<std::shared_ptr<P8WorkRequest>>P8WorkRequest::create_bound(std::shared_ptr<const InstalledCatalog>catalog,const DeviceCaps&caps,std::shared_ptr<const P8Assets>assets,std::shared_ptr<ProgramResidency>residency,const P8Request&input,vxnn::EvidenceScope scope,uint64_t budget,P8TraceSink trace,P8OutputSink output,const StartupContext*native,std::shared_ptr<ResidentLease>asset_charge,std::shared_ptr<HostMemoryLease>frame_charge,std::shared_ptr<P8FrameSession>session,uint64_t mode_sequence){
 using R=Result<std::shared_ptr<P8WorkRequest>>;
 if(!catalog||!assets||!residency||!same_hw(catalog->identity(),residency->identity())){return R::failure(err(EXDEV,"catalog/residency identity differs"));}
 if(!valid(input.request)||!input.cookie_namespace||input.cookie_namespace>0x7fffffffu||!input.display_cookie||!budget||budget>caps.max_execution_budget_ns||budget>60000000000ULL||!trace||!output){return R::failure(err(EINVAL,"bounded request/budget and trusted sinks required"));}
 if(input.texture_redraw&&!input.texture){return R::failure(err(ENODATA,"texture redraw requires admitted immutable bank"));}
 if(input.scene.startup_remaining&&!input.numeric_reference){return R::failure(err(EPERM,"initial reference gate required"));}
 try{
  auto adapter=native?P8Adapter::create_native(*native,catalog,assets,asset_charge):P8Adapter::create(catalog,caps,assets,scope);if(!adapter){return R::failure(*adapter.error);}auto plan=input.texture?(*adapter.value)->prepare_texture_frame(input.scene,input.texture,input.texture_redraw):(*adapter.value)->prepare_frame(input.scene);if(!plan){return R::failure(*plan.error);}auto cpu=assets->prepare_owned(input.scene,input.texture,input.numeric_reference,input.texture_redraw,bool(session)&&!input.numeric_reference);if(!cpu){return R::failure(*cpu.error);}if(!*cpu.value||cpu.value->use_count()!=1){return R::failure(err(ESTALE,"exclusive CPU frame handoff required"));}
  auto p=std::make_unique<Impl>();p->assets=std::move(assets);p->residency=std::move(residency);p->device=p->residency->borrowed_backend();p->input=input;p->frame=std::move(**cpu.value);p->plan=*plan.value;p->scope=scope;p->budget=budget;p->trace=std::move(trace);p->output=std::move(output);p->session=std::move(session);p->mode_sequence=mode_sequence;
  for(unsigned i=0;i<10;i++){p->offset[i]=p->total;p->total+=(p->frame.buffers[i].size()*4+191)&~63ULL;}p->args_offset=p->total;p->total+=64;
  p->meta.request=input.request;p->meta.app=AppKind::P8;p->meta.model=p->plan.model_id;p->meta.plan=p->plan.plan_id;p->meta.input_sha=p->frame.input_sha;p->meta.input_bytes=p->frame.buffers[2].size()*4;p->meta.cpu_bytes=3*p->total+(input.texture?input.texture->cpu_bytes():0);p->meta.resident_bytes=p->total;
  if(native){if(p->meta.cpu_bytes>frame_charge->bytes()||p->meta.input_bytes>(64ULL<<20))return R::failure(err(ENOSPC,"frozen P8 frame exceeds preallocated common lifetime bound"));p->asset_charge=std::move(asset_charge);p->frame_charge=std::move(frame_charge);p->meta.resident_bytes=p->asset_charge->bytes();p->meta.cpu_bytes=64ULL<<20;}
  vxl_hunet_sha h;vxl_hunet_sha_init(&h);vxl_hunet_sha_update(&h,p->meta.plan.data(),32);vxl_hunet_sha_update(&h,p->meta.input_sha.data(),32);vxl_hunet_sha_update(&h,&input.cookie_namespace,4);vxl_hunet_sha_update(&h,&input.display_cookie,8);
  for(const auto*s:{&input.request.boot_id,&input.request.client_id,&input.request.request_id}){uint32_t n=s->size();vxl_hunet_sha_update(&h,&n,4);vxl_hunet_sha_update(&h,s->data(),n);}
  const uint32_t state[]={uint32_t(scope),uint32_t(input.numeric_reference),input.scene.remaining,input.scene.startup_remaining,input.scene.displayed_pose,input.scene.view,uint32_t(input.scene.paused),uint32_t(input.scene.looping),uint32_t(input.scene.gallery),uint32_t(input.scene.automatic),uint32_t(input.scene.have_displayed)};vxl_hunet_sha_update(&h,state,sizeof(state));vxl_hunet_sha_update(&h,&input.scene.revision,8);vxl_hunet_sha_final(&h,p->source.data());return R::success(std::shared_ptr<P8WorkRequest>(new P8WorkRequest(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure(err(ENOMEM,"bounded CPU frame preparation"));}
}
uint64_t P8WorkRequest::size()const{return 4;}Digest P8WorkRequest::identity()const{return impl_->source;}
WorkRequest P8WorkRequest::request()const{auto r=impl_->meta;r.source=shared_from_this();return r;}
Result<WorkItem>P8WorkRequest::get(uint64_t i)const{
 if(i>=4){return Result<WorkItem>::failure(err(ERANGE,"frame source index"));}try{WorkItem w;w.request=impl_->meta.request;w.app=AppKind::P8;w.node=i+1;w.work_index=i;if(i){w.dependencies={i-1};}w.kind=i==0||i==2?WorkKind::GPU:i==1?WorkKind::CPU:WorkKind::TRANSFER;
  if(w.kind==WorkKind::GPU){w.operation=DeferredGpuRef{"p8.controlled.frame.v1",w.node,impl_->plan.policy.hardware.device_generation,(uint64_t(impl_->input.cookie_namespace)<<32)+i+1};}else{w.operation=CpuTaskRef{i==1?"p8.cpu.bins.v1":"p8.transfer.present.v1",w.node};}return Result<WorkItem>::success(std::move(w));
 }catch(const std::bad_alloc&){return Result<WorkItem>::failure(err(ENOMEM,"source metadata"));}
}
Result<bool>P8WorkRequest::ready(uint64_t i)const{auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);if(s.receipt.first_error){return Result<bool>::failure(*s.receipt.first_error);}if(i>=4||s.receipt.retired){return Result<bool>::failure(err(ESTALE,"retired frame"));}if(s.publishing){return Result<bool>::success(false);}if(i==1||i==3){return Result<bool>::success(true);}if(i==0&&s.session){auto ready=s.session->ready(s.meta.request);if(!ready||!*ready.value)return ready;}const auto&key=s.plan.programs[i==2?1:0];return s.lease?s.residency->ready_switch(*s.lease,key):s.residency->ready(s.meta.request,key);}
Result<P8OwnerReceipt>P8WorkRequest::receipt_snapshot()const{auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);try{return Result<P8OwnerReceipt>::success(s.receipt);}catch(const std::bad_alloc&){return Result<P8OwnerReceipt>::failure(err(ENOMEM,"receipt snapshot"));}}
ExecutionHooks P8WorkRequest::handlers(){auto self=shared_from_this();ExecutionHooks h;
 h.prepare_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&)->Result<GPUJobDesc>{auto&s=*self->impl_;std::lock_guard<std::mutex>lock(s.mutex);try{return s.prepare(w,b);}catch(...){return Result<GPUJobDesc>::failure(*s.fail(err(EIO,"preparation exception")).error);}};
 h.finish_gpu=[self](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){auto&s=*self->impl_;std::lock_guard<std::mutex>lock(s.mutex);try{return s.finish(w,b,r);}catch(...){return s.fail(err(EIO,"completion/evidence exception"));}};
 h.non_gpu=[self](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*pool,ExecutionReceipt&r){auto&s=*self->impl_;std::unique_lock<std::mutex>lock(s.mutex);auto checked=s.bind(w,b,false);if(!checked){return s.fail(*checked.error);}try{auto result=s.stage==1?s.bins(pool,w):s.stage==3?s.publish(r,lock):Result<void>::failure(err(EPROTO,"not CPU or transfer stage"));if(!result){return s.fail(*result.error);}s.stage++;r.outcome.status=RequestStatus::COMPLETED;r.outcome.result=0;r.outcome.retired=true;return Result<void>::success();}catch(...){return s.fail(err(EIO,"CPU or presentation exception"));}};
 h.retire_request=[self](RequestKey k,UnifiedBackend&b){auto&s=*self->impl_;std::lock_guard<std::mutex>lock(s.mutex);if(!same(k,s.meta.request)||&b!=s.device.get()){return Result<void>::failure(err(ESTALE,"retirement request/backend"));}if(s.receipt.first_error){return Result<void>::failure(*s.receipt.first_error);}return timed(s.receipt.release_ns,[&](){return s.release();});};return h;
}
bool P8WorkRequest::matches_texture_request(const P8Request&q,const BackendIdentity&hw,vxnn::EvidenceScope scope)const{
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);const auto&a=s.input;auto scene=[](const SceneState&x){return std::tie(x.object,x.theme,x.pose,x.view,x.remaining,x.startup_remaining,x.displayed_pose,x.completed,x.revision,x.gallery_due_ns,x.paused,x.looping,x.gallery,x.automatic,x.have_displayed);};
 return s.stage==0&&!s.active&&!s.receipt.retired&&!s.receipt.first_error&&same_hw(s.plan.policy.hardware,hw)&&s.scope==scope&&same(a.request,q.request)&&a.cookie_namespace==q.cookie_namespace&&a.display_cookie==q.display_cookie&&a.numeric_reference==q.numeric_reference&&a.texture_redraw&&q.texture_redraw&&scene(a.scene)==scene(q.scene)&&a.texture&&q.texture&&a.texture->identity()==q.texture->identity()&&a.texture->source_kind()==q.texture->source_kind()&&a.texture->source_scope()==q.texture->source_scope()&&a.texture->source_result_sha()==q.texture->source_result_sha()&&a.texture->source_tensor_sha()==q.texture->source_tensor_sha();
}

}

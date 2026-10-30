#include <vxunified/native_p8_service.hpp>
#include <vxunified/journal.hpp>
#include <vxunified/exit_retention.hpp>
#include <vxunified/sealed_assets.hpp>
#include <vxnn/cpu.hpp>
#include "nnrt/package/json.hpp"
#include "vendor/p8/network/texture_rx.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace vxunified {namespace {
vxnn::Error error(int n,const char*s){return {n,"NATIVE_P8_SERVICE",s};}
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))throw std::runtime_error("P8 monotonic clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
uint32_t word(const uint8_t*p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
struct Socket{int fd=-1;~Socket(){if(fd>=0)::close(fd);}void close(){if(fd>=0){::close(fd);fd=-1;}}};
const std::string control_keys="123isprlfadbgqezc";
struct ControlSource final:WorkSource {
 RequestKey request;Digest sha{};std::string operation="p8.control.v1";std::function<Result<bool>()>available;
 uint64_t size()const override{return 1;}Digest identity()const override{return sha;}
 Result<bool>ready(uint64_t i)const override{return i?Result<bool>::failure(error(ERANGE,"control work index")):available();}
 Result<WorkItem>get(uint64_t i)const override{if(i)return Result<WorkItem>::failure(error(ERANGE,"control work index"));WorkItem w;w.request=request;w.app=AppKind::P8;w.node=1;w.kind=WorkKind::CPU;w.work_index=0;w.operation=CpuTaskRef{operation,1};return Result<WorkItem>::success(std::move(w));}
};
struct ControlTicket {std::shared_ptr<NativeRequestFiles>file;RequestKey key;std::optional<vxnn::Error>first_error;bool executed=false;};
bool same_request(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;}
struct FileTransaction {
 RequestKey command;std::string id;std::shared_ptr<const TexturePackage>bank;std::shared_ptr<HostMemoryLease>charge;
 TexturePhase phase=TexturePhase::STAGED;std::optional<uint64_t>serial,gpu_raw_cycles;std::string clock_domain;uint64_t begin_ns=0,prepare_ns=0,seal_ns=0,source_ns=0,package_ns=0,end_ns=0;
};
bool pending_file(const std::shared_ptr<FileTransaction>&f){return f&&(f->phase==TexturePhase::STAGED||f->phase==TexturePhase::QUEUED);}
std::string file_json(const FileTransaction&f,bool accepted,bool superseded=false){
 namespace j=vxnn::json;auto o=j::parse("{}");auto add=[&](const char*k,json_object*v){if(!v)throw std::bad_alloc();if(json_object_object_add(o.get(),k,v)){json_object_put(v);throw std::bad_alloc();}};
 auto text=[&](const char*k,const std::string&v){add(k,json_object_new_string_len(v.data(),int(v.size())));};auto number=[&](const char*k,uint64_t v){add(k,json_object_new_uint64(v));};
 text("format","native-p8-file-command-v1");text("input_id",f.id);text("source_kind","MOSAIC_RESULT");text("package_id",vxnn::digest_hex(f.bank->identity()));text("source_result_sha256",vxnn::digest_hex(f.bank->source_result_sha()));text("source_tensor_sha256",vxnn::digest_hex(f.bank->source_tensor_sha()));number("source_scope",uint32_t(f.bank->source_scope()));
 auto original=j::parse(f.bank->metadata());auto*source=j::member(original.get(),"source");text("source_numeric_class",j::str(j::member(source,"numeric_class")));text("source_metadata_sha256",vxnn::digest_hex(vxnn::content_digest(f.bank->metadata())));
 const char*phase=superseded?"SUPERSEDED":accepted?"FILE_ACCEPTED":f.phase==TexturePhase::APPLIED?"APPLIED":f.phase==TexturePhase::CANCELLED?"CANCELLED":f.phase==TexturePhase::QUEUED?"QUEUED":"STAGED";text("phase",phase);add("applied",json_object_new_boolean(!accepted&&!superseded&&f.phase==TexturePhase::APPLIED));number("serial",f.serial.value_or(0));number("begin_ns",f.begin_ns);number("prepare_ns",f.prepare_ns);number("source_sealed_read_ns",f.seal_ns);number("source_import_ns",f.source_ns);number("package_verify_ns",f.package_ns);text("clock_domain",f.clock_domain);number("end_ns",f.end_ns);
 if(!accepted&&f.gpu_raw_cycles)number("gpu_raw_cycles",*f.gpu_raw_cycles);else if(json_object_object_add(o.get(),"gpu_raw_cycles",nullptr))throw std::bad_alloc();return j::dump(o.get());
}
}
struct NativeP8Service::State {
 detail::ExitRetention retention;StartupContext ctx;std::shared_ptr<NativeP8Library>library;std::shared_ptr<NativeRouter>router;std::shared_ptr<TextureService>receiver;
 Socket socket;uint16_t port=0;uint32_t session=0;uint64_t display_base=0,next=1;
 std::atomic<bool>closing{false};std::mutex io_mutex,work_mutex,metrics_mutex;bool bound=false,released=false;
 SceneState scene;std::shared_ptr<const TexturePackage>current;NativeP8ServiceStats metrics,wire_baseline;uint32_t wire_tx=0;uint64_t wire_begin=0;
 uint32_t startup_selftest_frames=100;std::shared_ptr<NativeRequestFiles>startup_policy_file;
 std::optional<TextureIngress>staged_file;
 // One receiver transaction, not an execution queue. Its CPU command retires
 // before step() reserves the original shared queue slot for the P8 frame.
 std::shared_ptr<FileTransaction>file;
 std::shared_ptr<ResidentLease>control_resident;
 struct Active{P8Request input;NativeP8Prepared prepared;std::optional<uint64_t>texture_serial;NativeP8ServiceStats baseline;uint64_t begin=0,prepare_ns=0;};std::unique_ptr<Active>active;
 Result<void>finish(bool drained){
  if(!active)return Result<void>::failure(error(ENOENT,"no active frame"));
  auto&a=*active;auto rt=ctx.runtime.lock();auto journal=ctx.journal.lock();if(!rt||!journal)return Result<void>::failure(error(ESHUTDOWN,"common roots expired"));auto begin=now();auto got=a.prepared.owner->receipt_snapshot();if(!got)return Result<void>::failure(*got.error);const auto&r=*got.value;
  if(r.first_error)return Result<void>::failure(*r.first_error);
  if(r.evidence_error)return Result<void>::failure(*r.evidence_error);
  if(!r.retired||!r.published||r.completed_gpu!=2||!r.output_crc_valid||!r.digest_ns||!r.next_scene||!r.display||!r.display->presented_ns)return Result<void>::failure(error(EBUSY,"original complete frame and display retirement required"));
  if((ctx.scope==vxnn::EvidenceScope::REAL_BOARD&&(!r.gpu_raw_cycles||!*r.gpu_raw_cycles))||(ctx.scope!=vxnn::EvidenceScope::REAL_BOARD&&r.gpu_raw_cycles))return Result<void>::failure(error(EPROTO,"raw cycle scope differs"));
  if(!drained){auto output=rt->take_result(a.input.request);if(!output)return Result<void>::failure(*output.error);if(!output.value->binding||output.value->binding->source!=a.prepared.work.source->identity()||output.value->gpu_raw_cycles!=r.gpu_raw_cycles)return Result<void>::failure(error(ESTALE,"published source or cycles differ"));}
  NativeP8ServiceStats measured;{std::lock_guard<std::mutex>lock(metrics_mutex);measured=metrics;}
  if(r.gpu_raw_cycles&&measured.gpu_raw_cycles&&*r.gpu_raw_cycles>UINT64_MAX-*measured.gpu_raw_cycles)return Result<void>::failure(error(EOVERFLOW,"cumulative raw cycles exhausted"));
  IngressTiming timing;timing.serial=a.texture_serial.value_or(next-1);timing.begin_ns=a.begin;timing.end_ns=now();timing.prepare_ns=a.prepare_ns;timing.complete_ns=timing.end_ns-begin;
  const bool wire=a.texture_serial&&a.input.request.client_id!="texture-file";if(wire){timing.packets=measured.packets-a.baseline.packets;timing.bytes=measured.wire_bytes-a.baseline.wire_bytes;timing.protocol_ns=measured.protocol_ns-a.baseline.protocol_ns;}
  // Required evidence precedes the original scene or wire APPLIED commit.
  auto recorded=wire?journal->save_ingress(a.input.request,ctx.identity,ctx.scope,timing):journal->save_frame_timing(a.input.request,ctx.identity,ctx.scope,timing);if(!recorded)return recorded;
  if(a.texture_serial){auto updated=receiver->complete(*a.texture_serial);if(!updated)return Result<void>::failure(*updated.error);scene=*updated.value;current=a.input.texture;if(file&&file->serial==a.texture_serial){file->phase=TexturePhase::APPLIED;file->end_ns=now();file->gpu_raw_cycles=r.gpu_raw_cycles;}}else scene=*r.next_scene;
  if(!drained){auto reaped=rt->reap_result(a.input.request);if(!reaped)return reaped;}
  {std::lock_guard<std::mutex>lock(metrics_mutex);++metrics.completed;metrics.complete_ns+=now()-begin;if(r.gpu_raw_cycles)metrics.gpu_raw_cycles=metrics.gpu_raw_cycles.value_or(0)+*r.gpu_raw_cycles;}
  active.reset();return Result<void>::success();
 }
};
NativeP8Service::NativeP8Service(std::shared_ptr<State>s):state_(std::move(s)){}
NativeP8Service::~NativeP8Service(){try{if(quiesce()&&release())return;}catch(...){}state_->retention.retain(state_);}
Result<std::shared_ptr<NativeP8Service>>NativeP8Service::create(const StartupContext&c,std::shared_ptr<NativeP8Library>library,std::shared_ptr<NativeRouter>router,uint32_t session,uint64_t display,uint16_t port,const std::string&address,uint32_t startup_selftest_frames){
 using R=Result<std::shared_ptr<NativeP8Service>>;try{
  if(startup_selftest_frames!=1&&startup_selftest_frames!=100)return R::failure(error(EINVAL,"startup self-test must be daily1 or maintenance100"));
  auto gate=admit_native_startup(c,2);if(!gate)return R::failure(*gate.error);auto rt=c.runtime.lock();sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(port);
  if(!library||!library->owns_runtime(rt.get())||!router||!router->owns_runtime(rt.get())||!session||!display||display>UINT32_MAX||inet_pton(AF_INET,address.c_str(),&endpoint.sin_addr)!=1)return R::failure(error(EXDEV,"same P8 library/router and bounded native endpoint required"));
  auto s=std::make_shared<State>();s->ctx=c;s->library=std::move(library);s->router=std::move(router);s->session=session;s->display_base=display;
  // Initialize the policy before preparing the first fixedpose0 frame. Every
  // decrement still comes exclusively from original checked frame retirement.
  s->startup_selftest_frames=startup_selftest_frames;s->scene.remaining=startup_selftest_frames;s->scene.startup_remaining=startup_selftest_frames;
  auto receiver=TextureService::create_native(c,s->router,session,display);if(!receiver)return R::failure(*receiver.error);s->receiver=*receiver.value;
  s->socket.fd=::socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(s->socket.fd<0)return R::failure(error(errno,"P8 UDP socket"));if(::bind(s->socket.fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint)))return R::failure(error(errno,"exclusive P8 UDP bind"));socklen_t length=sizeof(endpoint);if(getsockname(s->socket.fd,reinterpret_cast<sockaddr*>(&endpoint),&length))return R::failure(error(errno,"actual P8 endpoint"));s->port=ntohs(endpoint.sin_port);
  return R::success(std::shared_ptr<NativeP8Service>(new NativeP8Service(s)));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"P8 startup allocation"));}catch(const std::exception&e){return R::failure({EIO,"NATIVE_P8_SERVICE",e.what()});}
}
Result<void>NativeP8Service::bind(ApplicationSet&set,std::shared_ptr<NativeFiles>files,std::vector<RegisteredTextureFile>textures){
 try{auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->bound||s->closing||set.poll_native||set.prepare_native||!s->ctx.notify_work)return Result<void>::failure(error(EBUSY,"one common binding and worker wake required"));auto self=shared_from_this();auto old_activate=set.activate,old_stop=set.quiesce,old_release=set.release_resources,old_settle=set.settle_native;
  if(files){
   auto rt=s->ctx.runtime.lock();if(!rt||!files->owns_runtime(rt.get())||std::any_of(set.entries.begin(),set.entries.end(),[](const TrustedApplication&a){return a.id=="p8-control";}))return Result<void>::failure(error(EXDEV,"same common file owner and distinct P8 control registry required"));
   if(!s->startup_policy_file){
    auto file=files->request({s->ctx.boot_id,"p8-policy",std::to_string(s->session)},0,4096);if(!file)return Result<void>::failure(*file.error);
    std::ostringstream row;row<<"{\"format\":\"native-p8-startup-policy-v1\",\"startup_selftest_frames\":"<<s->startup_selftest_frames<<",\"policy\":\""<<(s->startup_selftest_frames==1?"DAILY1":"MAINTENANCE100")<<"\",\"initial_pose\":0,\"numeric_reference\":true,\"completion_gate\":\"ORIGINAL_CHECKED_FRAME_AND_DP_RETIREMENT\",\"hardware\":\""<<vxnn::digest_hex(s->ctx.identity.hardware_contract)<<"\",\"bit\":\""<<vxnn::digest_hex(s->ctx.identity.bit)<<"\",\"xsa\":\""<<vxnn::digest_hex(s->ctx.identity.xsa)<<"\",\"device_generation\":"<<s->ctx.identity.device_generation<<",\"scope\":\""<<(s->ctx.scope==vxnn::EvidenceScope::REAL_BOARD?"REAL_BOARD":"HOST_PROTOCOL_TEST")<<"\",\"acceptance\":\"NOT_RUN\",\"gpu_raw_cycles\":null}\n";
    auto text=row.str();auto saved=(*file.value)->output("startup-policy.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()},"JSON_UTF8",{text.size()});if(!saved)return Result<void>::failure(*saved.error);s->startup_policy_file=*file.value;
   }
   const auto model=vxnn::content_digest("P8_REGISTERED_CONTROL_MODEL_V1:"+vxnn::digest_hex(s->ctx.identity.hardware_contract));
   const auto plan=vxnn::content_digest("P8_REGISTERED_CONTROL_PLAN_V1:"+vxnn::digest_hex(s->ctx.identity.bit)+":"+vxnn::digest_hex(s->ctx.identity.xsa));
   auto charge=rt->reserve_resident(model,1u<<20,AppKind::P8);if(!charge)return Result<void>::failure(*charge.error);s->control_resident=*charge.value;
   TrustedApplication entry;entry.id="p8-control";entry.app=AppKind::P8;entry.hardware=s->ctx.identity.hardware_contract;entry.model=model;entry.plan=plan;
   for(char key:control_keys)entry.inputs.push_back({std::string(1,key),vxnn::content_digest(std::string(1,key)),1});
   auto valid=[self](const RpcRequest&q)->Result<void>{
    if(q.command!="submit"||q.entry_id!="p8-control"||q.parameters!="{}"||q.input_id.size()!=1||control_keys.find(q.input_id[0])==std::string::npos)return Result<void>::failure(error(EINVAL,"only registered original P8 key and empty parameters allowed"));
    auto state=self->state_;std::unique_lock<std::mutex>guard(state->work_mutex,std::try_to_lock);if(!guard.owns_lock())return Result<void>::failure(error(EAGAIN,"P8 scene metadata busy"));
    if(state->closing||state->released)return Result<void>::failure(error(ESHUTDOWN,"P8 control closed"));
    if(state->scene.startup_remaining)return Result<void>::failure(error(EAGAIN,"configured startup self-test must retire before control admission"));
    return Result<void>::success();
   };
   entry.validate=valid;
   entry.prepare=[self,files,model,plan,valid](const RpcRequest&q)->Result<WorkRequest>{
    using R=Result<WorkRequest>;auto state=self->state_;auto check=valid(q);if(!check)return R::failure(*check.error);
    auto file=files->request(q.request,4096,4096);if(!file)return R::failure(*file.error);auto ticket=std::make_shared<ControlTicket>();ticket->file=*file.value;ticket->key=q.request;
    auto source=std::make_shared<ControlSource>();source->request=q.request;source->sha=vxnn::content_digest("P8_REGISTERED_CONTROL_SOURCE_V1:"+vxnn::digest_hex(model)+":"+vxnn::digest_hex(plan)+":"+q.input_id);
    source->available=[weak=std::weak_ptr<State>(state)]()->Result<bool>{auto owner=weak.lock();if(!owner)return Result<bool>::failure(error(ESHUTDOWN,"P8 control owner expired"));std::unique_lock<std::mutex>guard(owner->work_mutex,std::try_to_lock);if(!guard.owns_lock())return Result<bool>::success(false);return Result<bool>::success(!owner->closing&&!owner->active&&!owner->staged_file&&!pending_file(owner->file)&&!owner->scene.startup_remaining);};
    ExecutionHooks hooks;
    hooks.prepare_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<GPUJobDesc>::failure(error(EPERM,"P8 control is CPU metadata only"));};
    hooks.finish_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<void>::failure(error(EPERM,"P8 control cannot finish a GPU job"));};
    hooks.non_gpu=[self,ticket,q,model,plan](const WorkItem&w,UnifiedBackend&,vxnn::CpuPool*pool,ExecutionReceipt&r)->Result<void>{
     try{
      const auto*op=std::get_if<CpuTaskRef>(&w.operation);if(w.kind!=WorkKind::CPU||w.work_index||w.node!=1||!op||op->registered_name!="p8.control.v1"||op->node!=1||!pool||pool->workers()!=4||!w.pending_token||!w.cancel_control||ticket->executed)return Result<void>::failure(error(EPROTO,"exact common CPU control reservation required"));
      if(w.cancel_control->cancelled.load())return Result<void>::failure(error(ECANCELED,"P8 control canceled before state commit"));
      auto owner=self->state_;const auto begin=now();SceneState snapshot;bool applied=false;int command_error=0;
      {std::lock_guard<std::mutex>guard(owner->work_mutex);if(owner->closing||owner->active||owner->staged_file||pending_file(owner->file))return Result<void>::failure(error(EBUSY,"P8 control cannot preempt a frame or texture"));auto next=scene_control(owner->scene,q.input_id[0],begin);if(next){owner->scene=*next.value;applied=true;}else command_error=next.error->code;snapshot=owner->scene;ticket->executed=true;}
      namespace j=vxnn::json;auto metadata=j::parse("{}");auto add=[&](const char*k,json_object*v){if(!v)throw std::bad_alloc();if(json_object_object_add(metadata.get(),k,v)){json_object_put(v);throw std::bad_alloc();}};auto text=[&](const char*k,const std::string&v){add(k,json_object_new_string_len(v.data(),int(v.size())));};auto number=[&](const char*k,uint64_t v){add(k,json_object_new_uint64(v));};auto flag=[&](const char*k,bool v){add(k,json_object_new_boolean(v));};
      text("format","native-p8-control-v1");text("command",q.input_id);text("clock_domain",owner->ctx.scope==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC");number("startup_selftest_frames",owner->startup_selftest_frames);flag("applied",applied);number("command_error",uint64_t(command_error));number("revision",snapshot.revision);number("completed",snapshot.completed);number("object",uint32_t(snapshot.object));number("pose",snapshot.pose);number("view",snapshot.view);number("remaining",snapshot.remaining);number("startup_remaining",snapshot.startup_remaining);flag("paused",snapshot.paused);flag("looping",snapshot.looping);flag("gallery",snapshot.gallery);flag("automatic",snapshot.automatic);flag("have_displayed",snapshot.have_displayed);text("theme",vxnn::digest_hex(snapshot.theme));number("begin_ns",begin);number("end_ns",now());if(json_object_object_add(metadata.get(),"gpu_raw_cycles",nullptr))throw std::bad_alloc();
      number("displayed_pose",snapshot.displayed_pose);number("gallery_due_ns",snapshot.gallery_due_ns);
      auto raw=j::dump(metadata.get());if(raw.size()>4096)return Result<void>::failure(error(EOVERFLOW,"bounded P8 control receipt"));auto artifact=ticket->file->output("control.json",{reinterpret_cast<const uint8_t*>(raw.data()),raw.size()},"JSON_UTF8",{raw.size()});if(!artifact){ticket->first_error=*artifact.error;return Result<void>::failure(*artifact.error);}
      PublishedResult p;p.execution_complete=true;p.lease.producer=q.request;p.lease.app=AppKind::P8;p.lease.scope=owner->ctx.scope;p.lease.hardware=owner->ctx.identity.hardware_contract;p.lease.model=model;p.lease.plan=plan;p.lease.artifacts={*artifact.value};p.raw_legacy_json=raw;r.output=std::move(p);r.outcome.status=RequestStatus::COMPLETED;r.outcome.result=0;r.outcome.retired=true;
      return owner->ctx.notify_work?owner->ctx.notify_work():Result<void>::success();
     }catch(const std::bad_alloc&){ticket->first_error=error(ENOMEM,"P8 control metadata allocation");return Result<void>::failure(*ticket->first_error);}catch(const std::exception&e){ticket->first_error=vxnn::Error{EIO,"NATIVE_P8_SERVICE",e.what()};return Result<void>::failure(*ticket->first_error);}
    };
    hooks.retire_request=[ticket](RequestKey key,UnifiedBackend&)->Result<void>{if(key.boot_id!=ticket->key.boot_id||key.client_id!=ticket->key.client_id||key.request_id!=ticket->key.request_id)return Result<void>::failure(error(ESTALE,"P8 control retirement identity"));return ticket->first_error?Result<void>::failure(*ticket->first_error):Result<void>::success();};
    WorkRequest request;request.request=q.request;request.app=AppKind::P8;request.model=model;request.plan=plan;request.input_sha=vxnn::content_digest(q.input_id);request.input_bytes=1;request.cpu_bytes=8192;request.resident_bytes=1u<<20;request.source=source;
    return state->router->bind_source(std::move(request),std::move(hooks),state->control_resident,ticket);
   };
   set.entries.push_back(std::move(entry));
   if(!textures.empty()){
    if(textures.size()>16||std::any_of(set.entries.begin(),set.entries.end(),[](const TrustedApplication&a){return a.id=="p8-texture";}))return Result<void>::failure(error(EINVAL,"bounded distinct sealed texture registry required"));
    TrustedApplication texture;texture.id="p8-texture";texture.app=AppKind::P8;texture.hardware=s->ctx.identity.hardware_contract;texture.model=model;texture.plan=vxnn::content_digest("P8_SEALED_MOSAIC_FILE_PLAN_V1:"+vxnn::digest_hex(plan));
    std::set<std::string>ids;for(const auto&f:textures){if(f.id.empty()||f.id.size()>128||!ids.insert(f.id).second||!nonzero_digest(f.result_sha)||!nonzero_digest(f.policy.expected_package_id))return Result<void>::failure(error(EINVAL,"sealed texture identifier and hashes required"));texture.inputs.push_back({f.id,vxnn::content_digest(f.id),f.id.size()});}
    auto validate=[self,textures](const RpcRequest&q)->Result<void>{
     if(q.command!="submit"||q.entry_id!="p8-texture"||q.parameters!="{}"||std::none_of(textures.begin(),textures.end(),[&](const RegisteredTextureFile&f){return f.id==q.input_id;}))return Result<void>::failure(error(EINVAL,"only a sealed Mosaic texture identifier and empty parameters allowed"));
     auto state=self->state_;std::unique_lock<std::mutex>guard(state->work_mutex,std::try_to_lock);if(!guard.owns_lock())return Result<void>::failure(error(EAGAIN,"P8 scene metadata busy"));if(state->closing||state->released)return Result<void>::failure(error(ESHUTDOWN,"P8 file admission closed"));if(state->scene.startup_remaining||state->active||state->staged_file||pending_file(state->file))return Result<void>::failure(error(EAGAIN,"original startup/frame/texture must retire first"));auto wire=state->receiver->snapshot();if(!wire)return Result<void>::failure(*wire.error);if(wire.value->phase==TexturePhase::RECEIVING||wire.value->phase==TexturePhase::STAGED||wire.value->phase==TexturePhase::QUEUED)return Result<void>::failure(error(EBUSY,"one original wire or file texture transaction"));return Result<void>::success();
    };
    texture.validate=validate;const auto texture_plan=texture.plan;
    texture.prepare=[self,files,textures,validate,model,texture_plan](const RpcRequest&q)->Result<WorkRequest>{
     using R=Result<WorkRequest>;auto check=validate(q);if(!check)return R::failure(*check.error);auto state=self->state_;auto rt=state->ctx.runtime.lock();if(!rt)return R::failure(error(ESHUTDOWN,"common Runtime expired"));auto spec=std::find_if(textures.begin(),textures.end(),[&](const RegisteredTextureFile&f){return f.id==q.input_id;});
     // Covers both original FP32 copies/RGB conversion, all LODs and bounded
     // metadata, before import allocates. Retained bank shares this same lease.
     auto charge=rt->reserve_host_memory(128ULL<<20);if(!charge)return R::failure(*charge.error);const auto begin=now();auto sealed=read_sealed_native_asset(spec->result_path,262144);if(!sealed)return R::failure(*sealed.error);if(vxnn::content_digest(*sealed.value)!=spec->result_sha)return R::failure(error(ESTALE,"sealed Mosaic source JSON differs"));const auto sealed_ns=now();auto source=import_result(spec->result_path);if(!source)return R::failure(*source.error);if(source.value->original_json!=*sealed.value)return R::failure(error(ESTALE,"source changed after sealed read"));const auto imported_ns=now();auto bank=make_texture(*source.value,spec->policy);if(!bank)return R::failure(*bank.error);if((*bank.value)->source_kind()!=TextureOrigin::MOSAIC_RESULT||(*bank.value)->cpu_bytes()>(128ULL<<20))return R::failure(error(ENOSPC,"bounded original Mosaic file bank required"));
     auto transaction=std::make_shared<FileTransaction>();transaction->command=q.request;transaction->id=q.input_id;transaction->bank=*bank.value;transaction->charge=*charge.value;transaction->begin_ns=begin;const auto prepared_ns=now();transaction->prepare_ns=prepared_ns-begin;transaction->seal_ns=sealed_ns-begin;transaction->source_ns=imported_ns-sealed_ns;transaction->package_ns=prepared_ns-imported_ns;transaction->clock_domain=state->ctx.scope==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC";auto file=files->request(q.request,4096,524288);if(!file)return R::failure(*file.error);auto ticket=std::make_shared<ControlTicket>();ticket->file=*file.value;ticket->key=q.request;
     auto source_work=std::make_shared<ControlSource>();source_work->request=q.request;source_work->operation="p8.file.v1";source_work->sha=vxnn::content_digest("P8_FILE_SOURCE_V1:"+vxnn::digest_hex(transaction->bank->identity())+":"+vxnn::digest_hex(spec->result_sha)+":"+vxnn::digest_hex(texture_plan));source_work->available=[weak=std::weak_ptr<State>(state)]()->Result<bool>{auto owner=weak.lock();if(!owner)return Result<bool>::failure(error(ESHUTDOWN,"P8 owner expired"));std::unique_lock<std::mutex>guard(owner->work_mutex,std::try_to_lock);if(!guard.owns_lock())return Result<bool>::success(false);return Result<bool>::success(!owner->closing&&!owner->active&&!owner->staged_file&&!pending_file(owner->file)&&!owner->scene.startup_remaining);};
     ExecutionHooks hooks;hooks.prepare_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<GPUJobDesc>::failure(error(EPERM,"file admission cannot submit a GPU job"));};hooks.finish_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<void>::failure(error(EPERM,"file admission is CPU metadata"));};
     hooks.non_gpu=[self,ticket,transaction,model,texture_plan](const WorkItem&w,UnifiedBackend&,vxnn::CpuPool*pool,ExecutionReceipt&r)->Result<void>{
      const auto*op=std::get_if<CpuTaskRef>(&w.operation);if(w.kind!=WorkKind::CPU||w.work_index||!op||op->registered_name!="p8.file.v1"||!pool||pool->workers()!=4||!w.pending_token||!w.cancel_control||ticket->executed)return Result<void>::failure(error(EPROTO,"exact common file CPU work required"));if(w.cancel_control->cancelled.load())return Result<void>::failure(error(ECANCELED,"file command cancelled before acceptance"));
      auto owner=self->state_;auto metadata=transaction->bank->metadata();auto ack=file_json(*transaction,true);auto artifact=ticket->file->output("texture-command.json",{reinterpret_cast<const uint8_t*>(ack.data()),ack.size()},"JSON_UTF8",{ack.size()});if(!artifact){ticket->first_error=*artifact.error;return Result<void>::failure(*artifact.error);}auto original=ticket->file->output("texture-source.json",{reinterpret_cast<const uint8_t*>(metadata.data()),metadata.size()},"JSON_UTF8",{metadata.size()});if(!original){ticket->first_error=*original.error;return Result<void>::failure(*original.error);}
      {std::lock_guard<std::mutex>guard(owner->work_mutex);if(owner->closing||owner->active||owner->staged_file||pending_file(owner->file))return Result<void>::failure(error(EBUSY,"file commit cannot preempt frame/texture"));owner->file=transaction;ticket->executed=true;}
      PublishedResult p;p.execution_complete=true;p.lease.producer=transaction->command;p.lease.app=AppKind::P8;p.lease.scope=owner->ctx.scope;p.lease.hardware=owner->ctx.identity.hardware_contract;p.lease.model=model;p.lease.plan=texture_plan;p.lease.artifacts={*artifact.value,*original.value};p.raw_legacy_json=ack;r.output=std::move(p);r.outcome.status=RequestStatus::COMPLETED;r.outcome.result=0;r.outcome.retired=true;return owner->ctx.notify_work();
     };
     hooks.retire_request=[ticket](RequestKey key,UnifiedBackend&)->Result<void>{if(!same_request(key,ticket->key))return Result<void>::failure(error(ESTALE,"file command retirement identity"));return ticket->first_error?Result<void>::failure(*ticket->first_error):Result<void>::success();};
     WorkRequest request;request.request=q.request;request.app=AppKind::P8;request.model=model;request.plan=texture_plan;request.input_sha=vxnn::content_digest(q.input_id);request.input_bytes=q.input_id.size();request.cpu_bytes=1u<<20;request.resident_bytes=1u<<20;request.source=source_work;return state->router->bind_source(std::move(request),std::move(hooks),state->control_resident,ticket);
    };
    set.entries.push_back(std::move(texture));auto previous=set.export_result;
    set.export_result=[self,files,previous](Runtime&rt,const RpcRequest&q,const PublishedResult&p)->Result<std::string>{
     if(q.destination_id!="texture-status-v1"){return previous?previous(rt,q,p):Result<std::string>::failure(error(EOPNOTSUPP,"unregistered P8 export destination"));}
     if(!files->owns_runtime(&rt)||q.artifact!=0||p.lease.app!=AppKind::P8){return Result<std::string>::failure(error(EXDEV,"same common file command artifact0 required"));}
     auto state=self->state_;std::unique_lock<std::mutex>guard(state->work_mutex,std::try_to_lock);if(!guard.owns_lock()){return Result<std::string>::failure(error(EAGAIN,"P8 transaction metadata busy"));}
     if(!state->file||!same_request(q.request,state->file->command)){return Result<std::string>::success("{\"phase\":\"SUPERSEDED\",\"applied\":false,\"gpu_raw_cycles\":null}");}
     return Result<std::string>::success(file_json(*state->file,false));
    };
   }
  }
  std::function<Result<void>(uint32_t)>poller=[self](uint32_t ms){return self->poll(ms);};std::function<Result<bool>()>prepare=[self](){return self->step();};std::function<Result<void>()>activate=[self,old_activate](){if(old_activate){auto r=old_activate();if(!r)return r;}return self->state_->ctx.notify_work();};std::function<Result<void>()>stop=[self,old_stop](){auto r=self->quiesce();if(!r)return r;return old_stop?old_stop():Result<void>::success();};std::function<Result<void>()>release=[self,old_release](){auto r=self->release();if(!r)return r;return old_release?old_release():Result<void>::success();};
  set.settle_native=[self,old_settle](){if(old_settle){auto r=old_settle();if(!r)return r;}return self->settle_inactive();};
  set.poll_native=std::move(poller);set.prepare_native=std::move(prepare);set.activate=std::move(activate);set.quiesce=std::move(stop);set.release_resources=std::move(release);s->bound=true;return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure(error(ENOMEM,"P8 hook allocation"));}
}
Result<void>NativeP8Service::poll(uint32_t timeout){
 if(auto rt=state_->ctx.runtime.lock())if(!rt->mode_allows(AppKind::P8))return Result<void>::success();
 try{auto s=state_;std::lock_guard<std::mutex>lock(s->io_mutex);if(s->closing)return Result<void>::success();pollfd fd{s->socket.fd,POLLIN,0};int ready=::poll(&fd,1,int(std::min(timeout,uint32_t(50))));if(ready<0&&errno==EINTR)return Result<void>::success();if(ready<0||fd.revents&(POLLERR|POLLHUP|POLLNVAL))return Result<void>::failure(error(ready<0?errno:EIO,"P8 socket poll"));
  for(unsigned i=0;ready&&i<8;++i){uint8_t packet[1280];sockaddr_in peer{};socklen_t length=sizeof(peer);auto n=recvfrom(s->socket.fd,packet,sizeof(packet),MSG_TRUNC,reinterpret_cast<sockaddr*>(&peer),&length);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;if(n<0&&errno==EINTR)continue;if(n<0)return Result<void>::failure(error(errno,"P8 UDP receive"));if(n>ssize_t(sizeof(packet)))continue;auto begin=now();auto reply=s->receiver->handle({packet,size_t(n)},{peer.sin_addr.s_addr,ntohs(peer.sin_port)},begin/1000000);if(!reply)return Result<void>::failure(*reply.error);auto elapsed=now()-begin;
   {std::lock_guard<std::mutex>metrics(s->metrics_mutex);if(n>=64&&packet[5]==TEX_BEGIN&&word(packet+8)==s->session&&reply.value->size()==64&&word(reply.value->data()+40)==TEX_RECEIVING&&word(packet+12)!=s->wire_tx){s->wire_tx=word(packet+12);s->wire_begin=begin;s->wire_baseline=s->metrics;}++s->metrics.packets;s->metrics.wire_bytes+=size_t(n);s->metrics.protocol_ns+=elapsed;}
   if(!reply.value->empty()){auto sent=sendto(s->socket.fd,reply.value->data(),reply.value->size(),0,reinterpret_cast<sockaddr*>(&peer),length);if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)return Result<void>::failure(error(errno,"P8 UDP reply"));if(sent>=0&&size_t(sent)!=reply.value->size())return Result<void>::failure(error(EIO,"short P8 UDP reply"));}
  }
  auto tick=s->receiver->poll(now()/1000000);if(!tick)return tick;
  // Poll never waits on scene preparation; deadlines run on the existing worker.
  return s->ctx.notify_work?s->ctx.notify_work():Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure(error(ENOMEM,"bounded P8 wire metadata"));}catch(const std::exception&e){return Result<void>::failure({EIO,"NATIVE_P8_SERVICE",e.what()});}
}
Result<void>NativeP8Service::settle_inactive(){
 using R=Result<void>;try{auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->released)return R::success();auto rt=s->ctx.runtime.lock();if(!rt)return R::failure(error(ESHUTDOWN,"common P8 Runtime expired; root retained"));
  // Only the sole worker calls this hook, outside Runtime locks. A leaving
  // switch whose current mode is still P8 must retain its inflight frame.
  if(!s8_joint_platform(s->ctx.identity)||rt->mode_snapshot().current==AppKind::P8)return R::success();
  if(rt->stats().phase!=RuntimePhase::READY)return R::failure(error(EBUSY,"common fault/delivery state retains P8 root"));
  if(s->active){auto status=rt->status(s->active->input.request);if(!status)return R::failure(*status.error);if(*status.value!=RequestStatus::COMPLETED)return R::failure(error(EBUSY,"active P8 frame completion unknown; root retained"));auto done=s->finish(false);if(!done)return done;}
  auto wire=s->receiver->snapshot();if(!wire)return R::failure(*wire.error);if(wire.value->first_error)return R::failure(*wire.value->first_error);if(wire.value->phase==TexturePhase::FAILED)return R::failure(error(EIO,"receiver fault retains P8 root"));
  // CPU-only staged/receiving packets do not borrow the DATA root. Session
  // idle/epoch and the kernel's original pins remain the release authority.
  return s->library->release_inactive_session();
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"mode retirement metadata; root retained"));}catch(const std::exception&e){return R::failure({EIO,"NATIVE_P8_SERVICE",e.what()});}
}
Result<bool>NativeP8Service::step(){
 using R=Result<bool>;try{auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->closing)return R::success(false);auto rt=s->ctx.runtime.lock();auto journal=s->ctx.journal.lock();if(!rt||!journal)return R::failure(error(ESHUTDOWN,"common P8 roots expired"));
  if(s->active){auto status=rt->status(s->active->input.request);if(!status)return R::failure(*status.error);if(*status.value==RequestStatus::FAILED||*status.value==RequestStatus::CANCELLED)return R::failure(error(EIO,"original P8 frame failed or cancelled"));if(*status.value!=RequestStatus::COMPLETED)return R::success(false);auto done=s->finish(false);return done?R::success(true):R::failure(*done.error);}
  if(!rt->mode_allows(AppKind::P8)&&!s->staged_file){
   // No active frame remains (handled above). STAGED can also mean complete
   // CPU-only wire data. Session idle/epoch and original kernel pins decide
   // whether this retired DATA root is free; CPU staging is preserved.
   auto wire=s->receiver->snapshot();if(!wire)return R::failure(*wire.error);
   if(wire.value->first_error)return R::failure(*wire.value->first_error);
   auto freed=s->library->release_inactive_session();return freed?R::success(false):R::failure(*freed.error);
  }
  if(pending_file(s->file)&&!s->file->serial){
   if(now()-s->file->begin_ns>UINT64_C(60000000000))return R::failure(error(ETIMEDOUT,"file admission waited beyond bounded original queue window"));
   auto staged=s->receiver->stage_native(s->file->bank,s->scene,s->file->charge);
   if(staged){s->staged_file=*staged.value;s->file->serial=staged.value->serial;}
   else if(staged.error->code!=EAGAIN&&staged.error->code!=EBUSY)return R::failure(*staged.error);
  }
  auto tick=scene_tick(s->scene,now());if(!tick)return R::failure(*tick.error);s->scene=*tick.value;std::optional<TextureIngress>incoming;
  if(s->staged_file)incoming=*s->staged_file;else if(!s->scene.startup_remaining){auto wire=s->receiver->acquire(s->scene);if(!wire){if(wire.error->code==EAGAIN||wire.error->code==EBUSY)return R::success(false);return R::failure(*wire.error);}incoming=*wire.value;}
  auto active=std::make_unique<State::Active>();auto begin=now();std::shared_ptr<PreparationLease>slot;{std::lock_guard<std::mutex>metrics(s->metrics_mutex);active->baseline=s->metrics;active->begin=begin;if(incoming&&incoming->input.request.client_id!="texture-file"){active->baseline=s->wire_baseline;active->begin=s->wire_begin?s->wire_begin:begin;}}
  if(incoming){active->input=incoming->input;active->texture_serial=incoming->serial;slot=incoming->preparation;}
  else{auto due=scene_frame_due(s->scene);if(!due)return R::failure(*due.error);if(!*due.value)return R::success(false);if(!s->next||s->next==UINT64_MAX)return R::failure(error(EOVERFLOW,"P8 frame serial exhausted"));active->input.request={s->ctx.boot_id,"p8-scene",std::to_string(s->session)+":frame:"+std::to_string(s->next)};auto reserve=rt->reserve_preparation(active->input.request,AppKind::P8);if(!reserve){if(reserve.error->code==EAGAIN||reserve.error->code==EBUSY)return R::success(false);return R::failure(*reserve.error);}slot=*reserve.value;auto cookie=s->router->allocate_cookie_namespace();if(!cookie)return R::failure(*cookie.error);if(*cookie.value>0x7fffffffu)return R::failure(error(EOVERFLOW,"P8 cookie range exhausted"));active->input.cookie_namespace=*cookie.value;
   // Disjoint from original texture display_base+serial (<2^31), never wraps.
   active->input.display_cookie=s->display_base+(uint64_t(*cookie.value)<<32);active->input.scene=s->scene;active->input.texture=s->current;
  }
  const bool numeric_reference=!s->ctx.fast_runtime||active->input.scene.startup_remaining!=0;
  if(incoming&&active->input.numeric_reference!=numeric_reference)return R::failure(error(ESTALE,"frozen texture numeric policy differs from trusted startup"));
  if(!incoming)active->input.numeric_reference=numeric_reference;
  auto prepared=s->library->prepare(active->input);if(!prepared)return R::failure(*prepared.error);active->prepared=*prepared.value;
  PreparationReceipt evidence;evidence.request=active->input.request;evidence.hardware=s->ctx.identity;evidence.model=prepared.value->work.model;evidence.plan=prepared.value->work.plan;evidence.input=prepared.value->work.input_sha;evidence.source=prepared.value->work.source->identity();evidence.entry="p8-scene";evidence.input_id=std::to_string(uint32_t(active->input.scene.object));evidence.begin_ns=begin;evidence.end_ns=now();auto saved=journal->save_preparation(evidence,s->ctx.scope);if(!saved)return R::failure(*saved.error);
  if(s->closing){if(incoming){auto cancelled=s->receiver->cancel(incoming->serial);if(!cancelled)return R::failure(*cancelled.error);s->staged_file.reset();}return R::success(false);}
  auto accepted=rt->submit(prepared.value->work,slot);if(!accepted)return R::failure(*accepted.error);s->active=std::move(active);if(incoming){auto bound=s->receiver->commit(incoming->serial,prepared.value->owner,*accepted.value);if(!bound)return R::failure(*bound.error);if(s->file&&s->file->serial==s->active->texture_serial)s->file->phase=TexturePhase::QUEUED;s->staged_file.reset();}else ++s->next;
  s->active->prepare_ns=now()-begin;{std::lock_guard<std::mutex>metrics(s->metrics_mutex);s->metrics.prepare_ns+=s->active->prepare_ns;}return R::success(true);
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"P8 worker metadata; backing retained"));}catch(const std::exception&e){return R::failure({EIO,"NATIVE_P8_SERVICE",e.what()});}
}
Result<void>NativeP8Service::control(char command){
 auto s=state_;{std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return Result<void>::failure(error(EAGAIN,"scene worker busy"));if(s->closing||s->active||s->staged_file||pending_file(s->file))return Result<void>::failure(error(EBUSY,"frame or texture transaction pending"));auto next=scene_control(s->scene,command,now());if(!next)return Result<void>::failure(*next.error);s->scene=*next.value;}
 return s->ctx.notify_work?s->ctx.notify_work():Result<void>::success();
}
Result<void>NativeP8Service::stage_file(std::shared_ptr<const TexturePackage>bank,std::shared_ptr<HostMemoryLease>charge){
 auto s=state_;{std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return Result<void>::failure(error(EAGAIN,"scene worker busy"));if(s->closing||s->active||s->staged_file||pending_file(s->file))return Result<void>::failure(error(EBUSY,"pending scene or texture"));auto staged=s->receiver->stage_native(std::move(bank),s->scene,std::move(charge));if(!staged)return Result<void>::failure(*staged.error);s->staged_file=*staged.value;}
 return s->ctx.notify_work?s->ctx.notify_work():Result<void>::success();
}
Result<std::string>NativeP8Service::accept_mosaic_file(const RegisteredTextureFile&spec,const RequestKey&producer){
 using R=Result<std::string>;try{
  auto s=state_;auto rt=s->ctx.runtime.lock();if(!rt||!s8_joint_platform(s->ctx.identity)||producer.boot_id!=s->ctx.boot_id)return R::failure(error(EXDEV,"same joint boot and original Mosaic producer required"));
  {std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return R::failure(error(EAGAIN,"scene worker busy"));
   if(s->file&&same_request(s->file->command,producer)){if(s->file->id!=spec.id||s->file->bank->source_result_sha()!=spec.result_sha||s->file->bank->identity()!=spec.policy.expected_package_id)return R::failure(error(ESTALE,"original bridge producer cannot change package"));return R::success(file_json(*s->file,false));}
   if(s->closing||s->released||s->active||s->staged_file||pending_file(s->file)||s->scene.startup_remaining||!rt->mode_allows(AppKind::P8))return R::failure(error(EBUSY,"complete original P8 startup/frame and mode handoff first"));
  }
  auto charge=rt->reserve_host_memory(128ULL<<20);if(!charge)return R::failure(*charge.error);const auto begin=now();
  auto sealed=read_sealed_native_asset(spec.result_path,262144);if(!sealed)return R::failure(*sealed.error);if(vxnn::content_digest(*sealed.value)!=spec.result_sha)return R::failure(error(ESTALE,"original live Mosaic manifest SHA differs"));const auto sealed_ns=now();
  auto source=import_result(spec.result_path);if(!source)return R::failure(*source.error);if(source.value->original_json!=*sealed.value||!same_request(source.value->lease.producer,producer)||source.value->lease.scope!=s->ctx.scope||source.value->lease.hardware!=s->ctx.identity.hardware_contract)return R::failure(error(EXDEV,"package source is not this boot's completed original Mosaic output"));
  namespace j=vxnn::json;auto meta=j::parse(*sealed.value);auto*origin=j::member(meta.get(),"native_origin");
  if(j::u32(j::member(origin,"provider_flags"))!=4||j::u64(j::member(origin,"device_generation"))!=s->ctx.identity.device_generation||j::digest(j::member(origin,"bit"))!=s->ctx.identity.bit||j::digest(j::member(origin,"xsa"))!=s->ctx.identity.xsa||j::digest(j::member(origin,"compatible"))!=s->ctx.identity.compatible)return R::failure(error(EXDEV,"complete original joint hardware identity required"));
  const auto imported_ns=now();
  auto bank=make_texture(*source.value,spec.policy);if(!bank)return R::failure(*bank.error);if((*bank.value)->source_kind()!=TextureOrigin::MOSAIC_RESULT||(*bank.value)->source_result_sha()!=spec.result_sha||(*bank.value)->source_scope()!=s->ctx.scope||(*bank.value)->cpu_bytes()>(128ULL<<20))return R::failure(error(EXDEV,"verified original dual-LOD Mosaic bank required"));
  auto transaction=std::make_shared<FileTransaction>();transaction->command=producer;transaction->id=spec.id;transaction->bank=*bank.value;transaction->charge=*charge.value;transaction->begin_ns=begin;const auto prepared_ns=now();transaction->prepare_ns=prepared_ns-begin;transaction->seal_ns=sealed_ns-begin;transaction->source_ns=imported_ns-sealed_ns;transaction->package_ns=prepared_ns-imported_ns;transaction->clock_domain=s->ctx.scope==vxnn::EvidenceScope::REAL_BOARD?"LINUX_MONOTONIC":"HOST_MONOTONIC";
  std::string reply;
  {std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return R::failure(error(EAGAIN,"scene worker busy after verified file import"));if(s->closing||s->released||s->active||s->staged_file||pending_file(s->file)||s->scene.startup_remaining)return R::failure(error(EBUSY,"original frame/file must retire before bridge staging"));
   // This reservation atomically validates mode/app/generation. It keeps a
   // later mode switch pending until the accepted complete frame retires.
   auto staged=s->receiver->stage_native(transaction->bank,s->scene,transaction->charge);if(!staged)return R::failure(*staged.error);transaction->serial=staged.value->serial;s->staged_file=*staged.value;s->file=transaction;reply=file_json(*transaction,true);
  }
  auto wake=s->ctx.notify_work?s->ctx.notify_work():Result<void>::failure(error(ESHUTDOWN,"common worker notification expired"));if(!wake)return R::failure(*wake.error);return R::success(std::move(reply));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded live bridge allocation"));}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_P8_SERVICE",e.what()});}
}
Result<std::string>NativeP8Service::mosaic_file_status(const RequestKey&producer)const{
 auto s=state_;std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return Result<std::string>::failure(error(EAGAIN,"scene worker busy"));if(!s8_joint_platform(s->ctx.identity)||s->released||!s->file||!same_request(s->file->command,producer))return Result<std::string>::failure(error(ENOENT,"original live bridge transaction not retained"));
 try{return Result<std::string>::success(file_json(*s->file,false));}catch(const std::bad_alloc&){return Result<std::string>::failure(error(ENOMEM,"bounded bridge status metadata"));}
}
Result<NativeP8ServiceSnapshot>NativeP8Service::snapshot()const{auto s=state_;std::unique_lock<std::mutex>lock(s->work_mutex,std::try_to_lock);if(!lock.owns_lock())return Result<NativeP8ServiceSnapshot>::failure(error(EAGAIN,"scene worker busy"));if(s->released)return Result<NativeP8ServiceSnapshot>::failure(error(EBADF,"scene service released"));std::lock_guard<std::mutex>metrics(s->metrics_mutex);return Result<NativeP8ServiceSnapshot>::success({s->scene,s->metrics,bool(s->active),s->startup_selftest_frames});}
Result<void>NativeP8Service::quiesce(){auto s=state_;s->closing=true;{std::lock_guard<std::mutex>lock(s->io_mutex);s->socket.close();}{std::lock_guard<std::mutex>lock(s->work_mutex);if(s->staged_file&&!s->active){auto cancelled=s->receiver->cancel(s->staged_file->serial);if(!cancelled)return cancelled;s->staged_file.reset();}}return Result<void>::success();}
Result<void>NativeP8Service::release(){
 auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->released)return Result<void>::success();if(!s->closing)return Result<void>::failure(error(EBUSY,"P8 must quiesce before release"));
 if(s->active){auto receipt=s->active->prepared.owner->receipt_snapshot();if(!receipt)return Result<void>::failure(*receipt.error);if(!receipt.value->retired||receipt.value->first_error||receipt.value->evidence_error)return Result<void>::failure(error(EBUSY,"original P8 retirement unproven"));if(receipt.value->published){auto done=s->finish(true);if(!done)return done;}else{if(s->active->texture_serial){auto cancelled=s->receiver->cancel(*s->active->texture_serial);if(!cancelled)return cancelled;}s->active.reset();}}
 auto closed=s->receiver->close();if(!closed)return closed;auto resources=s->library->release_session();if(!resources)return resources;s->receiver.reset();s->current.reset();s->file.reset();s->library.reset();s->control_resident.reset();s->released=true;return Result<void>::success();
}
uint16_t NativeP8Service::port()const{return state_->port;}
}

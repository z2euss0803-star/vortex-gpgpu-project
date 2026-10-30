#include <vxunified/texture_service.hpp>
#include <vxunified/exit_retention.hpp>
#include <vxunified/native.hpp>
#include <mutex>
#include <tuple>
extern "C" {
#include "vendor/p8/network/texture_rx.h"
uint32_t vx_p8_rx_state(void);uint32_t vx_p8_rx_tx(void);uint32_t vx_p8_rx_ip(void);uint16_t vx_p8_rx_port(void);
}
namespace vxunified {namespace {
std::mutex receiver_mutex;void*receiver_owner=nullptr;
vxnn::Error error(int c,const char*s){return {c,"TEXTURE_SERVICE",s};}
Digest literal(const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[2*i])<<4)|n(s[2*i+1]);}return d;}
bool hardware(const BackendIdentity&hw){return s8_joint_platform(hw)||(hw.provider_flags==2&&hw.device_generation&&hw.hardware_contract==literal("22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012")&&hw.bit==literal("faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc")&&hw.xsa==literal("08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536")&&hw.compatible==vxnn::content_digest("vortex,w4-graphics-p8-v1"));}
}
struct TextureService::Impl {
 BackendIdentity hw;std::string boot;uint32_t session=0,cookie_base=0;uint64_t display_base=0,next=1;vxnn::EvidenceScope scope;
 bool native=false,fast_runtime=false,acquiring=false;std::weak_ptr<Runtime>runtime;std::shared_ptr<NativeRouter>router;std::shared_ptr<HostMemoryLease>staging_charge;
 std::array<std::vector<uint8_t>,2>staging;TextureStatus state;std::optional<TextureIngress>pending;
 std::shared_ptr<P8WorkRequest>bound;std::shared_ptr<const TexturePackage>current;bool closed=false,wire=false,quarantined=false;
 std::optional<tex_ready>native_ready;uint32_t native_ip=0;uint16_t native_port=0;
 std::optional<SceneState>last_applied;detail::ExitRetention retention;
 Result<TextureIngress>stage_locked(std::shared_ptr<const TexturePackage>t,const SceneState&scene,bool from_wire){
  using R=Result<TextureIngress>;
  if(closed||receiver_owner!=this){return R::failure(error(EBADF,"receiver closed"));}
  if(quarantined||pending||(!from_wire&&(vx_p8_rx_state()==TEX_RECEIVING||vx_p8_rx_state()==TEX_READY))){return R::failure(error(EBUSY,"pending or receiving texture"));}
  if(!t||!nonzero_digest(t->identity())){return R::failure(error(EINVAL,"admitted immutable texture required"));}
  auto rendered=scene_render_state(scene,true);if(!rendered){return R::failure(*rendered.error);}
  if(next>0x7fffffffu-cookie_base||next>UINT64_MAX-display_base){return R::failure(error(EOVERFLOW,"texture request namespace exhausted"));}
  TextureIngress in;in.serial=next;in.input.request={boot,from_wire?std::to_string(vx_p8_rx_ip())+":"+std::to_string(vx_p8_rx_port()):"texture-file",std::to_string(session)+":"+std::to_string(next)};
  in.input.scene=scene;in.input.scene.theme=t->identity();in.input.texture=std::move(t);in.input.texture_redraw=true;in.input.cookie_namespace=cookie_base+uint32_t(next);in.input.display_cookie=display_base+next;
  pending=in;next++;wire=from_wire;state.serial=in.serial;state.wire_tx=from_wire?vx_p8_rx_tx():0;state.pending=in.input.texture->identity();state.phase=TexturePhase::STAGED;return R::success(std::move(in));
 }
 void wire_state(){if(pending||quarantined){return;}auto value=vx_p8_rx_state();if(value==TEX_RECEIVING){state.phase=TexturePhase::RECEIVING;}else if(value==TEX_READY){state.phase=TexturePhase::STAGED;}else if(value==TEX_FAILED&&state.phase!=TexturePhase::CANCELLED){state.phase=TexturePhase::FAILED;}}
 void close_quiet(std::optional<TextureIngress>&released){if(pending&&wire){tex_rx_failed(state.wire_tx,TEX_ERR_CANCELLED);}if(native_ready){tex_rx_failed(native_ready->tx_id,TEX_ERR_CANCELLED);native_ready.reset();}released=std::move(pending);pending.reset();tex_rx_init(nullptr,nullptr,0);receiver_owner=nullptr;closed=true;}
};
TextureService::TextureService(std::shared_ptr<Impl>p):impl_(std::move(p)){}
TextureService::~TextureService(){std::optional<TextureIngress>released;if(impl_){std::lock_guard<std::mutex>lock(receiver_mutex);if(receiver_owner==impl_.get()){if(impl_->quarantined||impl_->bound||impl_->acquiring||(impl_->native&&impl_->pending)){impl_->retention.retain(impl_);}else{impl_->close_quiet(released);}}}}
uint32_t texture_wire_phase(TexturePhase p){switch(p){case TexturePhase::IDLE:return TEX_IDLE;case TexturePhase::RECEIVING:return TEX_RECEIVING;case TexturePhase::STAGED:case TexturePhase::QUEUED:return TEX_READY;case TexturePhase::APPLIED:return TEX_APPLIED;case TexturePhase::FAILED:case TexturePhase::CANCELLED:return TEX_FAILED;}return TEX_FAILED;}
Result<std::shared_ptr<TextureService>>TextureService::create(const BackendIdentity&hw,const std::string&boot,uint32_t session,uint32_t cookie,uint64_t display,vxnn::EvidenceScope scope){
 return create_bound(hw,boot,session,cookie,display,scope,nullptr,{});
}
Result<std::shared_ptr<TextureService>>TextureService::create_native(const StartupContext&c,std::shared_ptr<NativeRouter>router,uint32_t session,uint64_t display){
 auto admitted=admit_native_startup(c,2);if(!admitted)return Result<std::shared_ptr<TextureService>>::failure(*admitted.error);auto rt=c.runtime.lock();if(!router||!router->owns_runtime(rt.get()))return Result<std::shared_ptr<TextureService>>::failure(error(EXDEV,"same common native router required"));
 return create_bound(c.identity,c.boot_id,session,0,display,c.scope,&c,std::move(router));
}
Result<std::shared_ptr<TextureService>>TextureService::create_bound(const BackendIdentity&hw,const std::string&boot,uint32_t session,uint32_t cookie,uint64_t display,vxnn::EvidenceScope scope,const StartupContext*native,std::shared_ptr<NativeRouter>router){
 using R=Result<std::shared_ptr<TextureService>>;
 if(!hardware(hw)||boot.empty()||boot.size()>128||!std::all_of(boot.begin(),boot.end(),[](unsigned char c){return c>=33&&c<127;})||!session||(!cookie&&!native)||cookie>=0x7fffffffu||!display||display==UINT64_MAX||(scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD)||(scope==vxnn::EvidenceScope::REAL_BOARD&&!hw.board_identity_verified&&!native)){return R::failure(error(EXDEV,"exact W4 hardware epoch boot and scope required"));}
 try{
  // Complete allocation outside the global lock: a failed shared control-block
  // allocation may destroy the uninstalled service and must not lock recursively.
  auto p=std::make_shared<Impl>();p->hw=hw;p->boot=boot;p->session=session;p->cookie_base=cookie;p->display_base=display;p->scope=scope;
  if(native){auto rt=native->runtime.lock();if(!rt)return R::failure(error(ESHUTDOWN,"native Runtime expired"));auto charge=rt->reserve_host_memory(staging_bytes);if(!charge)return R::failure(*charge.error);p->staging_charge=*charge.value;p->runtime=rt;p->router=std::move(router);p->native=true;p->fast_runtime=native->fast_runtime;}
  p->staging[0].resize(TEX_SMALL_BYTES);p->staging[1].resize(TEX_LARGE_BYTES);auto service=std::shared_ptr<TextureService>(new TextureService(p));
  {std::lock_guard<std::mutex>lock(receiver_mutex);if(receiver_owner){return R::failure(error(EBUSY,"sole original C receiver already owned"));}tex_rx_init(p->staging[0].data(),p->staging[1].data(),session);receiver_owner=p.get();}
  return R::success(std::move(service));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded receiver allocation"));}
}
Result<std::vector<uint8_t>>TextureService::handle(vxnn::Bytes bytes,TexturePeer peer,uint64_t now){
 using R=Result<std::vector<uint8_t>>;auto&s=*impl_;std::lock_guard<std::mutex>lock(receiver_mutex);
 if(s.closed||receiver_owner!=&s){return R::failure(error(EBADF,"receiver closed"));}
 if(bytes.size>TEX_PACKET_MAX||(!bytes.data&&bytes.size)){return R::failure(error(EINVAL,"bounded packet"));}
 if(bytes.size>=6&&(((s.pending||s.acquiring)&&bytes.data[5]==TEX_BEGIN)||(s.quarantined&&bytes.data[5]!=TEX_STATUS&&bytes.data[5]!=TEX_HELLO))){return R::success({});}
 uint8_t reply[TEX_PACKET_MAX];const size_t n=tex_rx_handle(bytes.data,bytes.size,peer.address,peer.port,now,reply,sizeof(reply));s.wire_state();
 try{return R::success({reply,reply+n});}catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded reply"));}
}
Result<void>TextureService::poll(uint64_t now){auto&s=*impl_;std::lock_guard<std::mutex>lock(receiver_mutex);if(s.closed||receiver_owner!=&s){return Result<void>::failure(error(EBADF,"receiver closed"));}tex_rx_poll(now);s.wire_state();return Result<void>::success();}
Result<TextureIngress>TextureService::stage(std::shared_ptr<const TexturePackage>t,const SceneState&scene){auto&s=*impl_;std::lock_guard<std::mutex>lock(receiver_mutex);if(s.native)return Result<TextureIngress>::failure(error(EPERM,"native file stage requires shared memory charge"));try{return s.stage_locked(std::move(t),scene,false);}catch(const std::bad_alloc&){return Result<TextureIngress>::failure(error(ENOMEM,"bounded file stage"));}}
Result<TextureIngress>TextureService::stage_native(std::shared_ptr<const TexturePackage>bank,const SceneState&scene,std::shared_ptr<HostMemoryLease>charge){
 using R=Result<TextureIngress>;auto&s=*impl_;auto rt=s.runtime.lock();if(!s.native||!rt||!bank||!nonzero_digest(bank->identity())||!rt->owns_host_memory(charge)||charge->bytes()<bank->cpu_bytes())return R::failure(error(EXDEV,"native bank must retain common CPU charge acquired before construction"));
 auto rendered=scene_render_state(scene,true);if(!rendered)return R::failure(*rendered.error);uint64_t serial=0;
 {std::lock_guard<std::mutex>lock(receiver_mutex);if(s.closed||receiver_owner!=&s)return R::failure(error(EBADF,"receiver closed"));if(s.quarantined||s.pending||s.acquiring||vx_p8_rx_state()==TEX_RECEIVING||vx_p8_rx_state()==TEX_READY)return R::failure(error(EBUSY,"one pending native texture"));if(!s.next||s.next>UINT64_MAX-s.display_base)return R::failure(error(EOVERFLOW,"native texture serial exhausted"));serial=s.next;s.acquiring=true;}
 struct Window{Impl&s;~Window(){std::lock_guard<std::mutex>lock(receiver_mutex);s.acquiring=false;}}window{s};
 struct ChargedBank{std::shared_ptr<HostMemoryLease>charge;std::shared_ptr<const TexturePackage>bank;};
 try{TextureIngress in;in.serial=serial;in.input.request={s.boot,"texture-file",std::to_string(s.session)+":"+std::to_string(serial)};auto slot=rt->reserve_preparation(in.input.request,AppKind::P8);if(!slot)return R::failure(*slot.error);in.preparation=*slot.value;auto cookie=s.router->allocate_cookie_namespace();if(!cookie)return R::failure(*cookie.error);if(*cookie.value>0x7fffffffu)return R::failure(error(EOVERFLOW,"original P8 cookie range exhausted"));
  auto owner=std::make_shared<ChargedBank>();owner->charge=charge;owner->bank=std::move(bank);in.input.texture=std::shared_ptr<const TexturePackage>(owner,owner->bank.get());in.input.scene=scene;in.input.numeric_reference=!s.fast_runtime||scene.startup_remaining!=0;in.input.scene.theme=in.input.texture->identity();in.input.texture_redraw=true;in.input.cookie_namespace=*cookie.value;in.input.display_cookie=s.display_base+serial;
  {std::lock_guard<std::mutex>lock(receiver_mutex);if(s.closed||receiver_owner!=&s||s.quarantined||s.pending||s.next!=serial)return R::failure(error(ESTALE,"native texture admission changed"));s.pending=in;++s.next;s.wire=false;s.state.serial=serial;s.state.wire_tx=0;s.state.pending=in.input.texture->identity();s.state.phase=TexturePhase::STAGED;}
  return R::success(std::move(in));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"native texture metadata allocation"));}
}
Result<std::optional<TextureIngress>>TextureService::acquire(const SceneState&scene){
 using R=Result<std::optional<TextureIngress>>;auto&s=*impl_;
 if(s.native){
  auto rt=s.runtime.lock();if(!rt)return R::failure(error(ESHUTDOWN,"native Runtime expired"));
  uint64_t serial=0;tex_ready ready{};uint32_t peer_ip=0;uint16_t peer_port=0;
  {std::lock_guard<std::mutex>lock(receiver_mutex);
   if(s.closed||receiver_owner!=&s)return R::failure(error(EBADF,"receiver closed"));
   if(s.quarantined)return R::failure(error(EBUSY,"first error retained"));
   if(s.pending||s.acquiring)return R::success(std::nullopt);
   if(!s.native_ready){
    if(!tex_rx_get_ready(&ready))return R::success(std::nullopt);
    auto valid=scene_render_state(scene,true);if(!valid)return R::failure(*valid.error);
    if(!tex_rx_acquire_ready(&ready))return R::failure(error(EPROTO,"original ready pin changed"));
    s.native_ready=ready;s.native_ip=vx_p8_rx_ip();s.native_port=vx_p8_rx_port();
   }
   if(!s.next||s.next>UINT64_MAX-s.display_base)return R::failure(error(EOVERFLOW,"native texture serial exhausted"));
   ready=*s.native_ready;peer_ip=s.native_ip;peer_port=s.native_port;serial=s.next;s.acquiring=true;
  }
  // Original C held pin makes staging immutable across metadata preparation.
  // No Runtime budget/cookie lock is taken while receiver_mutex is held.
  struct Window{Impl&s;~Window(){std::lock_guard<std::mutex>lock(receiver_mutex);s.acquiring=false;}}window{s};
  struct ChargedBank{std::shared_ptr<HostMemoryLease>charge;std::shared_ptr<const TexturePackage>bank;};
  try{
   TextureIngress in;in.serial=serial;in.input.request={s.boot,std::to_string(peer_ip)+":"+std::to_string(peer_port),std::to_string(s.session)+":"+std::to_string(serial)};
   auto slot=rt->reserve_preparation(in.input.request,AppKind::P8);if(!slot)return R::failure(*slot.error);in.preparation=*slot.value;
   auto charge=rt->reserve_host_memory(staging_bytes);if(!charge)return R::failure(*charge.error);
   auto bank=admit_uploaded_texture({s.staging[0].data(),s.staging[0].size()},{s.staging[1].data(),s.staging[1].size()});if(!bank)return R::failure(*bank.error);
   auto owner=std::make_shared<ChargedBank>();owner->charge=*charge.value;owner->bank=*bank.value;in.input.texture=std::shared_ptr<const TexturePackage>(owner,owner->bank.get());
   auto cookie=s.router->allocate_cookie_namespace();if(!cookie)return R::failure(*cookie.error);if(*cookie.value>0x7fffffffu)return R::failure(error(EOVERFLOW,"original P8 cookie range exhausted"));
   in.input.scene=scene;in.input.numeric_reference=!s.fast_runtime||scene.startup_remaining!=0;in.input.scene.theme=in.input.texture->identity();in.input.texture_redraw=true;in.input.cookie_namespace=*cookie.value;in.input.display_cookie=s.display_base+serial;
   {std::lock_guard<std::mutex>lock(receiver_mutex);
    if(s.closed||receiver_owner!=&s||s.quarantined||s.pending||s.next!=serial||!s.native_ready||s.native_ready->tx_id!=ready.tx_id||vx_p8_rx_state()!=TEX_READY||vx_p8_rx_tx()!=ready.tx_id)return R::failure(error(ESTALE,"native pinned wire admission changed"));
    s.pending=in;++s.next;s.wire=true;s.state.serial=serial;s.state.wire_tx=ready.tx_id;s.state.pending=in.input.texture->identity();s.state.phase=TexturePhase::STAGED;s.native_ready.reset();
   }
   return R::success(std::move(in));
  }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"native pinned wire metadata allocation"));}
 }
 std::lock_guard<std::mutex>lock(receiver_mutex);
 if(s.closed||receiver_owner!=&s){return R::failure(error(EBADF,"receiver closed"));}if(s.quarantined){return R::failure(error(EBUSY,"first error retained"));}if(s.pending){return R::success(std::nullopt);}
 tex_ready ready{};if(!tex_rx_get_ready(&ready)){return R::success(std::nullopt);}auto valid=scene_render_state(scene,true);if(!valid){return R::failure(*valid.error);}
 if(!tex_rx_acquire_ready(&ready)){return R::failure(error(EPROTO,"ready pin changed"));}
 try{
  auto bank=admit_uploaded_texture({s.staging[0].data(),s.staging[0].size()},{s.staging[1].data(),s.staging[1].size()});if(!bank){s.state.first_error=*bank.error;s.quarantined=true;s.state.phase=TexturePhase::FAILED;tex_rx_failed(ready.tx_id,TEX_ERR_CRC);return R::failure(*bank.error);}
  auto staged=s.stage_locked(*bank.value,scene,true);if(!staged){s.quarantined=true;s.state.first_error=*staged.error;s.state.phase=TexturePhase::FAILED;tex_rx_failed(ready.tx_id,TEX_ERR_CANCELLED);return R::failure(*staged.error);}return R::success(*staged.value);
 }catch(const std::bad_alloc&){s.quarantined=true;s.state.phase=TexturePhase::FAILED;s.state.first_error=error(ENOMEM,"pinned ingress allocation");tex_rx_failed(ready.tx_id,TEX_ERR_CANCELLED);return R::failure(*s.state.first_error);}
}
// Typed production binding and original retirement only; packets supply none of these receipts.
Result<void>TextureService::commit(uint64_t serial,std::shared_ptr<P8WorkRequest>app,const RequestKey&accepted){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(receiver_mutex);
 if(s.closed||receiver_owner!=&s){return Result<void>::failure(error(EBADF,"receiver closed"));}
 if(s.quarantined||!s.pending||s.pending->serial!=serial||s.bound||!app){return Result<void>::failure(error(EBUSY,"exact unsubmitted pending texture required"));}
 const auto&q=s.pending->input.request;if(q.boot_id!=accepted.boot_id||q.client_id!=accepted.client_id||q.request_id!=accepted.request_id||!app->matches_texture_request(s.pending->input,s.hw,s.scope)){return Result<void>::failure(error(ESTALE,"original admitted request hardware scene and texture required"));}
 s.bound=std::move(app);s.state.phase=TexturePhase::QUEUED;return Result<void>::success();
}
Result<SceneState>TextureService::complete(uint64_t serial){
 using R=Result<SceneState>;auto&s=*impl_;std::shared_ptr<P8WorkRequest>released_owner;std::shared_ptr<const TexturePackage>released_previous;std::optional<TextureIngress>released_pending;std::lock_guard<std::mutex>lock(receiver_mutex);
 if(s.closed||receiver_owner!=&s){return R::failure(error(EBADF,"receiver closed"));}
 if(!s.pending&&s.state.phase==TexturePhase::APPLIED&&s.state.serial==serial&&s.last_applied){return R::success(*s.last_applied);}
 if(!s.pending||s.pending->serial!=serial||!s.bound){return R::failure(error(EBUSY,"original pending execution required"));}
 auto failed=[&](vxnn::Error e){if(!s.state.first_error){s.state.first_error=std::move(e);}s.quarantined=true;s.state.phase=TexturePhase::FAILED;if(s.wire){tex_rx_failed(s.state.wire_tx,TEX_ERR_CANCELLED);}return R::failure(*s.state.first_error);};
 if(s.quarantined){return R::failure(s.state.first_error?*s.state.first_error:error(EBUSY,"first error retained"));}
 auto saved=s.bound->receipt_snapshot();if(!saved){return failed(*saved.error);}const auto&r=*saved.value;
 if(r.first_error){return failed(*r.first_error);}if(r.evidence_error){return failed(*r.evidence_error);}
 if(!r.published||!r.retired){return R::failure(error(EBUSY,"original GPU and display retirement required"));}
 if(r.completed_gpu!=2||!r.output_crc_valid||!r.digest_ns||!r.display||!r.next_scene){return failed(error(ENODATA,"retired original frame proof incomplete"));}
 if((s.scope==vxnn::EvidenceScope::REAL_BOARD&&(!r.gpu_raw_cycles||!*r.gpu_raw_cycles))||(s.scope!=vxnn::EvidenceScope::REAL_BOARD&&r.gpu_raw_cycles)){return failed(error(EPROTO,"renderer physical cycle scope differs"));}
 const auto&input=s.pending->input;const auto&d=*r.display;auto clock=s.scope==vxnn::EvidenceScope::REAL_BOARD?vxnn::ClockDomain::LINUX_MONOTONIC:vxnn::ClockDomain::HOST_MONOTONIC;
 if(d.cookie!=input.display_cookie||d.result||!d.presented||d.clock_domain!=clock||!d.submitted_ns||!d.presented_ns||*d.presented_ns<*d.submitted_ns||(d.had_old_front&&(!d.old_retired||!d.old_retired_ns||*d.old_retired_ns<*d.presented_ns))){return failed(error(EPROTO,"original presentation and old front retirement required"));}
 auto expected=scene_after_frame(input.scene,input.scene.displayed_pose,true,*d.presented_ns);if(!expected){return failed(*expected.error);}
 auto scene=[](const SceneState&x){return std::tie(x.object,x.theme,x.pose,x.view,x.remaining,x.startup_remaining,x.displayed_pose,x.completed,x.revision,x.gallery_due_ns,x.paused,x.looping,x.gallery,x.automatic,x.have_displayed);};
 if(scene(*r.next_scene)!=scene(*expected.value)||r.next_scene->theme!=input.texture->identity()){return failed(error(ESTALE,"redraw scene changed before retirement"));}
 if(s.wire){if(vx_p8_rx_state()!=TEX_READY||vx_p8_rx_tx()!=s.state.wire_tx){return failed(error(ESTALE,"original pinned wire transaction changed"));}tex_rx_applied(s.state.wire_tx,r.rgba_crc,r.depth_crc,uint32_t(input.scene.object),input.scene.displayed_pose,uint32_t(r.completed_gpu));if(vx_p8_rx_state()!=TEX_APPLIED){return failed(error(EPROTO,"wire applied transition refused"));}}
 s.last_applied=*r.next_scene;released_previous=std::move(s.current);s.current=input.texture;s.state.current=input.texture->identity();s.state.pending={};s.state.phase=TexturePhase::APPLIED;released_owner=std::move(s.bound);released_pending=std::move(s.pending);s.pending.reset();return R::success(*s.last_applied);
}
Result<void>TextureService::cancel(uint64_t serial){auto&s=*impl_;std::shared_ptr<P8WorkRequest>released_owner;std::optional<TextureIngress>released_pending;std::lock_guard<std::mutex>lock(receiver_mutex);if(s.closed||receiver_owner!=&s){return Result<void>::failure(error(EBADF,"receiver closed"));}if(s.quarantined||!s.pending||s.pending->serial!=serial){return Result<void>::failure(error(EBUSY,"exact pending texture required"));}if(s.bound){auto receipt=s.bound->receipt_snapshot();if(!receipt){return Result<void>::failure(*receipt.error);}if(!receipt.value->retired||receipt.value->published||receipt.value->first_error||receipt.value->evidence_error){return Result<void>::failure(error(EBUSY,"original cancelled owner retirement required"));}released_owner=std::move(s.bound);}if(s.wire){tex_rx_failed(s.state.wire_tx,TEX_ERR_CANCELLED);}released_pending=std::move(s.pending);s.pending.reset();s.state.pending={};s.state.phase=TexturePhase::CANCELLED;return Result<void>::success();}
Result<TextureStatus>TextureService::snapshot()const{auto&s=*impl_;std::lock_guard<std::mutex>lock(receiver_mutex);try{return Result<TextureStatus>::success(s.state);}catch(const std::bad_alloc&){return Result<TextureStatus>::failure(error(ENOMEM,"bounded snapshot"));}}
Result<void>TextureService::close(){auto&s=*impl_;std::optional<TextureIngress>released;std::lock_guard<std::mutex>lock(receiver_mutex);if(s.closed){return Result<void>::success();}if(receiver_owner!=&s){return Result<void>::failure(error(ESTALE,"receiver owner changed"));}if(s.quarantined||s.bound||s.acquiring||(s.native&&s.pending)){return Result<void>::failure(error(EBUSY,"uncertain execution or first evidence retained"));}s.close_quiet(released);return Result<void>::success();}
}

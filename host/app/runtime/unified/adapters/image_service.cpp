#include <vxunified/image_network.hpp>
#include <vxunified/native_admission.hpp>
#include <vxunified/exit_retention.hpp>
#include "image/network/image_rx_linux.h"
#include <mutex>
#include <vxl/hunet.h>
namespace vxunified {
namespace {
vxnn::Error neterr(int n,const char*s){return {n,"IMAGE_NETWORK",s};
 }
bool samekey(const RequestKey&a,const RequestKey&b){return a.boot_id==b.boot_id&&a.client_id==b.client_id&&a.request_id==b.request_id;
 }
bool samehw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;
 }
bool samedp(const DisplayReceipt&a,const DisplayReceipt&b){return a.cookie==b.cookie&&a.result==b.result&&a.clock_domain==b.clock_domain&&a.presented==b.presented&&a.had_old_front==b.had_old_front&&a.old_retired==b.old_retired&&a.submitted_ns==b.submitted_ns&&a.presented_ns==b.presented_ns&&a.old_retired_ns==b.old_retired_ns;
 }
}
struct ImageReceiver::Impl {
 detail::ExitRetention exit_retention;
 std::mutex mutex;
 vx_image_receiver*rx=nullptr;
 BackendIdentity hardware;
 std::string boot;
 vxnn::EvidenceScope scope;
 bool native=false,acquiring=false;
 std::weak_ptr<Runtime>runtime;
 std::shared_ptr<HostMemoryLease>persistent;
 std::optional<vx_image_ingress>unprepared;

 std::optional<ImageIngress>pending;
 std::shared_ptr<ImageWorkRequest>bound;
 uint32_t session=0,jobs_before=0;
 image_request wire{};

};

ImageReceiver::ImageReceiver(std::shared_ptr<Impl>p):impl_(std::move(p)){}
// A pinned/failed request remains retained by the C receiver until explicit
// service recovery;
// Destruction never fabricates completion or touches GPU.
ImageReceiver::~ImageReceiver(){try{if(close())return;}catch(...){/* Retention itself never allocates. */}impl_->exit_retention.retain(impl_);}
Result<std::shared_ptr<ImageReceiver>>ImageReceiver::create(const BackendIdentity&hw,const std::string&boot,uint32_t session,const std::string&bundle,uint64_t base,vxnn::EvidenceScope scope){
 return create_bound(hw,boot,session,bundle,base,scope,nullptr);
}
Result<std::shared_ptr<ImageReceiver>>ImageReceiver::create_native(const StartupContext&c,uint32_t session,const std::string&bundle,uint64_t base){
 auto admitted=admit_native_startup(c,1);if(!admitted)return Result<std::shared_ptr<ImageReceiver>>::failure(*admitted.error);
 return create_bound(c.identity,c.boot_id,session,bundle,base,c.scope,&c);
}
Result<std::shared_ptr<ImageReceiver>>ImageReceiver::create_bound(const BackendIdentity&hw,const std::string&boot,uint32_t session,const std::string&bundle,uint64_t base,vxnn::EvidenceScope scope,const StartupContext*native){
 using R=Result<std::shared_ptr<ImageReceiver>>;

 if(boot.empty()||boot.size()>128||(!s8_joint_platform(hw)&&(hw.provider_flags!=1||hw.compatible!=vxnn::content_digest("vortex,hw4-s8-demo-v1")))||!hw.device_generation||!nonzero_digest(hw.hardware_contract)||!nonzero_digest(hw.bit)||!nonzero_digest(hw.xsa)||(scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&scope!=vxnn::EvidenceScope::REAL_BOARD)||(scope==vxnn::EvidenceScope::REAL_BOARD&&!hw.board_identity_verified&&!native))return R::failure(neterr(EXDEV,"bound S8 hardware and evidence scope required"));

 try{auto p=std::make_shared<Impl>();
 p->hardware=hw;
 p->boot=boot;
 p->session=session;
 p->scope=scope;
 if(native){auto rt=native->runtime.lock();if(!rt)return R::failure(neterr(ESHUTDOWN,"native Runtime is gone"));auto charge=rt->reserve_host_memory(persistent_bytes);if(!charge)return R::failure(*charge.error);p->persistent=*charge.value;p->runtime=rt;p->native=true;}
 // Finish every C++ allocation before installing the sole C receiver.
 auto result=std::shared_ptr<ImageReceiver>(new ImageReceiver(std::move(p)));
 int rc=vx_image_receiver_create(session,bundle.c_str(),base,scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,&result->impl_->rx);
 if(rc)return R::failure(neterr(-rc,"sole original receiver create"));
 return R::success(result);
 }catch(const std::bad_alloc&){return R::failure(neterr(ENOMEM,"bounded receiver allocation"));
 }
}
Result<std::vector<uint8_t>>ImageReceiver::handle(vxnn::Bytes bytes,ImagePeer peer,uint64_t now){
 auto&s=*impl_;
 std::lock_guard<std::mutex>lock(s.mutex);
 uint8_t reply[IMAGE_PACKET_MAX];
 size_t n=0;
 int rc=vx_image_receiver_handle(s.rx,bytes.data,bytes.size,peer.address,peer.port,now,reply,sizeof(reply),&n);
 if(rc)return Result<std::vector<uint8_t>>::failure(neterr(-rc,"original packet receiver"));
 try{return Result<std::vector<uint8_t>>::success({reply,reply+n});
 }catch(const std::bad_alloc&){return Result<std::vector<uint8_t>>::failure(neterr(ENOMEM,"bounded reply"));
 }
}
Result<void>ImageReceiver::poll(uint64_t now){auto&s=*impl_;
 std::lock_guard<std::mutex>lock(s.mutex);
 int rc=vx_image_receiver_poll(s.rx,now);
 return rc?Result<void>::failure(neterr(-rc,"original receive timeout")):Result<void>::success();
 }
Result<std::optional<ImageIngress>>ImageReceiver::acquire(){
 using R=Result<std::optional<ImageIngress>>;
 auto&s=*impl_;
 vx_image_ingress q{};
 {std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.rx)return R::failure(neterr(ESHUTDOWN,"receiver is closed"));
 if(s.pending||s.acquiring)return R::success(std::nullopt);
 if(!s.unprepared){int rc=vx_image_receiver_acquire(s.rx,&q);if(rc<0)return R::failure(neterr(-rc,"acquire pinned ingress"));if(!rc)return R::success(std::nullopt);s.unprepared=q;}
 q=*s.unprepared;s.acquiring=true;
 }
 // The original C receiver pins q's buffer. Do not acquire Runtime locks or
 // allocate a snapshot while holding the application receiver mutex.
 struct Window{Impl&s;~Window(){std::lock_guard<std::mutex>lock(s.mutex);s.acquiring=false;}}window{s};
 struct Snapshot{std::shared_ptr<HostMemoryLease>charge;std::vector<uint8_t>bytes;};
 ImageIngress in;
 std::optional<ImageIngress>discarded;
 try{
 in.serial=q.serial;
 in.image.request={s.boot,std::to_string(q.peer_ip)+":"+std::to_string(q.peer_port),std::to_string(s.session)+":"+std::to_string(q.wire.tx)+":"+std::to_string(q.serial)};
 if(s.native){auto rt=s.runtime.lock();if(!rt)return R::failure(neterr(ESHUTDOWN,"native Runtime is gone"));auto slot=rt->reserve_preparation(in.image.request,AppKind::IMAGE);if(!slot){if(slot.error->code==EAGAIN||slot.error->code==ENOSPC||slot.error->code==EBUSY)return R::success(std::nullopt);return R::failure(*slot.error);}in.preparation=*slot.value;
 auto charge=rt->reserve_host_memory(q.bytes);if(!charge)return R::failure(*charge.error);
 auto snapshot=std::make_shared<Snapshot>();snapshot->charge=*charge.value;snapshot->bytes.assign(q.rgba,q.rgba+q.bytes);in.image.rgba=std::shared_ptr<const std::vector<uint8_t>>(snapshot,&snapshot->bytes);
 }else in.image.rgba=std::make_shared<const std::vector<uint8_t>>(q.rgba,q.rgba+q.bytes);
 in.image.width=IMAGE_WIDTH;
 in.image.height=IMAGE_HEIGHT;
 in.image.input_id=q.wire.input_id;
 in.image.crc=q.wire.image_crc;
 in.image.rectangle=q.wire.rect;
 in.image.params=q.wire.params;
 in.image.display_cookie=q.display_cookie;
 {std::lock_guard<std::mutex>lock(s.mutex);s.wire=q.wire;s.pending=in;s.unprepared.reset();}
 return R::success(in);
 }catch(const std::bad_alloc&){std::lock_guard<std::mutex>lock(s.mutex);int cleanup=vx_image_receiver_fail(s.rx,q.serial,6);
 if(cleanup)return R::failure(neterr(-cleanup,"ingress failure cleanup retained"));
 discarded=std::move(s.pending);s.pending.reset();s.unprepared.reset();
 return R::failure(neterr(ENOMEM,"immutable ingress allocation"));
 }
}
Result<void>ImageReceiver::admitted(uint64_t serial,std::shared_ptr<ImageWorkRequest>app,const RequestKey&accepted,uint32_t before){
 auto&s=*impl_;
 std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.pending||s.pending->serial!=serial||s.bound||!app)return Result<void>::failure(neterr(ESTALE,"one exact pending admission required"));
 const auto&w=app->request();
 const auto&in=s.pending->image;
 Digest input{};
 vxl_hunet_hash(in.rgba->data(),in.rgba->size(),input.data());

 if(!app->matches_input(in)||!samekey(accepted,in.request)||!samekey(w.request,in.request)||w.app!=AppKind::IMAGE||!samehw(app->plan().policy.hardware,s.hardware)||w.input_bytes!=in.rgba->size()||w.input_sha!=input||app->plan().total_jobs>VX_IMAGE_MAX_PASSES)return Result<void>::failure(neterr(EPROTO,"admitted work/input/hardware differs"));

 int rc=vx_image_receiver_admitted(s.rx,serial);
 if(rc)return Result<void>::failure(neterr(-rc,"immutable receiver admission"));
 s.bound=std::move(app);
 s.jobs_before=before;
 return Result<void>::success();

}
Result<void>ImageReceiver::complete(const ResultLease&lease,const DisplayReceipt&dp,uint64_t total){
 auto&s=*impl_;
 std::optional<ImageIngress>released_input;std::shared_ptr<ImageWorkRequest>released_owner;
 std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.pending||!s.bound)return Result<void>::failure(neterr(ESTALE,"no admitted image result"));
 const auto&w=s.bound->request();
 auto snapshot=s.bound->receipt_snapshot();
 if(!snapshot)return Result<void>::failure(*snapshot.error);
 const auto&r=*snapshot.value;

 if(!r.published||!r.retired||r.first_error||r.evidence_error||!r.image_result||!r.display||!samedp(dp,*r.display))return Result<void>::failure(neterr(EPROTO,"actual completed owner and exact display proof required"));
 const auto&result=*r.image_result;

 if(!result.gpu_complete||!result.displayed||result.first_error||result.scope!=s.scope||!samekey(result.request,w.request)||result.plan!=w.plan||result.input_sha!=w.input_sha||result.completed_gpu!=s.bound->plan().total_jobs||result.gpu_jobs!=result.completed_gpu||r.completed_jobs!=result.completed_gpu||result.gpu_raw_cycles!=r.gpu_raw_cycles||!samekey(lease.producer,w.request)||lease.app!=AppKind::IMAGE||lease.scope!=s.scope||lease.hardware!=s.hardware.hardware_contract||lease.model!=w.model||lease.plan!=w.plan||!lease.lease_tokens.empty())return Result<void>::failure(neterr(EPROTO,"result lease/plan/scope differs"));

 bool exact=false;
 for(const auto&a:lease.artifacts)exact|=!a.path.empty()&&a.sha==result.output_sha&&a.layout=="RGBA8888"&&a.shape==vxnn::Shape{720,1280,4}&&a.bytes==IMAGE_BYTES;

 // had_old_front describes a retained producer. The original C/KMS engine
 // also retires its initial blank scanout, with no old producer lease. Preserve
 // that real event/time; never normalize it into a fabricated missing event.
 if(!exact||!r.render_begin_ns||r.render_end_ns<r.render_begin_ns||!dp.submitted_ns||!dp.presented_ns||dp.result||!dp.presented||dp.cookie!=s.pending->image.display_cookie||*dp.presented_ns<*dp.submitted_ns||(dp.had_old_front&&!dp.old_retired)||(dp.old_retired?(!dp.old_retired_ns||*dp.old_retired_ns<*dp.presented_ns):dp.old_retired_ns.has_value()))return Result<void>::failure(neterr(EPROTO,"output hash/extent/clock/retirement differs"));

 if(s.scope==vxnn::EvidenceScope::REAL_BOARD&&*dp.submitted_ns<r.render_end_ns)return Result<void>::failure(neterr(EPROTO,"display preceded render completion"));

 vx_image_rx_completion c{};
 c.serial=s.pending->serial;
 c.display_cookie=dp.cookie;
 c.total_ns=total;
 c.output.input_id=s.wire.input_id;
 c.output.style=s.wire.params.style;
 c.output.preset=s.wire.params.preset;
 c.output.block=s.wire.params.block;
 c.output.output_crc=result.output_crc;
 c.output.cache_hit=result.cache_hit;
 c.output.pass_count=result.gpu_jobs;
 c.output.gpu_cycles=result.gpu_raw_cycles.value_or(0);
 c.output.generate_ticks=r.render_end_ns-r.render_begin_ns;
 c.output.blit_ticks=*dp.presented_ns-*dp.submitted_ns;
 c.output.retire_ticks=dp.had_old_front?*dp.old_retired_ns-*dp.presented_ns:0;
 c.jobs_before=s.jobs_before;
 c.jobs_after=s.jobs_before+result.gpu_jobs;
 c.scope=uint32_t(s.scope);
 c.cycles_valid=result.gpu_raw_cycles.has_value();
 c.execution_complete=1;

 vx_image_rx_display d{};
 d.cookie=dp.cookie;
 d.had_old_front=dp.had_old_front;
 d.submitted_ns=*dp.submitted_ns;
 d.presented_ns=*dp.presented_ns;
 d.old_retired_ns=dp.old_retired_ns.value_or(0);
 d.scope=c.scope;
 d.clock_domain=uint32_t(dp.clock_domain);
 d.presented=dp.presented;
 d.old_retired=dp.old_retired;

 int rc=vx_image_receiver_complete(s.rx,&c,&d);
 if(rc)return Result<void>::failure(neterr(-rc,"original completion metrics refused"));
 released_input=std::move(s.pending);s.pending.reset();released_owner=std::move(s.bound);
 return Result<void>::success();

}
Result<void>ImageReceiver::fail(uint64_t serial,uint32_t reason){auto&s=*impl_;
 std::optional<ImageIngress>released_input;std::shared_ptr<ImageWorkRequest>released_owner;
 std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.pending||s.pending->serial!=serial)return Result<void>::failure(neterr(ESTALE,"failure request differs"));
 if(s.bound){auto snapshot=s.bound->receipt_snapshot();
 if(!snapshot)return Result<void>::failure(*snapshot.error);
 if(!snapshot.value->retired)return Result<void>::failure(neterr(EBUSY,"live failed owner remains retained"));
 }
 int rc=vx_image_receiver_fail(s.rx,serial,reason);
 if(rc)return Result<void>::failure(neterr(-rc,"wire failure"));
 released_input=std::move(s.pending);s.pending.reset();released_owner=std::move(s.bound);
 return Result<void>::success();
 }
Result<void>ImageReceiver::close(){auto&s=*impl_;
 std::optional<ImageIngress>released_input;std::shared_ptr<HostMemoryLease>released_persistent;
 std::lock_guard<std::mutex>lock(s.mutex);
 if(!s.rx)return Result<void>::success();
 if(s.acquiring||s.bound||s.pending)return Result<void>::failure(neterr(EBUSY,"pending input retained"));
 if(s.native&&s.unprepared){int cancel=vx_image_receiver_fail(s.rx,s.unprepared->serial,3);if(cancel)return Result<void>::failure(neterr(-cancel,"unadmitted ingress cancellation retained"));s.unprepared.reset();}
 if(s.pending||s.unprepared)return Result<void>::failure(neterr(EBUSY,"pending input retained"));
 if(s.native){int stop=vx_image_receiver_quiesce_unadmitted(s.rx);if(stop)return Result<void>::failure(neterr(-stop,"native unsubmitted input cancellation retained"));}
 int rc=vx_image_receiver_close(s.rx);
 if(rc)return Result<void>::failure(neterr(-rc,"receiver close"));
 s.rx=nullptr;
 released_persistent=std::move(s.persistent);
 return Result<void>::success();
 }
}

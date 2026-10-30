#include <vxunified/presentation.hpp>
#include <vxunified/native_admission.hpp>
#include <mutex>
#include <time.h>
namespace vxunified {
namespace {
uint64_t clock_ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
bool same_identity(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
}
struct NativeDisplay::State {
 std::weak_ptr<Runtime>runtime;BackendIdentity hardware;vxnn::EvidenceScope scope;
 std::shared_ptr<DisplayConsumer>owner;std::shared_ptr<HostMemoryLease>raster_charge;PersistResult stage_journal;DisplayJournal journal;
 mutable std::mutex mutex;bool closing=false,retired=false;uint64_t application_cookie=0;std::optional<vxnn::Error>first_error,evidence_error;
 static DisplayJournal observations(const std::shared_ptr<State>&s){return [weak=std::weak_ptr<State>(s)](const DisplayObservation&o){auto current=weak.lock();if(!current)return Result<void>::failure({ESHUTDOWN,"NATIVE_DISPLAY","display evidence owner expired"});auto row=o;row.application_cookie=current->application_cookie;return current->journal(row);};}
 static Result<std::shared_ptr<State>>make(const StartupContext&c,DisplayJournal journal,PersistResult stage){
  using R=Result<std::shared_ptr<State>>;auto rt=c.runtime.lock();
  if(!rt||c.mode!=BackendMode::COMPUTE_V2||c.results.lock()!=rt->trusted_results()||c.scope!=rt->evidence_scope()||!same_identity(c.identity,rt->identity())||c.boot_id.empty()||!journal||!stage)return R::failure({EXDEV,"NATIVE_DISPLAY","same live Runtime/result roots/scope/platform and trusted journals required"});
  auto charge=rt->reserve_host_memory(RasterComposeCache::budget_bytes);if(!charge)return R::failure(*charge.error);
  auto s=std::make_shared<State>();s->raster_charge=*charge.value;s->runtime=rt;s->hardware=c.identity;s->scope=c.scope;s->journal=std::move(journal);s->stage_journal=std::move(stage);return R::success(std::move(s));
 }
 Result<void>validate(const PublishedResult&p)const{
  if(first_error)return Result<void>::failure(*first_error);
  if(closing||runtime.expired())return Result<void>::failure({ESHUTDOWN,"NATIVE_DISPLAY","common owner closed or expired"});
  if(p.lease.scope!=scope||p.lease.hardware!=hardware.hardware_contract)return Result<void>::failure({EXDEV,"NATIVE_DISPLAY","result scope/platform differs common owner"});
  return validate_published_result(p);
 }
 vxnn::Error fault(vxnn::Error error){
  if(!first_error)first_error=std::move(error);
  try{auto row=owner->snapshot();row.application_cookie=application_cookie;if(!row.first_error)row.first_error=first_error;auto saved=journal(row);if(!saved&&!evidence_error)evidence_error=saved.error;}
  catch(...){if(!evidence_error)evidence_error=vxnn::Error{EIO,"NATIVE_DISPLAY_EVIDENCE","display error journal exception; original first error retained"};}
  return *first_error;
 }
 Result<DisplayReceipt>present(const PublishedResult&p,const DisplayLayout&l,uint64_t cookie){
  using R=Result<DisplayReceipt>;const auto begin=clock_ns();if(!begin||begin>UINT64_MAX-3000000000ULL)return R::failure({EIO,"NATIVE_DISPLAY","presentation clock unavailable"});const auto deadline=begin+3000000000ULL;
  const bool joint=s8_joint_platform(hardware);application_cookie=joint?cookie:0;
  // The common ResultStore allocator already keeps DP tokens globally unique,
  // including a retained previous front. Preserve old wire/application IDs
  // solely as a recorded mapping, never as a second allocation namespace.
  auto submitted=owner->present(p.lease,l,joint?0:cookie);if(!submitted)return R::failure(fault(*submitted.error));
  while(owner->snapshot().pending){
   auto time=clock_ns();if(!time||time<begin||time>=deadline)return R::failure(fault({ETIMEDOUT,"NATIVE_DISPLAY","original 3s observation bound exhausted; backing retained"}));
   auto ms=uint32_t(std::min<uint64_t>(3000,(deadline-time+999999)/1000000));auto observed=owner->poll(ms);
   if(!observed){if(observed.error->code==EINTR||observed.error->code==EAGAIN)continue;return R::failure(fault(*observed.error));}
  }
  auto proof=owner->snapshot().receipt;if(!proof.presented||!proof.submitted_ns||!proof.presented_ns||(proof.had_old_front&&!proof.old_retired)||proof.result)return R::failure(fault({EPROTO,"NATIVE_DISPLAY","actual presentation and old result retirement required"}));
  if(joint&&cookie)proof.cookie=cookie;
  return R::success(std::move(proof));
 }
};
NativeDisplay::NativeDisplay(std::shared_ptr<State>s):state_(std::move(s)){}
Result<std::shared_ptr<NativeDisplay>>NativeDisplay::create(const StartupContext&c,const dp_ops&ops,DisplayComposer compose,DisplayJournal journal,PersistResult stage,std::shared_ptr<void>lifetime){
 using R=Result<std::shared_ptr<NativeDisplay>>;try{
  if(c.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST)return R::failure({EPERM,"NATIVE_DISPLAY","injected OS operations cannot produce physical display evidence"});
  auto prepared=State::make(c,std::move(journal),std::move(stage));if(!prepared)return R::failure(*prepared.error);auto s=*prepared.value;
  auto owner=DisplayConsumer::create(c.results.lock(),ops,vxnn::ClockDomain::HOST_MONOTONIC,std::move(compose),State::observations(s),std::move(lifetime),s->raster_charge);if(!owner)return R::failure(*owner.error);s->owner=*owner.value;
  return R::success(std::shared_ptr<NativeDisplay>(new NativeDisplay(std::move(s))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_DISPLAY","common display allocation"});}
}
Result<std::shared_ptr<NativeDisplay>>NativeDisplay::create_native(const StartupContext&c,DisplayComposer compose,DisplayJournal journal,PersistResult stage,const std::string&device){
 using R=Result<std::shared_ptr<NativeDisplay>>;try{
  if(c.scope!=vxnn::EvidenceScope::REAL_BOARD)return R::failure({EPERM,"NATIVE_DISPLAY","native KMS requires the real-I/O startup scope"});
  auto admission=admit_native_startup(c,c.identity.provider_flags);if(!admission)return R::failure(*admission.error);
  auto prepared=State::make(c,std::move(journal),std::move(stage));if(!prepared)return R::failure(*prepared.error);auto s=*prepared.value;
  auto owner=DisplayConsumer::create_native(c.results.lock(),std::move(compose),State::observations(s),device,s->raster_charge);if(!owner)return R::failure(*owner.error);s->owner=*owner.value;
  return R::success(std::shared_ptr<NativeDisplay>(new NativeDisplay(std::move(s))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_DISPLAY","native display allocation"});}
}
Result<DisplayReceipt>NativeDisplay::stage_and_present(const PublishedResult&p,const DisplayLayout&l,uint64_t cookie){
 using R=Result<DisplayReceipt>;auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto valid=s.validate(p);if(!valid)return R::failure(*valid.error);auto rt=s.runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"NATIVE_DISPLAY","common Runtime expired"});
 auto staged=rt->stage_display(p,s.stage_journal);if(!staged)return R::failure(*staged.error);return s.present(p,l,cookie);
}
Result<DisplayReceipt>NativeDisplay::present_result(const PublishedResult&p,const DisplayLayout&l,uint64_t cookie){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto valid=s.validate(p);if(!valid)return Result<DisplayReceipt>::failure(*valid.error);return s.present(p,l,cookie);}
Result<void>NativeDisplay::retire(){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);if(s.retired)return Result<void>::success();if(s.closing)return Result<void>::failure(s.first_error.value_or(vxnn::Error{EBUSY,"NATIVE_DISPLAY","previous failed restore retained; no implicit retry"}));s.closing=true;auto done=s.owner->restore();if(!done)return Result<void>::failure(s.fault(*done.error));s.retired=true;return Result<void>::success();}
DisplayObservation NativeDisplay::snapshot()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto observation=s.owner->snapshot();observation.application_cookie=s.application_cookie;if(!observation.first_error&&s.first_error)observation.first_error=s.first_error;return observation;}
dp_caps NativeDisplay::capabilities()const{std::lock_guard<std::mutex>lock(state_->mutex);return state_->owner->capabilities();}
bool NativeDisplay::owns_runtime(const Runtime*r)const{return r&&state_->runtime.lock().get()==r;}
}

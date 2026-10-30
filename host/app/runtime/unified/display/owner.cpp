#include "internal.hpp"
namespace vxunified {
namespace {std::mutex owner_mutex;bool owner_claimed=false;}
struct DisplayConsumer::State {
 detail::ExitRetention exit;std::mutex mutex;std::shared_ptr<ResultStore> store;
 DisplayComposer compose;DisplayJournal journal;dp_context context{};
 std::shared_ptr<void> context_lifetime;std::shared_ptr<void>raster_charge;RasterComposeCache raster_cache;
 vxnn::ClockDomain clock=vxnn::ClockDomain::HOST_MONOTONIC;
 std::shared_ptr<display_detail::Hold> front,pending;
 uint64_t front_cookie=0,pending_cookie=0;bool owns=false;
 DisplayObservation observation;
 void unclaim(){if(owns){std::lock_guard<std::mutex>l(owner_mutex);owner_claimed=false;owns=false;}}
 vxnn::Error fail(const vxnn::Error&e){
  if(!observation.first_error)observation.first_error=e;
  observation.receipt.result=observation.first_error->code;return *observation.first_error;
 }
 vxnn::Error fail(int n,const char*message){return fail(vxnn::Error{n<0?-n:n,"DISPLAY",message});}
 Result<void> save(){try{return journal(observation);}catch(...){return Result<void>::failure({EIO,"DISPLAY_EVIDENCE","display journal exception"});}}
};
DisplayConsumer::DisplayConsumer(std::shared_ptr<State>s):state_(std::move(s)){}
DisplayConsumer::~DisplayConsumer(){
 try{if(state_->observation.first_error&&(state_->context.opened||state_->front||state_->pending)){state_->exit.retain(state_);return;}auto r=restore();if(!r)state_->exit.retain(state_);}catch(...){state_->exit.retain(state_);}
}
Result<std::shared_ptr<DisplayConsumer>> DisplayConsumer::create(std::shared_ptr<ResultStore>store,const dp_ops&ops,vxnn::ClockDomain clock,DisplayComposer compose,DisplayJournal journal,std::shared_ptr<void> lifetime,std::shared_ptr<void>raster_charge){
 if(!store||!compose||!journal||(clock!=vxnn::ClockDomain::HOST_MONOTONIC&&clock!=vxnn::ClockDomain::LINUX_MONOTONIC))return Result<std::shared_ptr<DisplayConsumer>>::failure({EINVAL,"DISPLAY","missing trusted display dependencies"});
 try{
  auto s=std::make_shared<State>();s->store=std::move(store);s->compose=std::move(compose);s->journal=std::move(journal);s->clock=clock;s->context_lifetime=std::move(lifetime);s->raster_charge=std::move(raster_charge);s->observation.raw_events.reserve(8);
  auto out=std::shared_ptr<DisplayConsumer>(new DisplayConsumer(s));
  {std::lock_guard<std::mutex>lock(owner_mutex);if(owner_claimed)return Result<std::shared_ptr<DisplayConsumer>>::failure({EBUSY,"DISPLAY","one KMS owner already held"});owner_claimed=true;s->owns=true;}
  dp_config config{1280,720};int rc=dp_open(&s->context,&config,&ops);
  if(rc){auto e=s->fail(rc,"KMS open/initial mode failed");if(s->context.opened)s->exit.retain(s);else s->unclaim();return Result<std::shared_ptr<DisplayConsumer>>::failure(e);}
  s->observation.receipt.clock_domain=clock;return Result<std::shared_ptr<DisplayConsumer>>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<DisplayConsumer>>::failure({ENOMEM,"DISPLAY","display owner allocation"});}
}
Result<DisplayReceipt> DisplayConsumer::present(const ResultLease&lease,const DisplayLayout&layout){
 return present(lease,layout,0);
}
Result<DisplayReceipt> DisplayConsumer::present(const ResultLease&lease,const DisplayLayout&layout,uint64_t logical_cookie){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.observation.first_error)return Result<DisplayReceipt>::failure(*s.observation.first_error);
 if(!s.context.opened||!s.owns)return Result<DisplayReceipt>::failure({ESHUTDOWN,"DISPLAY","display is restored"});
 if(s.pending)return Result<DisplayReceipt>::failure({EBUSY,"DISPLAY","one pending presentation"});
 try{
  auto hold=std::make_shared<display_detail::Hold>(s.store,lease.producer);
  if(!hold->lease)return Result<DisplayReceipt>::failure(*hold->lease.error);
  // Publish the holder before any further allocation or callback can fail.
  s.pending=std::move(hold);
  auto refuse=[&](vxnn::Error e){auto released=display_detail::release(s.pending);s.observation.pending=bool(s.pending);if(!released)return Result<DisplayReceipt>::failure(s.fail(*released.error));return Result<DisplayReceipt>::failure(std::move(e));};
  if(logical_cookie){auto bound=s.store->bind_display_cookie(*s.pending->lease.value,logical_cookie);if(!bound)return refuse(*bound.error);}
  auto source=s.store->display_source(*s.pending->lease.value);
  if(!source)return refuse(*source.error);
  if(!display_detail::same_source(lease,source.value->lease))return refuse({ESTALE,"DISPLAY","producer result lease differs"});
  if(s.clock==vxnn::ClockDomain::HOST_MONOTONIC&&lease.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST)return refuse({EPERM,"DISPLAY","host fake KMS cannot mark non-host evidence displayed"});
  s.observation=DisplayObservation{};s.observation.raw_events.reserve(8);s.observation.source_lease_token=s.pending->lease.value->token;s.observation.receipt.clock_domain=s.clock;s.observation.receipt.cookie=logical_cookie?logical_cookie:s.pending->lease.value->token;s.observation.receipt.had_old_front=bool(s.front);s.observation.pending=true;
  s.observation.raster_cache_budget_bytes=s.raster_charge?RasterComposeCache::budget_bytes:0;s.observation.compose_begin_ns=dp_now();auto canvas=[&](){display_detail::RasterCacheScope selected(s.raster_charge?&s.raster_cache:nullptr);return s.compose(*source.value,layout);}();s.observation.compose_end_ns=dp_now();s.observation.raster_cache=s.raster_charge?s.raster_cache.stats():RasterComposeStats{};if(!canvas)return refuse(*canvas.error);
  auto frame=display_detail::frame(*source.value,**canvas.value,layout,s.context.caps.width,s.context.caps.height);if(!frame)return refuse(*frame.error);
  s.observation.copy_begin_ns=dp_now();int rc=(**canvas.value).has_composition_seal()?dp_submit_prevalidated_immutable(&s.context,&*frame.value):dp_submit(&s.context,&*frame.value);s.observation.copy_end_ns=dp_now();
  if(rc){if(!s.context.quarantined)display_detail::release(s.pending);return Result<DisplayReceipt>::failure(s.fail(rc,"KMS submit failed"));}
  s.pending_cookie=s.context.frames[s.context.pending-1].cookie;s.observation.receipt.submitted_ns=s.context.frames[s.context.pending-1].received_ns;
  auto saved=s.save();if(!saved)return Result<DisplayReceipt>::failure(s.fail(*saved.error));
  return Result<DisplayReceipt>::success(s.observation.receipt);
 }catch(const std::bad_alloc&){if(s.context.pending||s.context.quarantined)return Result<DisplayReceipt>::failure(s.fail(ENOMEM,"allocation after display submit; backing retained"));auto released=display_detail::release(s.pending);s.observation.pending=bool(s.pending);if(!released)return Result<DisplayReceipt>::failure(s.fail(*released.error));return Result<DisplayReceipt>::failure({ENOMEM,"DISPLAY","presentation allocation"});}
 catch(...){return Result<DisplayReceipt>::failure(s.fail(EIO,"display composer exception; backing retained"));}
}
Result<DisplayReceipt> DisplayConsumer::poll(uint32_t ms){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);
 if(s.observation.first_error)return Result<DisplayReceipt>::failure(*s.observation.first_error);
 if(!s.pending||ms>3000)return Result<DisplayReceipt>::failure({ms>3000?EINVAL:EAGAIN,"DISPLAY","no bounded pending display event"});
 try{
  dp_event event{};s.observation.poll_begin_ns=dp_now();int rc=dp_poll(&s.context,int(ms),&event);s.observation.poll_end_ns=dp_now();
  if(rc==-EAGAIN||rc==-EINTR)return Result<DisplayReceipt>::failure({-rc,"DISPLAY","display event pending"});
  if(event.kind)s.observation.raw_events.push_back(event);
  if(rc){auto e=s.fail(rc,"KMS event failure");s.save();return Result<DisplayReceipt>::failure(e);}
  auto&r=s.observation.receipt;
  if(event.kind==DP_SUBMITTED){if(event.cookie!=s.pending_cookie)return Result<DisplayReceipt>::failure(s.fail(EPROTO,"submitted cookie mismatch"));}
  else if(event.kind==DP_PRESENTED){
   if(event.cookie!=s.pending_cookie||!r.submitted_ns||event.received_ns<*r.submitted_ns)return Result<DisplayReceipt>::failure(s.fail(EPROTO,"latched cookie/time mismatch"));
   r.presented=true;r.presented_ns=event.received_ns;
  }else if(event.kind==DP_RETIRED){
   if(event.cookie!=s.front_cookie||!r.presented||!r.presented_ns)return Result<DisplayReceipt>::failure(s.fail(EPROTO,"old front retirement mismatch"));
   // Observation time, not a fabricated or rewritten raw kernel timestamp.
   r.old_retired=true;r.old_retired_ns=s.observation.poll_end_ns;
   auto saved=s.save();if(!saved)return Result<DisplayReceipt>::failure(s.fail(*saved.error));
   auto shown=s.store->mark_displayed(*s.pending->lease.value,r);if(!shown)return Result<DisplayReceipt>::failure(s.fail(*shown.error));
   auto old=display_detail::release(s.front);if(!old)return Result<DisplayReceipt>::failure(s.fail(*old.error));
   s.front=std::move(s.pending);s.front_cookie=s.pending_cookie;s.pending_cookie=0;s.observation.pending=false;
  }else return Result<DisplayReceipt>::failure(s.fail(EPROTO,"unexpected display event"));
  auto saved=s.save();if(!saved)return Result<DisplayReceipt>::failure(s.fail(*saved.error));return Result<DisplayReceipt>::success(r);
 }catch(const std::bad_alloc&){return Result<DisplayReceipt>::failure(s.fail(ENOMEM,"display event allocation; backing retained"));}
}
Result<void> DisplayConsumer::restore(){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);int rc=dp_close(&s.context);
 if(rc){auto e=s.fail(rc,"original display state restore failed");s.save();return Result<void>::failure(e);}
 s.unclaim();s.observation.restored=true;s.context_lifetime.reset();
 auto pending=display_detail::release(s.pending);if(!pending)return Result<void>::failure(s.fail(*pending.error));
 auto front=display_detail::release(s.front);if(!front)return Result<void>::failure(s.fail(*front.error));
 s.observation.pending=false;return Result<void>::success();
}
DisplayObservation DisplayConsumer::snapshot()const{std::lock_guard<std::mutex>lock(state_->mutex);return state_->observation;}
dp_caps DisplayConsumer::capabilities()const{std::lock_guard<std::mutex>lock(state_->mutex);return state_->context.opened?state_->context.caps:dp_caps{};}
}

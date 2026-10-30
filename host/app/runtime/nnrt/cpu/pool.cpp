#include <vxnn/cpu.hpp>
#include <vxunified/exit_retention.hpp>
#include <algorithm>
#include <cerrno>
#include <cfenv>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <pthread.h>
#include <stdexcept>
#include <time.h>
#include <unistd.h>
namespace vxunified {
struct PendingBudget::State {struct Reservation {uint32_t count=0;bool claimed=false;};mutable std::mutex mutex;uint32_t used=0;uint64_t next=1;std::map<uint64_t,Reservation> reservations;};
PendingBudget::PendingBudget():state_(std::make_shared<State>()){}
vxnn::Result<uint64_t> PendingBudget::reserve(uint32_t count){
 std::lock_guard<std::mutex>lock(state_->mutex);
 if(!count||count>32)return vxnn::Result<uint64_t>::failure({EINVAL,"PENDING_BUDGET","invalid count"});
 if(count>32-state_->used)return vxnn::Result<uint64_t>::failure({EAGAIN,"PENDING_BUDGET","global32 exhausted"});
 if(!state_->next)return vxnn::Result<uint64_t>::failure({EOVERFLOW,"PENDING_BUDGET","token id exhausted"});
 auto token=state_->next++;state_->reservations.emplace(token,State::Reservation{count,false});state_->used+=count;return vxnn::Result<uint64_t>::success(token);
}
vxnn::Result<void> PendingBudget::release(uint64_t token){
 std::lock_guard<std::mutex>lock(state_->mutex);auto it=state_->reservations.find(token);
 if(it==state_->reservations.end())return vxnn::Result<void>::failure({ENOENT,"PENDING_BUDGET","unknown or retired token"});
 if(it->second.claimed)return vxnn::Result<void>::failure({EBUSY,"PENDING_BUDGET","reservation still executing"});
 state_->used-=it->second.count;state_->reservations.erase(it);return vxnn::Result<void>::success();
}
uint32_t PendingBudget::used()const{std::lock_guard<std::mutex>lock(state_->mutex);return state_->used;}
vxnn::Result<void> PendingBudget::claim(uint64_t token,uint32_t count){std::lock_guard<std::mutex>lock(state_->mutex);auto it=state_->reservations.find(token);if(it==state_->reservations.end()||it->second.count!=count)return vxnn::Result<void>::failure({EINVAL,"PENDING_BUDGET","unknown token or count"});if(it->second.claimed)return vxnn::Result<void>::failure({EBUSY,"PENDING_BUDGET","already executing"});it->second.claimed=true;return vxnn::Result<void>::success();}
vxnn::Result<void> PendingBudget::unclaim(uint64_t token){std::lock_guard<std::mutex>lock(state_->mutex);auto it=state_->reservations.find(token);if(it==state_->reservations.end()||!it->second.claimed)return vxnn::Result<void>::failure({EINVAL,"PENDING_BUDGET","not claimed"});it->second.claimed=false;return vxnn::Result<void>::success();}
std::shared_ptr<PendingBudget> process_pending_budget(){static auto budget=std::make_shared<PendingBudget>();return budget;}
}
namespace vxnn {namespace {
thread_local const void* current_pool=nullptr;
struct WorkerSlots {std::mutex mutex;uint32_t used=0;};
WorkerSlots& worker_slots(){static auto*slots=new WorkerSlots;return *slots;}
uint64_t stamp(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))throw std::runtime_error("CLOCK_MONOTONIC");return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
}
struct CpuPool::State {
 vxunified::detail::ExitRetention exit_retention;
 struct Run {std::mutex mutex;std::condition_variable done;RangeFn fn;size_t count=0;uint32_t remaining=0;fenv_t environment{};std::shared_ptr<CpuCancel> cancel;std::optional<Error> error;uint64_t first_ns=0,last_ns=0;};
 struct Worker {State* state=nullptr;uint32_t index=0;pthread_t thread{};std::deque<std::shared_ptr<Run>> queue;};
 std::mutex mutex,join_mutex;std::condition_variable ready;std::vector<std::unique_ptr<Worker>> workers;
 std::shared_ptr<vxunified::PendingBudget> budget;size_t max_tasks=0,inflight=0;uint32_t count=0,created=0;bool closing=false,join_attempted=false,slots_held=false;int init_error=0,join_error=0;
 static void* enter(void*ptr){auto*w=static_cast<Worker*>(ptr);w->state->loop(*w);return nullptr;}
 void loop(Worker&w){
  current_pool=this;
  while(true){
   std::shared_ptr<Run>run;
   {std::unique_lock<std::mutex>lock(mutex);ready.wait(lock,[&]{return closing||!w.queue.empty();});if(w.queue.empty()){if(closing)break;continue;}run=w.queue.front();w.queue.pop_front();}
   std::optional<Error> error;uint64_t begin=0,end=0;fenv_t saved{};bool saved_ok=fegetenv(&saved)==0;
   try{
    begin=stamp();
    if(!saved_ok||fesetenv(&run->environment))throw std::runtime_error("worker floating environment");
    if(run->cancel&&run->cancel->cancelled.load())error=Error{ECANCELED,"CPU_POOL","cancelled before range"};
    else{
     // Division first preserves n*i/workers without overflowing n*i.
     size_t a=(run->count/count)*w.index+((run->count%count)*w.index)/count;
     size_t b=(run->count/count)*(w.index+1)+((run->count%count)*(w.index+1))/count;
     auto result=run->fn(a,b);if(!result)error=*result.error;
    }
    end=stamp();
   }catch(const std::exception&e){error=Error{EIO,"CPU_POOL",e.what()};}catch(...){error=Error{EIO,"CPU_POOL","unknown callback exception"};}
   if(saved_ok&&fesetenv(&saved)&&!error)error=Error{EIO,"CPU_POOL","restore floating environment"};
   {std::lock_guard<std::mutex>lock(mutex);--inflight;}
   {std::lock_guard<std::mutex>lock(run->mutex);if(error&&!run->error)run->error=error;if(begin&&(!run->first_ns||begin<run->first_ns))run->first_ns=begin;run->last_ns=std::max(run->last_ns,end);--run->remaining;run->done.notify_all();}
  }
  current_pool=nullptr;
 }
};
CpuPool::CpuPool(uint32_t workers,size_t max_tasks,std::shared_ptr<vxunified::PendingBudget> budget):state_(std::make_shared<State>()){
 auto&s=*state_;s.budget=std::move(budget);s.max_tasks=max_tasks;s.count=workers;long online=sysconf(_SC_NPROCESSORS_ONLN);
 if(!s.budget||!workers||workers>4||max_tasks<workers||max_tasks>32){s.init_error=EINVAL;return;}
 if(online<1||uint64_t(online)<workers){s.init_error=ENOTSUP;return;}
 try{for(uint32_t i=0;i<workers;i++){auto w=std::make_unique<State::Worker>();w->state=&s;w->index=i;s.workers.push_back(std::move(w));}}
 catch(...){s.init_error=ENOMEM;return;}
 {auto&slots=worker_slots();std::lock_guard<std::mutex>lock(slots.mutex);if(workers>4-slots.used){s.init_error=EAGAIN;return;}slots.used+=workers;s.slots_held=true;}
 for(auto&w:s.workers){int rc=pthread_create(&w->thread,nullptr,State::enter,w.get());if(rc){s.init_error=rc;shutdown();return;}++s.created;}
}
CpuPool::~CpuPool(){try{if(shutdown())return;}catch(...){/* Error construction can exhaust memory too. */}state_->exit_retention.retain(state_);}
Result<CpuTiming> CpuPool::run_ranges_reserved(size_t n,const RangeFn&fn,std::shared_ptr<CpuCancel>cancel,uint64_t token){if(!token)return Result<CpuTiming>::failure({EINVAL,"CPU_POOL","missing borrowed reservation"});return run_impl(n,fn,std::move(cancel),token);}
Result<void> CpuPool::ready()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);if(s.init_error||s.closing)return Result<void>::failure({s.init_error?s.init_error:ESHUTDOWN,"CPU_POOL","not ready"});return Result<void>::success();}
uint32_t CpuPool::workers()const{std::lock_guard<std::mutex>lock(state_->mutex);return state_->init_error||state_->closing?0:state_->created;}
Result<CpuTiming> CpuPool::run_ranges(size_t n,const RangeFn&fn,std::shared_ptr<CpuCancel>cancel){
 return run_impl(n,fn,std::move(cancel),0);
}
Result<CpuTiming> CpuPool::run_impl(size_t n,const RangeFn&fn,std::shared_ptr<CpuCancel>cancel,uint64_t borrowed){
 auto&s=*state_;if(current_pool==&s)return Result<CpuTiming>::failure({EDEADLK,"CPU_POOL","nested pool wait forbidden"});
 if(!n||!fn)return Result<CpuTiming>::failure({EINVAL,"CPU_POOL","empty task"});
 if(cancel&&cancel->cancelled.load())return Result<CpuTiming>::failure({ECANCELED,"CPU_POOL","cancelled before admission"});
 uint64_t token=0;bool claimed=false,published=false;CpuTiming timing;std::shared_ptr<State::Run>run;
 try{
  timing.begin_ns=stamp();run=std::make_shared<State::Run>();run->fn=fn;run->count=n;run->cancel=std::move(cancel);if(fegetenv(&run->environment))throw std::runtime_error("capture floating environment");
  {std::lock_guard<std::mutex>lock(s.mutex);
   if(s.init_error||s.closing)return Result<CpuTiming>::failure({s.init_error?s.init_error:ESHUTDOWN,"CPU_POOL","not ready"});
   if(s.inflight>s.max_tasks-s.count)return Result<CpuTiming>::failure({EAGAIN,"CPU_POOL","bounded CPU queue full"});
   if(borrowed)token=borrowed;else{auto reservation=s.budget->reserve(s.count);if(!reservation)return Result<CpuTiming>::failure(*reservation.error);token=*reservation.value;}
   auto claim=s.budget->claim(token,s.count);if(!claim){if(!borrowed)s.budget->release(token);token=0;return Result<CpuTiming>::failure(*claim.error);}claimed=true;
   size_t pushed=0;try{for(auto&w:s.workers){w->queue.push_back(run);++pushed;}}catch(...){for(size_t i=0;i<pushed;i++)s.workers[i]->queue.pop_back();throw;}
   run->remaining=s.count;s.inflight+=s.count;timing.workers=s.count;published=true;
  }
  s.ready.notify_all();
  {std::unique_lock<std::mutex>lock(run->mutex);run->done.wait(lock,[&]{return run->remaining==0;});}
  auto unclaimed=s.budget->unclaim(token);claimed=false;if(!unclaimed)return Result<CpuTiming>::failure(*unclaimed.error);
  if(!borrowed){auto released=s.budget->release(token);if(!released)return Result<CpuTiming>::failure(*released.error);}token=0;
  if(run->error)return Result<CpuTiming>::failure(*run->error);
  if(run->cancel&&run->cancel->cancelled.load())return Result<CpuTiming>::failure({ECANCELED,"CPU_POOL","cancelled before completion"});
  timing.end_ns=stamp();if(run->first_ns<timing.begin_ns||run->last_ns<run->first_ns||timing.end_ns<run->last_ns)throw std::runtime_error("nonmonotonic CPU timing");timing.total_ns=timing.end_ns-timing.begin_ns;timing.queue_wait_ns=run->first_ns-timing.begin_ns;timing.compute_ns=run->last_ns-run->first_ns;return Result<CpuTiming>::success(timing);
 }catch(const std::exception&e){
  // A failed wait is not proof that callbacks stopped using captured storage.
  // Drain before returning to the caller or releasing the reservation.
  if(published){std::unique_lock<std::mutex>lock(run->mutex);while(run->remaining)run->done.wait(lock);}
  if(token){if(claimed)s.budget->unclaim(token);if(!borrowed)s.budget->release(token);}
  return Result<CpuTiming>::failure({EIO,"CPU_POOL",e.what()});
 }
}
Result<void> CpuPool::shutdown(){
 auto&s=*state_;if(current_pool==&s)return Result<void>::failure({EDEADLK,"CPU_POOL","worker cannot join own pool"});
 std::lock_guard<std::mutex>join(s.join_mutex);if(s.join_attempted)return s.join_error?Result<void>::failure({s.join_error,"CPU_POOL","join quiescence unknown"}):Result<void>::success();
 {std::lock_guard<std::mutex>lock(s.mutex);s.closing=true;}s.ready.notify_all();
 for(uint32_t i=0;i<s.created;i++){int rc=pthread_join(s.workers[i]->thread,nullptr);if(rc&&!s.join_error)s.join_error=rc;}
 s.join_attempted=true;
 if(s.join_error){
  // A failed join is not proof of retirement. Retain worker state for process
  // lifetime; do not free contexts that an unknown worker could still access.
  s.exit_retention.retain(state_);return Result<void>::failure({s.join_error,"CPU_POOL","join quiescence unknown; state retained"});
 }
 if(s.slots_held){auto&slots=worker_slots();std::lock_guard<std::mutex>lock(slots.mutex);slots.used-=s.count;s.slots_held=false;}
 return Result<void>::success();
}
}

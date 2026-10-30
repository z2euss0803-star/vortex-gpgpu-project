#include <vxnn/cpu.hpp>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <thread>
#include <mutex>
#include <set>
#include <cfenv>
#include <condition_variable>
#include <pthread.h>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <chrono>
using namespace vxnn;
static std::atomic<int> creates{0},joins{0},fail_create{0},fail_join{0},forced_online{0};
extern "C" int __real_pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
extern "C" int __real_pthread_join(pthread_t,void**);
extern "C" long __real_sysconf(int);
extern "C" int __wrap_pthread_create(pthread_t*t,const pthread_attr_t*a,void*(*f)(void*),void*p){int n=++creates;if(fail_create.load()==n)return EAGAIN;return __real_pthread_create(t,a,f,p);}
extern "C" int __wrap_pthread_join(pthread_t t,void**p){int n=++joins;int rc=__real_pthread_join(t,p);return !rc&&fail_join.load()==n?EIO:rc;}
extern "C" long __wrap_sysconf(int key){if(key==_SC_NPROCESSORS_ONLN&&forced_online.load())return forced_online.load();return __real_sysconf(key);}
int main(int argc,char**argv){
 if(argc==2){
  if(!std::strcmp(argv[1],"create")){fail_create=2;CpuPool failed(4,32);assert(!failed.ready()&&failed.ready().error->code==EAGAIN);assert(creates==2&&joins==1);fail_create=0;CpuPool recovered(4,32);assert(recovered.ready());}
  else if(!std::strcmp(argv[1],"join")){CpuPool p(4,32);assert(p.ready());fail_join=1;auto result=p.shutdown();assert(!result&&result.error->code==EIO);assert(joins==4);CpuPool blocked(4,32);assert(!blocked.ready()&&blocked.ready().error->code==EAGAIN);}
  else if(!std::strcmp(argv[1],"few")){forced_online=2;CpuPool four(4,32);assert(!four.ready()&&four.ready().error->code==ENOTSUP&&creates==0);CpuPool two(2,32);assert(two.ready()&&two.workers()==2);}
  else return 2;
  puts("U08_CPU_FAILURE_OR_ONLINE_GATE_PASS_HOST_NOT_BOARD");return 0;
 }
 auto budget=std::make_shared<vxunified::PendingBudget>();CpuPool pool(4,32,budget);assert(pool.ready()&&pool.workers()==4);
 CpuPool duplicate(4,32,budget);assert(!duplicate.ready()&&duplicate.ready().error->code==EAGAIN);
 std::mutex mu;std::set<std::thread::id> threads;std::array<std::pair<size_t,size_t>,4> ranges{};
 // n=11 uses original n*i/4: [0,2),[2,5),[5,8),[8,11).
 auto run=pool.run_ranges(11,[&](size_t begin,size_t end){std::lock_guard<std::mutex>lock(mu);threads.insert(std::this_thread::get_id());unsigned i=begin==0?0:begin==2?1:begin==5?2:3;ranges[i]={begin,end};return Result<void>::success();});
 assert(run&&run.value->workers==4&&threads.size()==4);assert((ranges==std::array<std::pair<size_t,size_t>,4>{{{0,2},{2,5},{5,8},{8,11}}}));assert(budget->used()==0);
 auto first=threads;for(unsigned i=0;i<20;i++)assert(pool.run_ranges(11,[&](size_t,size_t){std::lock_guard<std::mutex>lock(mu);assert(first.count(std::this_thread::get_id()));return Result<void>::success();}));assert(threads.size()==4);
 assert(creates==4);assert(pool.run_ranges(4,[](size_t,size_t){assert(!fesetround(FE_DOWNWARD));return Result<void>::success();}));assert(pool.run_ranges(4,[](size_t,size_t){assert(fegetround()==FE_TONEAREST);return Result<void>::success();}));
 std::atomic<unsigned> retired{0};auto failure=pool.run_ranges(4,[&](size_t begin,size_t){++retired;return begin==1?Result<void>::failure({EDOM,"test","injected range failure"}):Result<void>::success();});assert(!failure&&failure.error->code==EDOM&&retired==4&&budget->used()==0);
 auto exception=pool.run_ranges(4,[](size_t begin,size_t){if(!begin)throw std::runtime_error("injected callback exception");return Result<void>::success();});assert(!exception&&exception.error->code==EIO&&budget->used()==0);
 auto nested=pool.run_ranges(4,[&](size_t begin,size_t){if(!begin){auto bad=pool.run_ranges(1,[](size_t,size_t){return Result<void>::success();});assert(!bad&&bad.error->code==EDEADLK);}return Result<void>::success();});assert(nested);
 auto full=budget->reserve(31);assert(full&&budget->used()==31);auto blocked=pool.run_ranges(4,[](size_t,size_t){assert(false);return Result<void>::success();});assert(!blocked&&blocked.error->code==EAGAIN);assert(budget->release(*full.value)&&budget->used()==0);assert(!budget->release(*full.value));
 auto cancel=std::make_shared<CpuCancel>();cancel->cancel();auto cancelled=pool.run_ranges(4,[](size_t,size_t){assert(false);return Result<void>::success();},cancel);assert(!cancelled&&cancelled.error->code==ECANCELED&&budget->used()==0);
 auto active_cancel=std::make_shared<CpuCancel>();std::condition_variable cv;unsigned entered=0;bool release=false;std::atomic<bool> cancelled_return{false};
 std::thread caller([&]{auto result=pool.run_ranges(4,[&](size_t,size_t){std::unique_lock<std::mutex>lock(mu);++entered;cv.notify_all();cv.wait(lock,[&]{return release;});return Result<void>::success();},active_cancel);cancelled_return=!result&&result.error->code==ECANCELED;});
 {std::unique_lock<std::mutex>lock(mu);cv.wait(lock,[&]{return entered==4;});active_cancel->cancel();release=true;cv.notify_all();}caller.join();assert(cancelled_return&&budget->used()==0&&creates==4);
 entered=0;release=false;std::atomic<bool> shutdown_done{false},drained{false};
 std::thread outstanding([&]{auto result=pool.run_ranges(4,[&](size_t,size_t){std::unique_lock<std::mutex>lock(mu);++entered;cv.notify_all();cv.wait(lock,[&]{return release;});return Result<void>::success();});drained=bool(result);});
 {std::unique_lock<std::mutex>lock(mu);cv.wait(lock,[&]{return entered==4;});}
 std::thread stopper([&]{assert(pool.shutdown());shutdown_done=true;});auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(pool.ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();assert(!pool.ready()&&!shutdown_done);
 {std::lock_guard<std::mutex>lock(mu);release=true;cv.notify_all();}outstanding.join();stopper.join();assert(drained&&shutdown_done&&budget->used()==0);assert(pool.shutdown());assert(!pool.run_ranges(4,[](size_t,size_t){return Result<void>::success();}));puts("U08_POOL_FOUR_RESIDENT_WORKERS_RANGES_BUDGET_CANCEL_PASS_NOT_BOARD");
}

#include <vxnn/cpu.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <pthread.h>
#include <unistd.h>
using namespace vxnn;
static std::atomic<bool>refuse{false},join_fault{false};static unsigned joins=0;
void*operator new(std::size_t n){if(refuse.load())throw std::bad_alloc();auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
extern "C" int __real_pthread_join(pthread_t,void**);
extern "C" int __wrap_pthread_join(pthread_t thread,void**result){int r=__real_pthread_join(thread,result);++joins;return !r&&join_fault.load()?EIO:r;}
void check(bool x,const char*why){if(!x){std::fprintf(stderr,"U10_POOL_EXIT_ASSERT %s\n",why);std::exit(1);}}
int main(int argc,char**argv){
 check(argc==2,"explicit test mode");std::string mode=argv[1];check(mode=="clean"||mode=="unknown"||mode=="explicit","known mode");
 std::set_terminate([](){const char text[]="U10_POOL_UNKNOWN_JOIN_DESTRUCTOR_TERMINATED_UNDER_ENOMEM\n";ssize_t n=write(2,text,sizeof(text)-1);(void)n;_exit(88);});
 auto budget=std::make_shared<vxunified::PendingBudget>();std::weak_ptr<vxunified::PendingBudget>weak=budget;auto pool=std::make_unique<CpuPool>(4,32,budget);check(bool(pool->ready()),"four real pthread workers ready");budget.reset();join_fault=mode!="clean";
 if(mode=="explicit"){auto first=pool->shutdown();check(!first&&first.error->code==EIO,"original explicit join error");}
 refuse=true;pool.reset();refuse=false;
 check(joins==4,"each real worker joined exactly once; injected return is not proof");check(weak.expired()==(mode=="clean"),"unknown join retains worker state and budget");
 {CpuPool next(4,32);auto ready=next.ready();check(bool(ready)==(mode=="clean"),"unknown worker slots are never guessed reusable");if(!ready)check(ready.error->code==EAGAIN,"only actual global worker-slot refusal satisfies quarantine");}
 std::printf("U10_POOL_ALLOCATION_FREE_EXIT_PASS_NOT_BOARD mode=%s\n",mode.c_str());
}

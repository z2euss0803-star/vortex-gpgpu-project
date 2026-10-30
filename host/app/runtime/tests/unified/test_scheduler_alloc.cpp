#include "scheduler_fixture.hpp"
#include <new>
static int fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0){fail_after=-1;throw std::bad_alloc();}if(fail_after>0)--fail_after;auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(){
 for(int fail=0;fail<8;fail++){
  auto budget=std::make_shared<PendingBudget>();Scheduler s(budget);auto r=request(std::string(128,'x').c_str(),1);value(s.submit(r),"allocation request");auto w=value(s.next_ready(),"allocation active");auto o=outcome(w);bool escaped=false;std::optional<vxnn::Result<void>> result;
  fail_after=fail;try{result.emplace(s.complete(o));}catch(const std::bad_alloc&){escaped=true;}fail_after=-1;
  require(!escaped,"completion allocation failure must not escape or lose retirement state");
  if(!*result){require(result->error->code==ENOMEM,"allocation error retained");require(s.stats().active_gpu==1&&budget->used()==1,"failed delivery retains active and token");ok(s.complete(o),"same completion recoverable after allocation failure");}
  require(s.stats().requests==0&&s.stats().active_gpu==0&&budget->used()==0,"terminal removes live queue entry without allocation");value(s.take_completion(),"completion preserved exactly once");require(!s.take_completion(),"no duplicate completion");
 }
 std::puts("U09_COMPLETION_ALLOCATION_RETIREMENT_TRANSACTION_PASS_NOT_BOARD");
}

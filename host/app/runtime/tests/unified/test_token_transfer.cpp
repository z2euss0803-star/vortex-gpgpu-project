#include "scheduler_fixture.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
int main(){
 auto b=std::make_shared<PendingBudget>();auto token=value(b->reserve(4),"reserve four");
 ok(b->claim(token,4),"claim original reservation");require(!b->claim(token,4),"no duplicate claim");require(!b->release(token),"cannot release executing ranges");ok(b->unclaim(token),"drained claim");require(!b->unclaim(token),"duplicate unclaim");require(!b->claim(token,1),"wrong count");
 vxnn::CpuPool pool(4,32,b);ok(pool.ready(),"shared pool");std::mutex m;std::condition_variable cv;int entered=0;bool release=false;
 std::thread runner([&]{auto r=pool.run_ranges_reserved(11,[&](size_t,size_t){std::unique_lock<std::mutex>l(m);++entered;cv.notify_all();cv.wait(l,[&]{return release;});return vxnn::Result<void>::success();},{},token);require(bool(r),"borrowed run succeeds");});
 {std::unique_lock<std::mutex>l(m);cv.wait(l,[&]{return entered==4;});}require(b->used()==4,"no second reservation during dispatch");require(!b->release(token),"retain until all worker retirement");require(!pool.run_ranges_reserved(11,[](size_t,size_t){return vxnn::Result<void>::success();},{},token),"duplicate borrower rejected");{std::lock_guard<std::mutex>l(m);release=true;}cv.notify_all();runner.join();require(b->used()==4,"scheduler retains original token after CPU execution");ok(b->release(token),"owner retires original once");require(b->used()==0,"all retired");
 std::puts("U09_CPU_TOKEN_TRANSFER_NO_DOUBLE_CHARGE_OR_EARLY_RELEASE_PASS_NOT_BOARD");
}

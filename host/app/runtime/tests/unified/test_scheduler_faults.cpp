#include "scheduler_fixture.hpp"
#include <stdexcept>
struct Throws:Source{Throws():Source(2){}vxnn::Result<WorkItem> get(uint64_t i)const override{if(i)throw std::runtime_error("lowering failure evidence");return Source::get(i);}};
struct FutureDependency:Source{FutureDependency():Source(1){}vxnn::Result<WorkItem> get(uint64_t i)const override{auto w=*Source::get(i).value;w.dependencies={0};return vxnn::Result<WorkItem>::success(w);}};
int main(){
 auto budget=std::make_shared<PendingBudget>();uint64_t now=100;Scheduler s(budget,[&]{return now;});
 auto bad=request("bad");bad.source=std::make_shared<FutureDependency>();require(!s.submit(bad)&&budget->used()==0,"bad dependency rejected before budget");auto expired=request("expired",1);expired.queue_deadline_ns=100;require(!s.submit(expired),"expired at admission");
 auto r=request("throws");r.source=std::make_shared<Throws>();value(s.submit(r),"throw source first accepted");auto w=value(s.next_ready(),"throw prefix");ok(s.complete(outcome(w)),"prefix retires");require(!s.next_ready(),"lowering exception no dispatch");require(value(s.status(r.request),"failed lowering status")==RequestStatus::FAILED,"throwing source terminalizes and does not retry");require(value(s.request_error(r.request),"recorded lowering error")==EIO,"exception error retained");require(budget->used()==0,"lowering failure no tokens");
 auto d=request("deadline",2);d.queue_deadline_ns=200;value(s.submit(d),"deadline admit");auto active=value(s.next_ready(),"deadline active");now=201;require(s.stats().active_gpu==1,"clock does not preempt");ok(s.complete(outcome(active)),"active still must retire");require(!s.next_ready(),"expired successor not sent");require(value(s.status(d.request),"expired status")==RequestStatus::FAILED&&value(s.request_error(d.request),"deadline error")==ETIMEDOUT,"deadline reason retained");
 auto wrong=request("epoch",1);wrong.request.boot_id="other-boot";require(!s.submit(wrong),"old or foreign boot rejected");
 auto fail=request("failure",2);fail.app=AppKind::P8;value(s.submit(fail),"P8 request kind admitted by scheduler fixture");auto f=value(s.next_ready(),"failure active");auto o=outcome(f);o.status=RequestStatus::FAILED;o.result=EIO;o.completion->result=EIO;ok(s.complete(o),"failure drains");require(!s.next_ready()&&value(s.status(fail.request),"failed request")==RequestStatus::FAILED,"no dependent after failure");require(value(s.request_error(fail.request),"runtime error")==EIO,"original runtime error");
 std::puts("U09_FAULT_DEPENDENCY_EPOCH_DEADLINE_EXCEPTION_ERRORS_RETAINED_PASS_NOT_BOARD");
}

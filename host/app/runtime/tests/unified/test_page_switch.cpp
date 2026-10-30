#include "display_fixture.hpp"
static void fault(int error,bool wrong_cookie){
 int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"fault producer");FakeDisplay f;auto o=f.ops();auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal()),"fault owner");
 get(d->present(p.lease,layout()),"fault pending");get(d->poll(),"fault submitted");good(s->reap_result(p.lease.producer),"fault client reaped");if(wrong_cookie)f.event_cookie=99;else f.receive_error=-error;auto r=d->poll();need(!r&&r.error->code==error,"actual first KMS error retained");
 need(d->snapshot().first_error->code==error&&s->stats().consumer_leases==1&&retired==0&&f.destroyed==0,"unknown scanout retains original source and buffers");need(!d->poll()&&!d->present(p.lease,layout()),"fault cannot continue or resubmit");
 good(d->restore(),"fault explicit restoration proves retirement");need(retired==1&&f.destroyed==2,"fault resources freed only after safe restore");
}
int main(){
 int retired=0;auto s=store(retired);auto a=producer("old"),b=producer("new");good(s->publish(a),"publish old");good(s->publish(b),"publish new");FakeDisplay f;auto o=f.ops();auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal()),"page switch owner");
 get(d->present(a.lease,layout()),"old submit");for(int i=0;i<3;i++)get(d->poll(),"old page event");good(s->reap_result(a.lease.producer),"old client reap");need(retired==0,"old front pinned");
 get(d->present(b.lease,layout()),"new submit");get(d->poll(),"new submitted");get(d->poll(),"new latched");need(retired==0,"old common lease not released before old retired event");auto shown=get(d->poll(),"old retired");need(shown.had_old_front&&shown.old_retired&&retired==1,"switch retires old, never waits new front retirement");
 good(s->reap_result(b.lease.producer),"new client reap");f.restore_error=-EIO;auto fail=d->restore();need(!fail&&fail.error->code==EIO&&f.destroyed==0&&retired==1&&s->stats().consumer_leases==1,"failed restore retains scanout and source");
 auto other=DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal());need(!other&&other.error->code==EBUSY,"failed owner cannot be stolen");f.restore_error=0;good(d->restore(),"explicit restore retry");need(f.destroyed==2&&retired==2&&d->snapshot().first_error->code==EIO,"first error preserved after safe restore");
 fault(EPROTO,true);fault(ENODEV,false);fault(ETIMEDOUT,false);
 puts("U18_PAGE_SWITCH_OLD_RETIREMENT_AND_RESTORE_FAILURE_PIN_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
}

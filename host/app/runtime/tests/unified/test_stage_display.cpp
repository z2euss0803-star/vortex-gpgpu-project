#include "display_fixture.hpp"
#include <type_traits>
static PublishedResult bound_source(){
 auto p=producer();ExecutionBinding b;b.app=p.lease.app;b.hardware=p.lease.hardware;b.model=p.lease.model;b.plan=p.lease.plan;b.bit[0]=4;b.xsa[0]=5;b.compatible[0]=6;b.input[0]=7;b.source[0]=8;b.provider_flags=1;b.device_generation=9;p.binding=b;return p;
}
template<class D,class=void>struct CookieChecks {
 static int run(){need(false,"U20 native application cookie must bind to actual common DP source hold");return 1;}
};
template<class D>struct CookieChecks<D,std::void_t<decltype(std::declval<D&>().present(std::declval<const ResultLease&>(),std::declval<const DisplayLayout&>(),uint64_t{}))>> {
 static int run(){
  int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"cookie source");FakeDisplay f;auto d=get(D::create(s,f.ops(),vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal()),"cookie owner");
  auto submitted=get(d->present(p.lease,layout(),9001),"actual original C engine with application cookie");need(submitted.cookie==9001,"application cookie returned without pretending raw OS cookie equals it");
  for(int i=0;i<3;++i){get(d->poll(),"actual raw events");}auto snapshot=d->snapshot();
  need(snapshot.receipt.cookie==9001&&snapshot.source_lease_token&&snapshot.source_lease_token!=9001&&snapshot.raw_events.size()==3,"logical application cookie, private lease and raw KMS cookie separately retained");
  need(get(s->take_result(p.lease.producer),"shown original producer").displayed,"actual binding accepted by common result store");
  auto other=producer("other");good(s->publish(other),"next page");auto collision=d->present(other.lease,layout(),9001);need(!collision&&collision.error->code==EEXIST&&!d->snapshot().pending,"cannot reuse still-live front logical cookie");
  good(s->reap_result(other.lease.producer),"rejected page release");good(s->reap_result(p.lease.producer),"front client release");good(d->restore(),"actual original restore");need(retired==2,"both confirmed sources retired once");
  puts("U20_APP_DP_COOKIE_SOURCE_HOLD_AND_RAW_OS_MAPPING_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
 }
};
static int stage(){
 int retired=0,public_saves=0,stage_saves=0;auto s=std::make_shared<ResultStore>([&](const PublishedResult&p){++public_saves;need(p.displayed,"public result keeps real staged display receipt");return Result<void>::success();},[&](const ResultLease&){++retired;return Result<void>::success();});auto p=bound_source();
 good(s->stage_display(p,[&](const PublishedResult&){++stage_saves;return Result<void>::success();}),"private checked output before display");FakeDisplay f;auto d=get(DisplayConsumer::create(s,f.ops(),vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal()),"single common display owner");
 auto submit=d->present(p.lease,layout());need(bool(submit),"U20 common display owner must read retained private stage");
 need(!s->take_result(p.lease.producer)&&!submit.value->presented&&public_saves==0,"source hold is not public request completion");
 for(int i=0;i<3;++i){get(d->poll(),"same actual old C display state machine");}need(stage_saves==2&&!s->take_result(p.lease.producer)&&public_saves==0&&s->stats().staged_records==1,"DP publication stays private until request commit");
 good(s->publish(p),"runtime equivalent final commit");need(get(s->take_result(p.lease.producer),"public after checked request commit").displayed&&public_saves==1,"final result keeps actual presentation");good(s->reap_result(p.lease.producer),"client release");need(retired==0,"front pinned");good(d->restore(),"native owner restoration");need(retired==1&&s->stats().records==0,"source and owner retired exactly once");
 puts("U20_PRIVATE_STAGE_THROUGH_ACTUAL_COMMON_C_DISPLAY_OWNER_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
}
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="cookie")return CookieChecks<DisplayConsumer>::run();return stage();}

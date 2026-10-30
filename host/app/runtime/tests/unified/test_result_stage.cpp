#include "display_fixture.hpp"
#include <type_traits>
template<class T,class=void>struct StageChecks {
 static int run(){need(false,"U20 checked output needs private pre-display staging before public completion");return 1;}
};
template<class T>struct StageChecks<T,std::void_t<decltype(std::declval<T&>().stage_display(std::declval<const PublishedResult&>(),std::declval<PersistResult>()))>> {
 static int run(){
  int public_saves=0,staged_saves=0,retired=0;bool fail_stage=true,fail_commit=false;std::optional<PublishedResult>last;
  T s([&](const PublishedResult&p){++public_saves;last=p;return fail_commit?Result<void>::failure({ENOSPC,"PUBLIC_JOURNAL","original commit full"}):Result<void>::success();},[&](const ResultLease&){++retired;return Result<void>::success();});
  auto p=producer();ExecutionBinding b;b.app=p.lease.app;b.hardware=p.lease.hardware;b.model=p.lease.model;b.plan=p.lease.plan;b.bit[0]=4;b.xsa[0]=5;b.compatible[0]=6;b.input[0]=7;b.source[0]=8;b.device_generation=9;b.provider_flags=1;p.binding=b;
  PersistResult stage=[&](const PublishedResult&q){++staged_saves;need(q.raw_legacy_json==p.raw_legacy_json&&!q.gpu_raw_cycles,"stage preserves numeric/history and offline null");return fail_stage?Result<void>::failure({ENOSPC,"STAGE_JOURNAL","original stage full"}):Result<void>::success();};
  auto failed=s.stage_display(p,stage);need(!failed&&failed.error->category=="STAGE_JOURNAL"&&s.stats().records==0&&retired==0,"failed private stage is not visible or retired");
  fail_stage=false;good(s.stage_display(p,stage),"durable private display source");good(s.stage_display(p,stage),"exact stage retry idempotent");need(staged_saves==2&&public_saves==0&&s.stats().client_holds==0&&!s.take_result(p.lease.producer),"staging never exposes completed client result");
  need(!s.reap_result(p.lease.producer)&&!s.retain_result(p.lease.producer,Consumer::CPU)&&!s.retain_result(p.lease.producer,Consumer::TEX),"client and texture cannot consume uncommitted display stage");
  auto hold=get(s.retain_result(p.lease.producer,Consumer::DP),"private DP lease");auto source=get(s.display_source(hold),"exact retained stage source");need(source.binding&&source.binding->source==b.source&&source.numeric==p.numeric,"shared source retains real admitted binding");
  auto forged=hold;++forged.token;need(!s.display_source(forged),"unknown DP token cannot read staged output");
  good(s.bind_display_cookie(hold,2),"bind application presentation cookie before scanout");need(!s.bind_display_cookie(hold,9002),"logical cookie binding immutable");
  auto next=get(s.retain_result(p.lease.producer,Consumer::DP),"next automatic DP lease");need(next.token!=2,"U20 automatic DP cookie must not collide with active application cookie");good(s.release_result(next,true),"unused staged DP lease released without request retirement");need(s.stats().staged_records==1&&retired==0,"dropping last optional stage hold never retires an active request");
  DisplayReceipt receipt;receipt.cookie=hold.token;receipt.presented=true;receipt.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;need(!s.mark_displayed(hold,receipt),"default token cannot replace bound app cookie");
  receipt.cookie=2;good(s.mark_displayed(hold,receipt),"stage DP proof recorded separately");need(staged_saves==3&&public_saves==0&&!s.take_result(p.lease.producer),"displayed stage is still not a public completed request");
  auto changed=p;changed.lease.artifacts[0].sha[0]^=1;auto collision=s.publish(changed);need(!collision&&collision.error->code==EEXIST,"publication must match exact staged output");
  fail_commit=true;auto full=s.publish(p);need(!full&&full.error->category=="PUBLIC_JOURNAL"&&!s.take_result(p.lease.producer)&&s.stats().consumer_leases==1&&retired==0,"commit persistence failure retains exact displayed stage and source");
  fail_commit=false;good(s.publish(p),"commit after native application checks and durable request state");auto visible=get(s.take_result(p.lease.producer),"now publicly completed");need(visible.displayed&&visible.numeric==p.numeric&&visible.raw_legacy_json==p.raw_legacy_json&&!visible.gpu_raw_cycles&&last->displayed,"commit preserves actual staged presentation and original failures");
  good(s.reap_result(p.lease.producer),"client release");need(retired==0,"current front pins source");good(s.release_result(hold,true),"actual old front retirement");need(retired==1&&s.stats().records==0,"single backing retirement after client and DP");
  puts("U20_PRIVATE_RESULT_STAGE_AND_ATOMIC_PUBLIC_COMMIT_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
 }
};
int main(){return StageChecks<ResultStore>::run();}

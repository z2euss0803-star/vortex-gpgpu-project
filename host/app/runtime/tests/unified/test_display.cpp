#include "display_fixture.hpp"
#include <thread>
#include <stdexcept>
static int rejection(){
 int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"publish rejection source");FakeDisplay f;auto o=f.ops();
 DisplayComposer invalid=[](const PublishedResult&,const DisplayLayout&){return Result<std::shared_ptr<const DisplayCanvas>>::failure({EBADMSG,"SOURCE","original source failure"});};
 auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,invalid,journal()),"rejection owner");auto r=d->present(p.lease,layout());need(!r&&r.error->code==EBADMSG,"source error preserved");
 need(s->stats().consumer_leases==0&&!d->snapshot().pending&&f.cookie==0,"rejected frame leaves no pending display or common lease");good(d->restore(),"rejected source safe restore");good(s->reap_result(p.lease.producer),"rejected source client reap");need(retired==1,"rejected source releases normally");puts("U18_DISPLAY_REJECTION_HAS_NO_PENDING_SOURCE_PASS_HOST_NOT_DP");return 0;
}
static int evidence(){
 int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"evidence source");FakeDisplay f;auto o=f.ops();DisplayJournal bad=[](const DisplayObservation&){return Result<void>::failure({ENOSPC,"ORIGINAL_JOURNAL","original display evidence full"});};
 auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),bad),"evidence owner");auto r=d->present(p.lease,layout());
 need(!r&&r.error->code==ENOSPC&&r.error->category=="ORIGINAL_JOURNAL"&&r.error->detail=="original display evidence full","first evidence error identity must be retained");need(d->snapshot().first_error->detail==r.error->detail&&s->stats().consumer_leases==1,"failed evidence retains pending source");
 good(d->restore(),"evidence failure explicit restore");good(s->reap_result(p.lease.producer),"evidence producer reap");need(retired==1,"no evidence retry or leaked known-retired source");puts("U18_DISPLAY_ORIGINAL_EVIDENCE_ERROR_PASS_HOST_NOT_DP");return 0;
}
static int allocation(){
 int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"allocation source");FakeDisplay f;auto o=f.ops();DisplayComposer fail=[](const PublishedResult&,const DisplayLayout&)->Result<std::shared_ptr<const DisplayCanvas>>{throw std::bad_alloc();};auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,fail,journal()),"allocation owner");auto r=d->present(p.lease,layout());need(!r&&r.error->code==ENOMEM,"allocation failure returned");need(!d->snapshot().pending&&s->stats().consumer_leases==0&&!f.cookie,"OOM before KMS submit leaves no false pending or lease");good(d->restore(),"allocation safe restore");good(s->reap_result(p.lease.producer),"allocation reap");need(retired==1,"allocation known-retired source released");puts("U18_PRE_SUBMIT_OOM_RELEASE_PASS_HOST_NOT_DP");return 0;
}
static int scope(){
 int retired=0;auto s=store(retired);auto p=producer();p.lease.scope=vxnn::EvidenceScope::OFFLINE;good(s->publish(p),"offline metadata fixture");FakeDisplay f;auto o=f.ops();auto d=get(DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal()),"scope owner");auto r=d->present(p.lease,layout());need(!r&&r.error->code==EPERM&&!f.cookie&&s->stats().consumer_leases==0,"fake KMS cannot mark offline producer as physical");auto lease=get(s->retain_result(p.lease.producer,Consumer::DP),"scope lease");DisplayReceipt forged;forged.cookie=lease.token;forged.presented=true;forged.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;auto mark=s->mark_displayed(lease,forged);need(!mark&&mark.error->code==EINVAL&&!s->take_result(p.lease.producer).value->displayed,"result store independently refuses fake presentation scope");good(s->release_result(lease,true),"scope lease release");good(d->restore(),"scope restore");good(s->reap_result(p.lease.producer),"scope reap");puts("U18_HOST_DISPLAY_SCOPE_UPGRADE_REFUSAL_PASS");return 0;
}
int main(int argc,char**argv){
 if(argc==2&&std::string(argv[1])=="reject")return rejection();
 if(argc==2&&std::string(argv[1])=="evidence")return evidence();
 if(argc==2&&std::string(argv[1])=="allocation")return allocation();
 if(argc==2&&std::string(argv[1])=="scope")return scope();
 int retired=0;auto s=store(retired);auto p=producer();good(s->publish(p),"publish original result");FakeDisplay f;auto o=f.ops();
 auto made=DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal());need(bool(made),"one KMS owner must be acquired");auto d=*made.value;
 auto second=DisplayConsumer::create(s,o,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal());need(!second&&second.error->code==EBUSY&&f.opened==1,"second owner refused before device open");
 std::atomic<unsigned>reads{0};std::vector<std::thread>readers;for(unsigned n=0;n<4;n++)readers.emplace_back([&](){for(unsigned i=0;i<500;i++){(void)d->snapshot();(void)s->stats();++reads;}});
 auto submitted=get(d->present(p.lease,layout()),"private scanout submit");need(!submitted.presented&&submitted.cookie&&s->stats().consumer_leases==1,"submit retains DP source without claiming presented");
 need(f.maps[1][0]==4&&f.maps[1][1]==17&&f.maps[1][2]==255&&f.maps[1][3]==255,"RGB to ARGB bytes hand reference");need(f.maps[1][16]==0&&f.maps[1][23]==0&&f.maps[1][24]==3,"actual pitch and padding");
 need(!d->present(p.lease,layout()),"one pending frame");get(d->poll(),"submitted raw event");f.receive_error=-EAGAIN;auto delay=d->poll();need(!delay&&delay.error->code==EAGAIN&&!d->snapshot().first_error,"delayed event not fault");f.receive_error=0;
 get(d->poll(),"new front latch event");need(!s->take_result(p.lease.producer).value->displayed,"latch alone cannot publish before old blank retirement");
 auto shown=get(d->poll(),"old blank retirement");need(shown.presented&&!shown.had_old_front&&shown.old_retired&&shown.clock_domain==vxnn::ClockDomain::HOST_MONOTONIC,"first frame succeeds without waiting current front");
 auto original=get(s->take_result(p.lease.producer),"retained result");need(original.displayed&&original.numeric==p.numeric&&original.raw_legacy_json==p.raw_legacy_json&&!original.gpu_raw_cycles,"display does not rewrite numeric or create GPU cycles");
 good(s->reap_result(p.lease.producer),"client release while displayed");need(retired==0,"current front source remains held");good(d->restore(),"original CRTC restored");need(retired==1&&f.destroyed==2&&f.closed==1&&s->stats().consumer_leases==0,"retire source only after restore");
 for(auto&t:readers){t.join();}
 need(reads==2000,"concurrent snapshots and source stats complete");
 puts("U18_DISPLAY_SINGLE_OWNER_PITCH_FIRST_BLANK_AND_SCOPE_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
}

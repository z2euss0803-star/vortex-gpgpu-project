#include "timing_fixture.hpp"
#include <vxunified/optimization.hpp>
#include <thread>
using namespace vxunified;using u19::need;
int main(int argc,char**argv){
 need(argc==2,"adversarial test mode");std::string mode=argv[1];
 if(mode=="zero-parent"){
  auto e=u19::event(1,0,1);e.event.parent_event=0;need(!serialize_event(e),"serialized parent event zero must be rejected");puts("U19_SERIALIZED_ZERO_PARENT_REFUSAL_PASS_HOST gpu_raw_cycles=null");return 0;
 }
 if(mode=="constant-alias"){
  auto c=u19::context();CacheKey key{c.hardware,c.program,c.model,c.plan,u19::digest(11),7};BufferRef ref{101,2,0,128,VXL_V2_READ,7};auto cache=ConstantResidency::create(c.hardware,1024,4);need(bool(cache)&&bool((*cache.value)->remember(key,ref,key.constant)),"alias fixture");auto other=key;other.constant=u19::digest(12);need(!(*cache.value)->remember(other,ref,other.constant),"same immutable reference cannot claim contradictory content");other=key;other.model=u19::digest(13);need(bool((*cache.value)->remember(other,ref,other.constant)),"same bytes may be shared under separate complete model keys");puts("U19_CONTRADICTORY_IDENTICAL_CONSTANT_RANGE_REFUSAL_PASS_HOST gpu_raw_cycles=null");return 0;
 }
 if(mode=="duplicate-counter"){
  auto first=u19::event(1,1,3,TimingPhase::ACK);first.context.counter=CounterKind::GPU_JOB;first.event.scope=vxnn::EvidenceScope::REAL_BOARD;first.event.clock_domain=vxnn::ClockDomain::LINUX_MONOTONIC;first.event.cycles_valid=true;first.event.raw_cycles=35;
  auto second=first;second.event.event_id=2;second.event.start_ns=4;second.event.end_ns=6;
  need(!vxunified::summarize_timing({first,second}),"same GPU job raw counter must not be added twice");
  second.event.job_id="job1";need(bool(vxunified::summarize_timing({first,second})),"distinct GPU job records admitted");second.event.status=-EIO;second.context.execution_error=vxnn::Error{-EIO,"ORIGINAL_GPU","incomplete counter"};second.event.cycles_valid=false;second.event.raw_cycles.reset();auto partial=vxunified::summarize_timing({first,second});need(bool(partial)&&!partial.value->gpu_raw_cycles&&!partial.value->gpu_cycles_complete,"incomplete GPU counters must not yield partial total");
  puts("U19_DUPLICATE_GPU_COUNTER_AND_INCOMPLETE_TOTAL_REFUSAL_PASS_SYNTHETIC_NOT_GPU gpu_raw_cycles=null");return 0;
 }
 if(mode=="pending-lifetime"){
  std::weak_ptr<unsigned> alive;
  {auto marker=std::make_shared<unsigned>(17);alive=marker;auto e=u19::event(1,1,2);auto sink=UnifiedEvidenceSink::create(e.context,[marker](const std::string&){return Result<void>::failure({ENOSPC,"ORIGINAL_PENDING","first full store"});});need(bool(sink),"pending sink creation");marker.reset();need(!(*sink.value)->emit(e.event)&&(*sink.value)->stats().pending,"pending bytes observed");}
  need(!alive.expired(),"pending exact evidence and writer must survive consumer destruction");puts("U19_PENDING_EXACT_EVENT_EXIT_RETENTION_PASS_HOST_NOT_GPU gpu_raw_cycles=null");return 0;
 }
 if(mode=="constant-concurrent"){
  auto c=u19::context();CacheKey key{c.hardware,c.program,c.model,c.plan,u19::digest(11),7};BufferRef ref{101,2,0,128,VXL_V2_READ,7};auto cache=ConstantResidency::create(c.hardware,128,1);need(bool(cache)&&bool((*cache.value)->remember(key,ref,key.constant)),"constant boundary fixture");
  for(unsigned i=0;i<14;i++){auto k=key;switch(i){case 0:k.hardware.hardware_contract=u19::digest(90);break;case 1:k.hardware.bit=u19::digest(90);break;case 2:k.hardware.xsa=u19::digest(90);break;case 3:k.hardware.compatible=u19::digest(90);break;case 4:k.hardware.device_generation++;break;case 5:k.program.code=u19::digest(90);break;case 6:k.program.schema=u19::digest(90);break;case 7:k.program.logical_profile++;break;case 8:k.program.logical_program++;break;case 9:k.program.namespace_id++;break;case 10:k.model=u19::digest(90);break;case 11:k.plan=u19::digest(90);break;case 12:k.constant=u19::digest(90);break;default:k.epoch++;break;}need(!(*cache.value)->retain(k),"each frozen key component prevents stale reuse");}
  std::vector<std::thread>workers;for(unsigned t=0;t<4;t++){workers.emplace_back([&]{for(unsigned i=0;i<100;i++){auto lease=(*cache.value)->retain(key);need(bool(lease)&&bool((*cache.value)->release(*lease.value)),"concurrent exact constant token");}});}for(auto&t:workers){t.join();}need((*cache.value)->stats().pins==0&&(*cache.value)->stats().hits==400,"all borrowed constant pins retired");
  std::vector<ConstantLease>pins;for(unsigned i=0;i<64;i++){auto lease=(*cache.value)->retain(key);need(bool(lease),"bounded64 lease fill");pins.push_back(*lease.value);}need(!(*cache.value)->retain(key),"lease65 refused");for(const auto&l:pins){need(bool((*cache.value)->release(l)),"all exact bounded pins released");}need(bool((*cache.value)->retire(key,true)),"known retirement after all pins");
  auto models=ConstantResidency::create(c.hardware,1024,4);need(bool(models),"two-model cache");for(unsigned i=0;i<3;i++){auto k=key;k.model=u19::digest(30+i);k.constant=u19::digest(40+i);auto model_ref=ref;model_ref.handle+=i;auto r=(*models.value)->remember(k,model_ref,k.constant);need(bool(r)==(i<2),"two resident models enforced");}need((*models.value)->stats().entries==2,"third model cannot mutate cache");
  puts("U19_FULL_CONSTANT_KEY_LIMITS_AND_CONCURRENT_PINS_PASS_HOST_NO_DEVICE_MEMORY gpu_raw_cycles=null");return 0;
 }
 need(mode=="weighted","weighted mode");OptimizationBaseline b{u19::digest(1),u19::digest(2),u19::digest(3),u19::digest(4),u19::digest(5)};OptimizationPolicy p;p.weighted_dispatch=true;
 std::vector<ReadyTicket>q{{AppKind::NN,1},{AppKind::IMAGE,10},{AppKind::NN,2},{AppKind::NN,3},{AppKind::IMAGE,11},{AppKind::NN,4}};auto r=plan_dispatch_order(p,b,u19::context().hardware,q,{2,1,1});need(bool(r),"bounded nonpreemptive weighted ordering required");need(*r.value==std::vector<uint64_t>({1,2,10,3,4,11}),"weighted FIFO hand reference");p={};auto baseline=plan_dispatch_order(p,b,u19::context().hardware,q);need(bool(baseline)&&*baseline.value==std::vector<uint64_t>({1,10,2,3,11,4}),"allfalse baseline order unchanged");p.weighted_dispatch=true;need(!plan_dispatch_order(p,b,u19::context().hardware,{{AppKind::P8,1}}),"P8 ticket cannot enter S8 policy candidate");need(!plan_dispatch_order(p,b,u19::context().hardware,q,{0,1,1}),"zero quota refused");need(!plan_dispatch_order(p,b,u19::context().hardware,{{AppKind::NN,1},{AppKind::IMAGE,1}}),"duplicate ready cookie refused");
 puts("U19_WEIGHTED_READY_ORDER_ONLY_NO_PREEMPTION_OR_GPU_PARALLELISM_PASS_HOST gpu_raw_cycles=null");return 0;
}

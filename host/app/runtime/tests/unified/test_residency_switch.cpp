#include <vxunified/residency.hpp>
#include "scheduler_fixture.hpp"
#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <fstream>
#include <cstring>
#include <thread>
#include <atomic>
using namespace vxunified;
namespace {
BackendIdentity hw(){BackendIdentity b;b.hardware_contract.fill(1);b.bit.fill(2);b.xsa.fill(3);b.compatible.fill(4);b.provider_flags=1;b.device_generation=7;return b;}
struct Backend:UnifiedBackend {
 BackendIdentity h=hw();int loads=0,frees=0;bool load_error=false,free_error=false,wrong_epoch=false;Digest requested{};
 Result<BackendIdentity>identity()override{return Result<BackendIdentity>::success(h);}
 Result<DeviceCaps>capabilities()override{DeviceCaps c;c.contexts=1;c.max_programs=16;return Result<DeviceCaps>::success(c);}
 Result<ProgramRef>program_load(const Digest&d)override{++loads;requested=d;if(load_error)return Result<ProgramRef>::failure({EIO,"TEST","original ambiguous load"});ProgramRef p;p.handle=loads;p.program_generation=100+loads;p.device_generation=wrong_epoch?8:7;p.program_sha=d;return Result<ProgramRef>::success(p);}
 Result<void>release_program(ProgramRef)override{++frees;return free_error?Result<void>::failure({EBUSY,"TEST","kernel still pinned"}):Result<void>::success();}
 Result<BufferRef>allocate(uint64_t,vxnn::AddressDomain,uint32_t)override{std::abort();}
 Result<void>copy(BufferRef,vxnn::Bytes)override{std::abort();}Result<void>copy(BufferRef,vxnn::MutableBytes)override{std::abort();}Result<void>copy(BufferRef,BufferRef)override{std::abort();}
 Result<void>release_buffer(BufferRef)override{std::abort();}Result<uint64_t>submit(const JobDesc&)override{std::abort();}Result<void>wait(uint64_t,uint64_t)override{std::abort();}Result<Completion>read_completion(uint64_t)override{std::abort();}Result<void>ack_reap(uint64_t)override{std::abort();}
};
}

namespace {
struct Proof {WorkOutcome outcome;vxl_v2_completion detail{};};
Proof proof(const ResidencyLease&lease,uint64_t seq){
 Proof p;auto&o=p.outcome;o.request=lease.request;o.node=1;o.work_index=seq-1;o.status=RequestStatus::COMPLETED;o.retired=o.acknowledged=true;
 o.completion.emplace();auto&c=*o.completion;c.sequence=seq;c.cookie=seq+100;c.wire_cookie=c.cookie;c.status=3;
 auto&d=p.detail;d.sequence=seq;d.cookie=c.cookie;d.device_generation=lease.program.device_generation;d.program_handle=lease.program.handle;d.program_generation=lease.program.program_generation;d.state=3;d.status=2;d.core_mask=15;d.submit_ns=10;d.start_ns=20;d.done_seen_ns=30;d.ack_ns=40;d.complete_ns=50;return p;
}
}
int main(int argc,char**argv){
 require(argc==2,"two-family actual C catalog");std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t>raw((std::istreambuf_iterator<char>(file)),{});Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw(),sum),"typed catalog");
 ProgramKey a,b;for(unsigned i=0;i<2;i++){vxl_v2_catalog_entry e{};memcpy(&e,raw.data()+64+512*i,512);auto&k=i?b:a;k.namespace_id=e.namespace_id;k.logical_profile=e.logical_profile;k.logical_program=e.logical_program;memcpy(k.code.data(),e.code,32);memcpy(k.schema.data(),e.schema,32);memcpy(k.hardware_contract.data(),e.hardware_contract,32);}
 RequestKey one{"boot","client","one"},two{"boot","client","two"};auto backend=std::make_shared<Backend>();auto pool=value(ProgramResidency::create(catalog,backend),"one pool");auto first=value(pool->acquire(one,a),"initial program");
 require(!value(pool->ready(one,b),"old readiness unchanged")&&!value(pool->ready(two,b),"other request blocked"),"ordinary admission never implies shader switch");
 require(value(pool->ready_switch(first,b),"owned transition metadata"),"same live owner may plan shader replacement");require(backend->loads==1&&!backend->frees,"readiness no I/O");
 auto stale=first;stale.serial++;require(!pool->ready_switch(stale,b),"stale lease cannot plan transition");
 for(unsigned mode=0;mode<16;mode++){
  auto p=proof(first,1);switch(mode){
   case 0:p.outcome.acknowledged=false;break;case 1:p.outcome.retired=false;break;case 2:p.detail.program_handle++;break;case 3:p.detail.program_generation++;break;case 4:p.detail.device_generation++;break;case 5:p.detail.sequence++;break;case 6:p.detail.cookie++;break;case 7:p.detail.state=4;break;case 8:p.detail.result=-EIO;break;case 9:p.detail.errors=1;break;case 10:p.detail.pending_w=1;break;case 11:p.detail.core_mask=1;break;case 12:p.detail.ack_ns=1;break;case 13:p.outcome.request=two;break;case 14:p.outcome.completion.reset();break;case 15:p.outcome.status=RequestStatus::FAILED;break;
  }
  require(!pool->switch_after_ack(first,b,p.outcome,p.detail),"invalid original ACK proof rejected");require(backend->loads==1&&!backend->frees&&!pool->stats().faulted,"proof refusal never evicts live backing");
 }
 auto p=proof(first,1);auto second=value(pool->switch_after_ack(first,b,p.outcome,p.detail),"valid ACK switch");require(second.serial>first.serial&&backend->loads==2&&backend->frees==1&&second.program.program_sha==value(catalog->resolve(b),"next binding").entry_id&&pool->stats().owned,"DATA owner remains reserved across replacement");
 require(!pool->release(first,true)&&!pool->switch_after_ack(first,a,p.outcome,p.detail)&&!value(pool->ready(two,a),"request exclusion after switch"),"old lease/proof cannot release or reuse new program");
 auto next=proof(second,2);auto third=value(pool->switch_after_ack(second,a,next.outcome,next.detail),"switch back after next ACK");require(third.serial>second.serial&&backend->loads==3&&backend->frees==2,"exact one eviction and load per actual change");
 std::atomic<int>errors{0};std::vector<std::thread>threads;for(unsigned t=0;t<4;t++)threads.emplace_back([&]{for(unsigned i=0;i<1000;i++){auto r=pool->ready_switch(third,b);if(!r||!*r.value)errors++;}});for(auto&t:threads)t.join();require(!errors&&backend->loads==3&&backend->frees==2,"transition readiness synchronized metadata only");
 ok(pool->release(third,true),"whole DATA retirement remains separate");ok(pool->shutdown(),"cached program shutdown");require(backend->frees==3,"no leaked cached reference");
 for(unsigned mode=0;mode<3;mode++){
  auto io=std::make_shared<Backend>();auto owned=value(ProgramResidency::create(catalog,io),"fault pool");auto lease=value(owned->acquire(one,a),"fault initial");auto acked=proof(lease,1);if(mode==0)io->free_error=true;if(mode==1)io->load_error=true;if(mode==2)io->wrong_epoch=true;
  require(!owned->switch_after_ack(lease,b,acked.outcome,acked.detail)&&owned->stats().faulted&&owned->stats().owned,"release/load/epoch uncertainty retains owner");int loads=io->loads,frees=io->frees;require(!owned->switch_after_ack(lease,b,acked.outcome,acked.detail)&&!owned->shutdown()&&loads==io->loads&&frees==io->frees,"no retry or forced eviction after ambiguous transition");
 }
 puts("U14_RESIDENCY_ACK_SWITCH_PROOF16_FAILURES_DATA_OWNER_AND_FAULT_RETENTION_PASS_HOST_NO_GPU gpu_raw_cycles=null");
}

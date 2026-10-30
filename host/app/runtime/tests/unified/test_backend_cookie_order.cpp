#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "scheduler_fixture.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <thread>
using namespace vxunified;
namespace {
struct Transport:IoctlTransport {
 void*bridge;uint64_t last_wire=0;unsigned submits=0;bool corrupt_read=false;vxl_v2_job last_job{};vxl_v2_completion original{};
 explicit Transport(const char*p):bridge(backend_bridge_create(p)){}
 ~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long cmd,void*p)override{
  if(cmd==VXL_V2_SUBMIT_BATCH){auto&r=*static_cast<vxl_compute_v2_request*>(p);auto&j=*reinterpret_cast<vxl_v2_job*>(r.user_ptr);last_wire=j.cookie;last_job=j;++submits;}
  auto rc=backend_bridge_call(bridge,cmd,p);
  if(!rc&&cmd==VXL_V2_COMPLETE_REAP){auto&r=*static_cast<vxl_compute_v2_request*>(p);if(r.flags==VXL_V2_READ){auto&d=*reinterpret_cast<vxl_v2_completion*>(r.user_ptr);original=d;if(corrupt_read){++d.cookie;++r.cookie;}}}
  return rc;
 }
 uint64_t now_ns()override{return 100;}
};
struct Gated:Source {
 std::string name;std::string&owner;unsigned space;JobDesc admitted;
 Gated(const char*n,uint64_t count,std::string&o,unsigned ns,JobDesc j):Source(count),name(n),owner(o),space(ns),admitted(std::move(j)){}
 Result<bool>ready(uint64_t)const override{return Result<bool>::success(owner.empty()||owner==name);}
 Result<WorkItem>get(uint64_t i)const override{
  auto item=*Source::get(i).value;auto job=admitted;job.cookie=(uint64_t(space)<<32)+i+1;item.operation=job;return Result<WorkItem>::success(std::move(item));
 }
};
}
int main(int n,char**v){
 require(n==2,"catalog fixture");Transport io(v[1]);ComputeV2Backend backend(io);
 value(backend.capabilities(),"actual C CAPS");Digest id{};backend_bridge_entry(io.bridge,id.data());auto program=value(backend.program_load(id),"admitted program");
 auto args=value(backend.allocate(4,vxnn::AddressDomain::PL,3),"args");uint32_t one=1;ok(backend.copy(args,vxnn::Bytes{reinterpret_cast<uint8_t*>(&one),4}),"args upload");const auto baseline=backend_bridge_live(io.bridge);
 JobDesc base;base.program_handle=program.handle;base.program_generation=program.program_generation;base.device_generation=program.device_generation;base.deadline_ns=1000;base.args=args;
 std::string owner;Scheduler scheduler(std::make_shared<PendingBudget>());
 auto make=[&](const char*name,uint64_t count,unsigned ns){auto r=request(name,count);r.source=std::make_shared<Gated>(name,count,owner,ns,base);return r;};
 auto a=make("A",3,1),b=make("B",2,2),c=make("C",2,3);value(scheduler.submit(a),"admit A");
 const char*expected[]={"A","A","A","C","C","B","B"};uint64_t previous_wire=0;
 for(unsigned i=0;i<7;++i){
  if(i==1)value(scheduler.submit(b),"prepare B while A holds residency");
  if(i==2)value(scheduler.submit(c),"prepare C while A holds residency");
  auto w=value(scheduler.next_ready(),"actual scheduler dispatch");require(w.request.request_id==expected[i],"whole-request residency rotates A A A C C B B");
  owner=w.request.request_id;auto job=std::get<GPUJobDesc>(w.operation);auto submitted=backend.submit(job);
  if(!submitted)std::cerr<<"COOKIE_ORDER_FAILURE step="<<i<<" request="<<owner<<" logical="<<job.cookie<<" errno="<<submitted.error->code<<"\n";
  require(bool(submitted),"newly dispatched request must not be rejected as a replay");
  require(io.last_wire>previous_wire,"wire cookie increases in actual submit order");previous_wire=io.last_wire;
  require(backend_bridge_live(io.bridge)==baseline+1,"one retained ARGS staging allocation");
  auto duplicate=backend.submit(job);require(!duplicate&&duplicate.error->code==-EALREADY,"live logical identity cannot resubmit");
  backend_bridge_finish(io.bridge,1);ok(backend.wait(job.cookie,1000),"wait uses logical cookie after reordering");
  auto raw=value(backend.detail(*submitted.value,job.cookie),"original raw completion");auto complete=value(backend.read_completion(*submitted.value),"logical completion");
  require(raw.cookie==io.last_wire&&raw.sequence==*submitted.value,"raw driver identity is not rewritten");
  require(complete.cookie==job.cookie&&complete.wire_cookie==raw.cookie&&complete.sequence==raw.sequence&&complete.raw_cycles==raw.elapsed_raw,"logical scheduler identity retains exact original counter");
  ok(backend.ack_reap(*submitted.value),"ACK binds original wire sequence/cookie");require(backend_bridge_live(io.bridge)==baseline,"staging released only after ACK");
  if(i==0){auto calls=io.submits;auto replay=backend.submit(job);require(!replay&&replay.error->code==-ESTALE&&io.submits==calls,"retired logical cookie must never acquire a fresh wire identity");}
  auto out=outcome(w);out.completion=complete;ok(scheduler.complete(out),"logical outcome completes original request");value(scheduler.take_completion(),"consume host protocol completion");
  if((owner=="A"&&w.work_index==2)||(owner!="A"&&w.work_index==1))owner.clear();
 }
 require(backend_bridge_starts(io.bridge)==7&&io.submits==7,"no hidden retries or extra starts");
 // Ambiguous acceptance, original raw bytes and ACK copy-out faults must bind
 // the reordered logical identity without any hidden second submission.
 auto job=base;job.cookie=2;backend_bridge_fault_submit(io.bridge,1);auto ambiguous=backend.submit(job);
 require(!ambiguous&&ambiguous.error->code==-EFAULT&&backend_bridge_starts(io.bridge)==8,"acceptance copy-out fault retained");const auto accepted_wire=value(backend.submitted_wire_cookie(2),"ambiguous accepted wire identity");require(accepted_wire>previous_wire,"ambiguous wire reservation never rolls back");
 require(!backend.submit(job)&&io.submits==8,"ambiguous logical cookie cannot retry");backend_bridge_fault_submit(io.bridge,0);ok(backend.wait(2,1000),"recover by logical identity only");auto recovered=value(backend.detail(0,2),"recover original sequence");
 io.corrupt_read=true;auto corrupt=backend.read_completion(recovered.sequence);require(!corrupt&&corrupt.error->code==-EPROTO,"unknown wire cookie never maps to logical work");io.corrupt_read=false;
 auto exact=value(backend.detail(recovered.sequence,2),"explicit read-only recovery");require(!std::memcmp(&exact,&io.original,256)&&exact.cookie==accepted_wire,"all raw kernel bytes remain intact");
 auto mapped=value(backend.read_completion(exact.sequence),"mapped logical recovery");require(mapped.cookie==2&&mapped.wire_cookie==accepted_wire,"recovery retains both identities");backend_bridge_fault_ack(io.bridge,1);auto ack_fault=backend.ack_reap(exact.sequence);require(!ack_fault&&ack_fault.error->code==-EFAULT&&backend_bridge_live(io.bridge)==baseline+1,"ambiguous ACK retains backing");backend_bridge_fault_ack(io.bridge,0);ok(backend.ack_reap(exact.sequence),"explicit ambiguous ACK proof");require(!backend.submitted_wire_cookie(2)&&backend_bridge_live(io.bridge)==baseline,"retired map/staging released together");
 // A replay of the original wire descriptor is still refused by the unchanged
 // actual C driver, even though user-space logical numbering is independent.
 auto replay=io.last_job;vxl_compute_v2_request request{};request.major=2;request.size=128;request.device_generation=program.device_generation;request.length=1;request.payload_bytes=512;request.user_ptr=reinterpret_cast<uintptr_t>(&replay);const auto starts=backend_bridge_starts(io.bridge);
 require(io.call(VXL_V2_SUBMIT_BATCH,&request)==-ESTALE&&backend_bridge_starts(io.bridge)==starts,"kernel replay guard is not bypassed or weakened");
 // Concurrent preparation/submission shares one serialized owner allocator.
 struct Pending{uint64_t logical=0,sequence=0;};Pending pending[8];std::thread workers[8];
 for(unsigned i=0;i<8;++i)workers[i]=std::thread([&,i]{auto j=base;j.cookie=100-i;pending[i]={j.cookie,value(backend.submit(j),"concurrent legitimate submit")};});
 for(auto&t:workers){t.join();}
 std::sort(std::begin(pending),std::end(pending),[](const Pending&a,const Pending&b){return a.sequence<b.sequence;});
 uint64_t wire_before=accepted_wire;for(const auto&p:pending){ok(backend.wait(p.logical,1000),"ordered active completion by logical identity");auto d=value(backend.detail(p.sequence,p.logical),"concurrent binding raw read");auto c=value(backend.read_completion(p.sequence),"concurrent binding logical read");require(d.cookie>wire_before&&c.cookie==p.logical&&c.wire_cookie==d.cookie,"concurrent wire identities never collide or swap");wire_before=d.cookie;ok(backend.ack_reap(p.sequence),"concurrent staging retirement");}
 require(backend_bridge_live(io.bridge)==baseline,"concurrent staging fully retired");
 // Exhaustion refuses before allocation or ioctl; no wraparound, reset or
 // epoch invention can make an old wire identity appear new.
 job.cookie=UINT64_MAX;auto last=value(backend.submit(job),"last representable wire cookie");ok(backend.wait(job.cookie,1000),"last cookie wait");ok(backend.ack_reap(last),"last cookie retirement");auto count=io.submits;job.cookie=3;auto exhausted=backend.submit(job);require(!exhausted&&exhausted.error->code==-EOVERFLOW&&io.submits==count&&backend_bridge_live(io.bridge)==baseline,"wire exhaustion has no new side effects");
 ok(backend.release_buffer(args),"retire args");ok(backend.release_program(program),"retire program");
 puts("U25_ACTUAL_SCHEDULER_AND_BACKEND_COOKIE_ORDER_PASS_HOST_ONLY gpu_raw_cycles=null");
}

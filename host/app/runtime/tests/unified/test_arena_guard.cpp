#include <vxnn/arena_guard.hpp>
#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "../nnrt/fixture.hpp"
#include <cstring>
using namespace vxnn;
namespace {
// C decoder/memory implementation is real; only OS/hardware are host fixtures.
struct Transport:vxunified::IoctlTransport {
 void*bridge;unsigned copies=0;bool fail_copy=false,throw_copy=false;
 explicit Transport(const char*p):bridge(backend_bridge_create(p)){}
 ~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{if(c==VXL_V2_BUFFER_COPY){++copies;if(throw_copy)throw std::runtime_error("original guard COPY exception");if(fail_copy)return -EIO;}return backend_bridge_call(bridge,c,p);}
 uint64_t now_ns()override{return 100;}
};
PlannedTensor tensor(uint32_t id,uint64_t first,uint64_t last,uint64_t bytes){PlannedTensor t;t.desc.id=id;t.desc.bytes=bytes;t.desc.alignment=64;t.desc.domain=AddressDomain::PL;t.desc.access=3;t.first=first;t.last=last;return t;}
struct Fixture {
 Transport io;vxunified::ComputeV2Backend backend;Plan plan;ArenaPlan arena;std::vector<ArenaAllocation>owned;ArenaBindings bindings;std::shared_ptr<ArenaGuards>guards;
 explicit Fixture(const char*catalog):io(catalog),backend(io){
  plan.caps=graph_caps();plan.policy.hardware.device_generation=1;plan.plan_id=content_digest("GUARD_LIFETIMES_LITERAL_GRAPH");plan.tensors={tensor(1,0,1,128),tensor(2,0,0,64),tensor(3,1,2,64),tensor(4,2,3,8),tensor(5,3,3,64)};plan.tensors[1].desc.constant=true;plan.tensors[1].desc.access=1;plan.inputs={1};plan.outputs={5};
  for(unsigned i=0;i<3;i++){PlannedNode n;n.node.id=11*(i+1);n.node.inputs={i==0?1u:i==1?3u:4u,2};n.node.outputs={i+3};n.workspace_bytes=i==2?0:64;plan.nodes.push_back(n);}
  MemoryBudget b;b.pl_bytes=1<<20;auto p=plan_arenas(plan,b);require(bool(p),"fixture plan");arena=*p.value;
  for(const auto&o:arena.objects){auto a=backend.allocate(o.bytes,o.domain,3);require(bool(a),"real C BUFFER_CREATE");owned.push_back({o.slot,o.domain,*a.value});}
  auto refs=bind_arenas(arena,owned,1);require(bool(refs),"guarded subreferences");bindings=*refs.value;
 }
 void start(){auto g=ArenaGuards::create(plan,arena,owned,1,backend);require(bool(g),"guard runtime must bind actual owned handles");guards=*g.value;require(bool(guards->initialize()),"seed initial live input/constant guards");}
 BufferRef guard(TensorId id,bool tail){for(const auto&v:arena.tensors)if(v.tensor.tensor==id){auto b=owned.at(v.arena-1).backing;b.offset=tail?v.guard_after:v.guard_before;b.bytes=64;return b;}std::abort();}
 void corrupt(TensorId id,bool tail){auto ref=guard(id,tail);uint8_t byte=0xff;ref.bytes=1;require(bool(backend.copy(ref,Bytes{&byte,1})),"inject guard corruption through controlled COPY");}
 ~Fixture(){guards.reset();for(const auto&a:owned)require(bool(backend.release_buffer(a.backing)),"guard metadata never frees/retries device ownership");}
};
}
int main(int n,char**v){if(n!=3)return 2;Fixture f(v[1]);std::string mode=v[2];
 if(mode.compare(0,5,"batch")==0)f.plan.nodes[0].job_count=3;
 if(mode=="invalid"){
  auto stale=f.plan;stale.policy.hardware.device_generation=2;require(!ArenaGuards::create(stale,f.arena,f.owned,1,f.backend),"planned epoch must match actual owned epoch");auto bad=f.arena;bad.plan_id[0]^=1;require(!ArenaGuards::create(f.plan,bad,f.owned,1,f.backend),"different plan rejected without COPY");bad=f.arena;bad.tensors[0].tensor.bytes--;require(!ArenaGuards::create(f.plan,bad,f.owned,1,f.backend),"short tensor view rejected");bad=f.arena;bad.tensors[3]=bad.tensors[0];bad.tensors[3].tensor.tensor=4;bad.tensors[3].tensor.bytes=8;bad.tensors[3].guard_after=bad.tensors[3].object_offset+8;auto p=f.plan;p.tensors[3].first=1;require(!ArenaGuards::create(p,bad,f.owned,1,f.backend),"simultaneously live guard/data overlap rejected");require(!f.io.copies,"invalid binding never touches device");
  auto overflow=f.plan;overflow.nodes[0].job_count=UINT64_MAX;require(!ArenaGuards::create(overflow,f.arena,f.owned,1,f.backend)&&!f.io.copies,"unbounded subjob count refused before COPY");
 }else{
  f.start();if(mode.compare(0,5,"batch")==0){
   require(bool(f.guards->begin_node(11)),"first multi-job node");
   if(mode=="batch-early"){
    auto copies=f.io.copies;require(!f.guards->verify_retired_node(11)&&f.guards->receipt().completed_nodes==0&&f.io.copies==copies,"incomplete phase cannot retire or release dependencies");
   }else{
    require(bool(f.guards->verify_retired_subjob(11)),"first subjob ACK verifies guards without retiring node");require(f.guards->receipt().active&&f.guards->receipt().completed_nodes==0&&f.guards->receipt().retired_batches==1,"logical lifetimes stay active");
    if(mode=="batch-corrupt"){
     f.corrupt(3,true);require(!f.guards->verify_retired_subjob(11)&&f.guards->receipt().fault&&f.guards->receipt().completed_nodes==0&&f.guards->receipt().retired_batches==1,"intermediate guard fault retains original evidence without counting retirement");auto copies=f.io.copies;require(!f.guards->verify_retired_node(11)&&!f.guards->begin_node(22)&&f.io.copies==copies,"no retry or lifetime advance after failure");
    }else{
     require(bool(f.guards->verify_retired_subjob(11)),"second subjob ACK");
     if(mode=="batch-extra")require(!f.guards->verify_retired_subjob(11)&&f.guards->receipt().completed_nodes==0,"last subjob must retire through final phase gate");
     else{
      require(mode=="batch-flow"&&bool(f.guards->verify_retired_node(11)),"third and final subjob retires logical node");require(f.guards->receipt().completed_nodes==1&&f.guards->receipt().retired_batches==3&&!f.guards->receipt().active,"phase advances exactly once");
      require(bool(f.guards->begin_node(22))&&bool(f.guards->verify_retired_node(22))&&bool(f.guards->begin_node(33))&&bool(f.guards->verify_retired_node(33))&&bool(f.guards->verify_final()),"following CPU/single-job lifetimes unchanged");require(f.guards->receipt().completed_nodes==3&&f.guards->receipt().retired_batches==5,"all actual guard retirement observations counted");
     }
    }
   }
  }else if(mode=="flow"){
   std::array<uint8_t,128> input;input.fill(0x19);require(bool(f.backend.copy(f.bindings.tensors.at(1),Bytes{input.data(),input.size()})),"upload input");
   require(f.arena.tensors[0].object_offset==f.arena.tensors[3].object_offset&&f.arena.tensors[3].guard_after<f.arena.tensors[0].object_offset+128,"future small trailing guard overlaps old live input data");
   require(bool(f.guards->begin_node(11)),"begin1");std::array<uint8_t,128>read{};require(bool(f.backend.copy(f.bindings.tensors.at(1),MutableBytes{read.data(),read.size()}))&&read==input,"future guards must not corrupt still-live input");
   require(bool(f.guards->verify_retired_node(11)),"retired1");require(bool(f.guards->begin_node(22)),"reuse after last use");require(bool(f.guards->verify_retired_node(22)),"retired2");require(bool(f.guards->begin_node(33)),"begin3");require(bool(f.guards->verify_retired_node(33)),"retired3");require(bool(f.guards->verify_final()),"final output and constants verified");const auto&r=f.guards->receipt();require(r.final_verified&&r.completed_nodes==3&&r.reads&&r.writes&&r.seed_ns&&r.check_ns&&!r.first_error,"lifecycle and measured Linux guard phases");
  }else if(mode=="corrupt-before"||mode=="corrupt-after"||mode=="constant"||mode=="copy-error"||mode=="copy-throw"){
   require(bool(f.guards->begin_node(11)),"first node");bool tail=mode!="corrupt-before";TensorId tensor_id=mode=="constant"?2:3;
   if(mode=="copy-error")f.io.fail_copy=true;else if(mode=="copy-throw")f.io.throw_copy=true;else f.corrupt(tensor_id,tail);
   auto failed=f.guards->verify_retired_node(11);require(!failed,"new corruption/COPY error blocks completion");auto before=f.io.copies;require(!f.guards->begin_node(22)&&!f.guards->verify_final()&&f.io.copies==before,"sticky first error forbids new node or automatic I/O retry");auto&r=f.guards->receipt();require(r.first_error&&r.completed_nodes==0&&!r.final_verified,"failure retains active node before dependency release");
   if(mode=="copy-error")require(failed.error->code==-EIO&&!r.fault,"preserve raw negative COPY errno");else if(mode=="copy-throw")require(failed.error->code==EIO&&failed.error->detail=="original guard COPY exception","preserve throwing phase evidence");else require(failed.error->code==EBADMSG&&r.fault&&r.fault->node==11&&r.fault->tensor==tensor_id&&r.fault->trailing==tail&&r.fault->observed[0]==0xff&&r.fault->expected!=r.fault->observed&&r.fault->guard.bytes==64,"retain exact raw64-byte expected/observed, reference, node and side");
  }else if(mode=="order"){
   require(!f.guards->begin_node(22),"skip node rejected");require(!f.guards->begin_node(11),"protocol first error remains sticky");
  }else return 2;
 }
 std::printf("U12_ARENA_GUARD_%s_PASS_REAL_C_COPY_NOT_GPU gpu_raw_cycles=null\n",mode.c_str());
}

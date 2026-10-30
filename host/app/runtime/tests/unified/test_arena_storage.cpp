#include <vxnn/arena_storage.hpp>
#include <vxnn/arena_guard.hpp>
#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "../nnrt/fixture.hpp"
using namespace vxnn;
namespace {
struct Transport:vxunified::IoctlTransport {
 void*bridge;unsigned creates=0,releases=0;std::string fault;
 explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{
  if(c==VXL_V2_BUFFER_CREATE){++creates;if(fault=="nomem")return -ENOMEM;auto rc=backend_bridge_call(bridge,c,p);if(fault=="create-throw")throw std::runtime_error("original allocation return exception");if(!rc&&fault=="bad-epoch"){auto*r=static_cast<vxl_compute_v2_request*>(p);reinterpret_cast<vxl_v2_buffer_info*>(r->user_ptr)->ref.device_generation++;}return rc;}
  if(c==VXL_V2_BUFFER_RELEASE){++releases;if(fault=="release-error")return -EFAULT;}
  return backend_bridge_call(bridge,c,p);
 }
 uint64_t now_ns()override{return 100;}
};
struct Fixture {
 Transport io;vxunified::ComputeV2Backend backend;Plan plan;ArenaPlan arena;uint64_t baseline;
 explicit Fixture(const char*catalog):io(catalog),backend(io){
  baseline=backend_bridge_live(io.bridge);auto caps=backend.capabilities();require(bool(caps),"actual C CAPS");plan.caps=*caps.value;auto identity=backend.identity();require(bool(identity),"actual C provider declaration");plan.policy.hardware=*identity.value;plan.plan_id=content_digest("SINGLE_ARENA_STORAGE_PROTOCOL");
  PlannedTensor a;a.desc.id=1;a.desc.domain=AddressDomain::PL;a.desc.bytes=64;a.desc.alignment=64;a.desc.access=3;a.first=0;a.last=1;auto b=a;b.desc.id=2;b.first=b.last=1;plan.tensors={a,b};plan.inputs={1};plan.outputs={2};PlannedNode n;n.node.id=7;n.node.inputs={1};n.node.outputs={2};n.workspace_bytes=64;plan.nodes={n};MemoryBudget m;m.pl_bytes=plan.caps.pl_bytes;m.pl_reserved=8<<20;auto p=plan_arenas(plan,m);require(bool(p),"plan single arena with internal-prefix candidate");arena=*p.value;
 }
};
}
int main(int n,char**v){if(n!=3)return 2;Fixture f(v[1]);std::string mode=v[2];
 if(mode=="invalid"){
  auto other=f.plan;other.policy.hardware.provider_flags=2;require(!ArenaStorage::create(other,f.arena,1,f.backend),"P8 provider cannot use S8 NN materialization contract");auto bad=f.arena;bad.objects.push_back(bad.objects[0]);require(!ArenaStorage::create(f.plan,bad,1,f.backend),"multiple physical arena/held-hole semantics need separate provider");bad=f.arena;bad.objects[0].offset=0;require(!ArenaStorage::create(f.plan,bad,1,f.backend),"cannot drop required internal low prefix");require(!ArenaStorage::create(f.plan,f.arena,2,f.backend),"stale plan epoch");bad=f.arena;bad.objects[0].bytes=UINT64_MAX;require(!ArenaStorage::create(f.plan,bad,1,f.backend),"prefix extent overflow");require(!f.io.creates,"invalid metadata never allocates");
 }else{
  auto created=ArenaStorage::create(f.plan,f.arena,1,f.backend);require(bool(created),"materializer metadata must prepare real single-object allocation");auto lease=*created.value;
  if(mode=="nomem"||mode=="create-throw"||mode=="bad-epoch"){
   f.io.fault=mode;auto result=lease->allocate();require(!result&&lease->receipt().phase==ArenaStoragePhase::FAULTED&&!lease->bindings(),"failed allocation never makes usable bindings");auto before=f.io.creates;require(!lease->allocate()&&!lease->release()&&f.io.creates==before&&!f.io.releases,"no automatic retry/free of ambiguous fd-owned state");if(mode=="nomem")require(result.error->code==-ENOMEM&&backend_bridge_live(f.io.bridge)==f.baseline,"raw ENOMEM and no fixture allocation");else require(backend_bridge_live(f.io.bridge)==f.baseline+1,"unknown returned allocation remains fd-owned until owner lifecycle");
  }else{
   require(bool(lease->allocate()),"actual C BUFFER_CREATE");const auto&r=lease->receipt();require(r.phase==ArenaStoragePhase::READY&&r.root&&r.prefix_bytes==(8u<<20)&&r.backing_bytes==(8u<<20)+f.arena.objects[0].bytes&&r.root->bytes==r.backing_bytes&&r.allocate_ns,"prefix accounted within exactly one owned object");require(backend_bridge_live(f.io.bridge)==f.baseline+1&&f.io.creates==1,"no low-hole fill objects");const auto&layout=lease->layout();require(layout.objects[0].offset==0&&layout.objects[0].bytes==r.backing_bytes,"root-relative materialized layout");for(const auto&b:lease->bindings()->tensors){require(b.second.offset>=(8u<<20)&&b.second.handle==r.root->handle&&b.second.device_generation==1,"all tensor views stay beyond unused prefix in same epoch");}
   if(mode=="flow"){
    auto guard=ArenaGuards::create(f.plan,layout,lease->allocations(),1,f.backend);require(bool(guard)&&bool((*guard.value)->initialize())&&bool((*guard.value)->begin_node(7)),"rebased layout uses existing real guard COPY");uint8_t values[64];std::fill(values,values+64,0x1d);require(bool(f.backend.copy(lease->bindings()->tensors.at(2),Bytes{values,64})),"real output subrange");require(bool((*guard.value)->verify_retired_node(7))&&bool((*guard.value)->verify_final()),"root-relative guards after body copy");require(bool(lease->release())&&lease->receipt().phase==ArenaStoragePhase::RETIRED&&!lease->receipt().root&&!lease->bindings()&&lease->allocations().empty()&&backend_bridge_live(f.io.bridge)==f.baseline,"explicit release invalidates every stale borrowed view");auto calls=f.io.releases;require(!lease->release()&&f.io.releases==calls,"double release never repeats ioctl");
   }else if(mode=="release-error"){
    f.io.fault=mode;auto failed=lease->release();require(!failed&&failed.error->code==-EFAULT&&lease->receipt().root&&lease->receipt().phase==ArenaStoragePhase::FAULTED,"ambiguous release retains root and raw error");auto calls=f.io.releases;require(!lease->release()&&f.io.releases==calls&&backend_bridge_live(f.io.bridge)==f.baseline+1,"no automatic ambiguous release retry");
   }else return 2;
  }
 }
 std::printf("U12_ARENA_STORAGE_%s_PASS_REAL_C_NOT_DMA_OR_GPU gpu_raw_cycles=null\n",mode.c_str());
}

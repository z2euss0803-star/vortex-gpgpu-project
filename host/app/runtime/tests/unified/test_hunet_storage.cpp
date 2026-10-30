#include <vxnn/model_hunet.hpp>
#include <vxnn/arena_guard.hpp>
#include <vxnn/arena_storage.hpp>
#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
namespace {
void need(bool yes,const char*s){if(!yes){std::fprintf(stderr,"U12_FULL_STORAGE_ASSERT %s\n",s);std::exit(71);}}
struct Transport:vxunified::IoctlTransport {
 void*bridge;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return backend_bridge_call(bridge,c,p);}uint64_t now_ns()override{return 100;}
};
}
int main(int argc,char**argv){
 need(argc==3,"actual package and C catalog fixture");auto loaded=load_package(Path(argv[1]));need(bool(loaded),"secure frozen package");const auto&package=*loaded.value;
 const auto&raw=*package.assets().at("binding.json").content;auto binding=json::parse(std::string(raw.begin(),raw.end()));auto*b=binding.get();
 HunetFamily family{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"implementation_sha256")),json::digest(json::member(b,"schema_sha256"))};OperatorRegistry registry;need(bool(register_hunet_operators(registry,family)),"actual operator registry");
 vxunified::BackendIdentity hw;auto*h=json::member(b,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=1;hw.device_generation=1;
 Transport io(argv[2]);vxunified::ComputeV2Backend backend(io);auto actual_caps=backend.capabilities();need(bool(actual_caps),"actual C CAPS physical pool domains");DeviceCaps caps=*actual_caps.value;
 auto prepared=prepare_hunet(package,registry,caps,hw);need(bool(prepared),"actual116-node plan");const auto&plan=*prepared.value;MemoryBudget budget;budget.pl_bytes=caps.pl_bytes;budget.ps_bytes=caps.ps_bytes;budget.pl_reserved=8<<20;
 auto planned=plan_arenas(plan,budget);need(bool(planned),"complete guarded memory plan");auto storage_result=ArenaStorage::create(plan,*planned.value,1,backend);need(bool(storage_result),"full graph storage metadata");auto storage=*storage_result.value;const auto baseline_live=backend_bridge_live(io.bridge);need(bool(storage->allocate()),"actual full data root with internal prefix");const auto&arena=storage->layout();const auto&owned=storage->allocations();
 auto resolved=bind_arenas(arena,owned,1);need(bool(resolved),"actual C handles, not invented references");const auto&refs=*resolved.value;
 auto created=ArenaGuards::create(plan,arena,owned,1,backend);if(!created)std::fprintf(stderr,"GUARD_CREATE %s\n",created.error->detail.c_str());need(bool(created),"actual graph guard metadata");auto guards=*created.value;need(bool(guards->initialize()),"initial lifetime seeding");
 std::map<TensorId,const TensorDesc*> desc;std::map<TensorId,uint8_t> markers;
 for(const auto&t:plan.tensors){desc.emplace(t.desc.id,&t.desc);if(t.desc.constant){const auto&bytes=*package.assets().at(t.desc.asset).content;auto ref=refs.tensors.at(t.desc.id);ref.access=2;need(bool(backend.copy(ref,Bytes{bytes.data(),bytes.size()})),"upload original frozen constants");}}
 const auto&input=*package.assets().at("input.f32").content;need(bool(backend.copy(refs.tensors.at(plan.inputs[0]),Bytes{input.data(),input.size()})),"upload frozen ROI0");
 auto inspect=[&](TensorId id){const auto&t=*desc.at(id);std::vector<uint8_t> got(t.bytes);need(bool(backend.copy(refs.tensors.at(id),MutableBytes{got.data(),got.size()})),"read actual bound view");if(t.constant){need(got==*package.assets().at(t.asset).content,"constant bytes survive all overlapping lifetime transitions");}else if(id==plan.inputs[0]){need(got==input,"initial data not overwritten by future guard");}else{need(markers.count(id)&&std::all_of(got.begin(),got.end(),[&](uint8_t x){return x==markers.at(id);}),"producer marker survives until actual consumer");}};
 unsigned cpu=0,gpu=0;
 for(size_t i=0;i<plan.nodes.size();i++){
  const auto&n=plan.nodes[i];need(bool(guards->begin_node(n.node.id)),"guarded node begin");for(auto id:n.node.inputs)inspect(id);
  // Deliberately no math or hardware execution. Marker writes exercise actual
  // subrange COPY and lifetime reuse; they cannot become inference evidence.
  for(auto id:n.node.outputs){uint8_t marker=uint8_t(1+i%251);markers[id]=marker;std::vector<uint8_t>bytes(desc.at(id)->bytes,marker);need(bool(backend.copy(refs.tensors.at(id),Bytes{bytes.data(),bytes.size()})),"write host marker output");}
  if(n.workspace_bytes){std::vector<uint8_t>bytes(n.workspace_bytes,0x35);need(bool(backend.copy(refs.workspaces.at(n.node.id),Bytes{bytes.data(),bytes.size()})),"full workspace extent does not hit live guard");}
  if(n.job_count)++gpu;else ++cpu;need(bool(guards->verify_retired_node(n.node.id)),"post-marker verification, no GPU/CPU retirement claimed");
 }
 need(bool(guards->verify_final()),"all graph guards and final views");for(const auto&t:plan.tensors)if(t.desc.constant)inspect(t.desc.id);inspect(plan.outputs[0]);
 const auto&r=guards->receipt();need(r.completed_nodes==116&&cpu==13&&gpu==103&&r.final_verified&&!r.first_error&&backend_bridge_starts(io.bridge)==0,"116 logical marker transitions only, zero GPU starts");
 guards.reset();need(bool(storage->release()),"explicit storage release after final guard");need(!storage->bindings()&&!storage->receipt().root,"retired root invalidates all native bindings");need(backend_bridge_live(io.bridge)==baseline_live,"no leaked test objects");
 std::printf("U12_HUNET_STORAGE_MARKER_REPLAY_PASS_NOT_MATH nodes=116 cpu_boundaries=13 gpu_nodes=103 guards_writes=%llu guards_reads=%llu seed_ns=%llu check_ns=%llu gpu_raw_cycles=null\n",(unsigned long long)r.writes,(unsigned long long)r.reads,(unsigned long long)r.seed_ns,(unsigned long long)r.check_ns);
}

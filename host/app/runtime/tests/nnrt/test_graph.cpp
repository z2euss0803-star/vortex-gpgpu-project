#include "fixture.hpp"
#include <vxunified/work.hpp>
int main(int argc,char**argv){assert(argc==2);OperatorRegistry r;require(bool(r.add(identity_spec())),"graph registry ready");auto run=[&](const char*n,OptimizationPolicy policy=graph_policy()){auto p=load_package(Path(argv[1])/n);require(bool(p),n);return prepare_graph(*p.value,r,graph_caps(),policy);};
 auto p=run("valid");require(bool(p)&&p.value->nodes.size()==1&&p.value->tensors.size()==2&&p.value->total_jobs==1,"static identity graph");
 for(const char*n:{"cycle","bad-shape","bad-layout","bytes-overflow","job-budget","evidence-budget","unregistered","unbound-program","dynamic-unlisted","unbound-constant","unreadable-input"})require(!run(n),n);
 auto dyn=graph_policy();dyn.shape_profile="small";require(bool(run("dynamic-listed",dyn)),"finite dynamic shape profile");dyn.shape_profile="unknown";require(!run("dynamic-listed",dyn),"unknown dynamic profile");
 auto large=run("large-mosaic");require(bool(large)&&large.value->nodes.size()==1&&large.value->total_jobs==10000000,"large graph remains compact");
 auto sorted=run("unsorted");require(bool(sorted)&&sorted.value->nodes.at(0).node.id==10&&sorted.value->nodes.at(1).node.id==11,"topological order");
 auto constant=run("bound-constant");require(bool(constant)&&constant.value->tensors[0].desc.asset=="weights.bin"&&constant.value->tensors[0].last==UINT64_MAX,"constant binds exact immutable asset and residency");
 auto hw=graph_policy();hw.hardware.bit[0]^=1;require(!run("valid",hw),"wrong bit rejected before open");
 auto opt=graph_policy();opt.fusion=true;require(!run("valid",opt),"future optimization not silently enabled");
 auto epoch=graph_policy();epoch.hardware.device_generation=2;auto fresh=run("valid",epoch);require(bool(fresh)&&fresh.value->plan_id==p.value->plan_id,"plan content identity independent of epoch");
 auto caps=graph_caps();caps.pl_bytes/=2;auto package=load_package(Path(argv[1])/"valid");auto smaller=prepare_graph(*package.value,r,caps,graph_policy());require(bool(smaller)&&smaller.value->plan_id!=p.value->plan_id,"capacity contract participates in plan identity");
 vxunified::WorkRequest request;require(request.input_bytes==0,"unified types compiled");puts("U07_GRAPH_PASS_BEFORE_DEVICE_OPEN_GPU_CYCLES_NULL");}

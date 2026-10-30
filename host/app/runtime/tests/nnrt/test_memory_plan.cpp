#include "fixture.hpp"
inline PlannedTensor t(uint32_t id,uint64_t first,uint64_t last,uint64_t bytes=64){PlannedTensor v;v.desc.id=id;v.desc.bytes=bytes;v.desc.alignment=64;v.desc.domain=AddressDomain::PL;v.first=first;v.last=last;return v;}
int main(){Plan p;p.caps=graph_caps();p.resident_bytes=128;p.tensors={t(1,0,1),t(2,2,3),t(3,1,4)};MemoryBudget b;b.pl_bytes=1024;b.ps_bytes=1024;
 auto m=plan_memory(p,b);require(bool(m)&&m.value->bindings.size()==3,"valid memory plan");require(m.value->bindings[0].offset==m.value->bindings[1].offset,"nonoverlap reuse");require(m.value->bindings[2].offset!=m.value->bindings[0].offset,"live interval cannot alias");
 b.held={{AddressDomain::PL,0,64,false},{AddressDomain::PL,64,64,false}};auto held=plan_memory(p,b);require(bool(held)&&held.value->bindings[0].offset>=128,"GPU DP cancelled unretired held ranges excluded");
 b.held[0].retired=true;b.held[1].retired=true;auto freed=plan_memory(p,b);require(bool(freed)&&freed.value->bindings[0].offset==0,"retired ranges reusable");
 b.held.clear();b.pl_bytes=64;require(!plan_memory(p,b),"capacity before models count");b.pl_bytes=1024;b.resident_models=3;require(!plan_memory(p,b),"two resident models maximum");b.resident_models=2;b.existing_objects=63;require(!plan_memory(p,b),"64 objects includes existing objects");b.existing_objects=0;b.cpu_bytes=127;require(!plan_memory(p,b),"CPU budget includes immutable package");b.cpu_bytes=1024;
 p.tensors[0].desc.bytes=UINT64_MAX;require(!plan_memory(p,b),"align overflow rejected");p.tensors={t(1,0,1),t(2,2,3)};p.tensors[0].desc.constant=true;auto c=plan_memory(p,b);require(bool(c)&&c.value->bindings[0].offset!=c.value->bindings[1].offset,"resident constant cannot be reused");
 p.tensors={t(1,0,1)};p.tensors[0].desc.domain=static_cast<AddressDomain>(42);require(!plan_memory(p,b),"invalid address domain rejected");
 p.tensors={t(1,0,1)};p.nodes.resize(1);p.nodes[0].node.id=10;p.nodes[0].workspace_bytes=1024;require(!plan_memory(p,b),"workspace participates in capacity");p.nodes.clear();
 puts("U07_MEMORY_PLAN_PASS_OFFLINE_GPU_CYCLES_NULL");}

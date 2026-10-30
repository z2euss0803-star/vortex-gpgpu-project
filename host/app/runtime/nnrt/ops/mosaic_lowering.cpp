#include <vxnn/mosaic_lowering.hpp>
#include <vxnn/mosaic_ro64_program.hpp>
#include <vxnn/mosaic_program_family.hpp>
#include "device/vxnn/mosaic_wire_reuse_v4.h"
#include "../package/json.hpp"
#include <cerrno>
namespace vxnn {
Result<MosaicLowered> lower_mosaic_batch(const Plan&p,const MosaicFamily&f,const ArenaStorage&s,NodeId id,uint64_t batch){try{
 const auto&r=s.receipt();json::check(r.phase==ArenaStoragePhase::READY&&!r.first_error&&r.root&&s.bindings()&&r.epoch&&p.plan_id==s.layout().plan_id&&p.policy.hardware.device_generation==r.epoch&&vxunified::s8_provider(p.policy.hardware),"READY S8 owned arena/plan and matching epoch required");
 json::check(p.programs.size()==1,"one controlled Mosaic library");const auto&key=p.programs[0];json::check(key.namespace_id==1&&key.logical_profile==1&&key.logical_program==65568&&key.code==f.program_sha&&mosaic_program_code_matches(f.schema_sha,f.program_sha)&&key.schema==f.schema_sha&&digest_hex(f.readonly_sha)=="30e0e46cb30629f4a7de447405225e208b37f15aaf2be23603bbe4cb975b5643"&&bool(vxunified::admit_program(p.policy.hardware,key)),"exact new CODE/read-only/schema/provider identity");
 const PlannedNode*pn=nullptr;std::set<NodeId>nodes;for(const auto&n:p.nodes){json::check(nodes.insert(n.node.id).second,"unique nodes");if(n.node.id==id)pn=&n;}json::check(pn&&id,"unknown node");const auto&n=pn->node;uint32_t op=0;for(uint32_t i=1;i<=8;i++){auto k=mosaic_operator_key(i,f);if(!(k<n.key)&&!(n.key<k))op=i;}json::check(op&&n.program_requirements==std::vector<Digest>{f.program_sha},"native operator/source/program binding");
 const auto&bound=*s.bindings();const auto&root=*r.root;auto valid=[&](const BufferRef&v){return v.handle==root.handle&&v.object_generation==root.object_generation&&v.device_generation==r.epoch&&v.bytes&&v.offset>=r.prefix_bytes&&!(v.offset%4)&&v.offset<=root.bytes&&v.bytes<=root.bytes-v.offset&&(v.access==1||v.access==2||v.access==3);};
 std::map<TensorId,const TensorDesc*>desc;for(const auto&t:p.tensors){json::check(desc.emplace(t.desc.id,&t.desc).second,"unique tensor");auto b=bound.tensors.find(t.desc.id);json::check(b!=bound.tensors.end()&&valid(b->second)&&b->second.bytes==t.desc.bytes&&b->second.access==t.desc.access,"tensor storage extent/access differs");}
 std::vector<TensorDesc>inputs;std::set<TensorId>ids;for(auto tid:n.inputs){json::check(desc.count(tid)&&ids.insert(tid).second,"unique known input");inputs.push_back(*desc.at(tid));}auto described=inspect_mosaic_operator(op,inputs,n.parameters,p.caps,f);if(!described)return Result<MosaicLowered>::failure(*described.error);const auto&d=*described.value;
 json::check(pn->job_count==d.jobs&&batch<d.jobs&&pn->workspace_bytes==d.args.extent[VXM_WORK]&&bound.workspaces.count(id)&&n.outputs.size()==d.outputs.size(),"exact bounded job/workspace/output contract");
 MosaicLowered out;out.node=id;out.batch=batch;out.args=d.args;const uint64_t first=batch*d.budget;json::check(first<d.shape.items,"batch multiplication bounded before narrowing");out.args.first=uint32_t(first);out.args.count=uint32_t(std::min<uint64_t>(d.budget,d.shape.items-first));
 const uint32_t access[8]={1,2,3,1,1,3,3,3};const auto work=bound.workspaces.at(id);json::check(valid(work)&&work.access==3&&work.bytes==pn->workspace_bytes&&!(work.offset%64),"full owned per-core workspace plus dummy prefix");
 json::check(p.readonly_placeholder_bytes==64&&bound.readonly_placeholder&&valid(*bound.readonly_placeholder)&&bound.readonly_placeholder->bytes==64&&bound.readonly_placeholder->access==1,"independent lifetime readonly placeholder required");
 for(unsigned role=0;role<8;role++){out.references[role]=access[role]==1?*bound.readonly_placeholder:work;out.references[role].bytes=64;out.references[role].access=access[role];}out.references[VXM_WORK]=work;
 auto assign=[&](uint32_t role,TensorId tid){auto ref=bound.tensors.at(tid);json::check((ref.access&access[role])==access[role],"declared tensor permits exact sealed role access");ref.access=access[role];out.references[role]=ref;};
 for(size_t i=0;i<n.inputs.size();i++)assign(d.inputs[i].role,n.inputs[i]);
 for(size_t i=0;i<n.outputs.size();i++){
  auto tid=n.outputs[i];json::check(desc.count(tid)&&ids.insert(tid).second,"distinct known SSA output");const auto&t=*desc.at(tid);const auto&e=d.outputs[i];json::check(!t.constant&&t.dtype==e.type&&t.layout==e.layout&&t.shape==e.shape&&t.bytes==e.bytes&&t.domain==AddressDomain::PL&&(t.access&2)&&t.alignment>=p.caps.alignment&&!(t.alignment&(t.alignment-1)),"typed output storage differs");assign(e.role,tid);
 }
 for(unsigned role=0;role<8;role++)json::check(valid(out.references[role])&&out.references[role].bytes==out.args.extent[role],"exact canonical ref extent");
 auto overlaps=[](const BufferRef&a,const BufferRef&b){return a.handle==b.handle&&a.offset<b.offset+b.bytes&&b.offset<a.offset+a.bytes;};
 for(unsigned i=0;i<8;i++)if(d.shape.used_mask&(1u<<i))for(unsigned j=0;j<i;j++)if(d.shape.used_mask&(1u<<j))json::check(!overlaps(out.references[i],out.references[j]),"active input/output/scratch intervals must be disjoint");
 for(unsigned i=0;i<8;i++)if(access[i]==1)for(unsigned j=0;j<8;j++)if(access[j]&2)json::check(!overlaps(out.references[i],out.references[j]),"ALL readonly slots disjoint from every writable slot");
 VxnnMosaicShape checked{};json::check(!(out.args.abi==4?vxnn_mosaic_validate_reuse_v4(&out.args,false,&checked):out.args.abi==3?vxnn_mosaic_validate_coop_v3(&out.args,false,&checked):vxnn_mosaic_validate_fused(&out.args,false,&checked)),"canonical unrelocated packet");return Result<MosaicLowered>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<MosaicLowered>::failure({ENOMEM,"MOSAIC_LOWER","metadata allocation"});}catch(const std::exception&e){return Result<MosaicLowered>::failure({EINVAL,"MOSAIC_LOWER",e.what()});}}
}

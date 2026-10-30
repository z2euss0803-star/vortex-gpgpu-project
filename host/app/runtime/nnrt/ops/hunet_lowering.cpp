#include <vxnn/hunet_lowering.hpp>
#include "../package/json.hpp"
#include <algorithm>
#include <cerrno>
#include <set>
namespace vxnn {
Result<HunetLowered> lower_hunet_node(const Plan&p,const HunetFamily&f,const ArenaStorage&s,NodeId id){try{
 const auto&r=s.receipt();
 json::check(r.phase==ArenaStoragePhase::READY&&!r.first_error&&r.root&&s.bindings()&&r.epoch&&p.policy.hardware.device_generation==r.epoch&&vxunified::s8_provider(p.policy.hardware)&&p.plan_id==s.layout().plan_id,"READY owned S8 plan and epoch required");
 json::check(p.programs.size()==1,"one trusted program family");const auto&key=p.programs[0];
 json::check(key.namespace_id==1&&key.logical_profile==1&&key.logical_program==65552&&key.code==f.program_sha&&key.schema==f.schema_sha&&digest_hex(key.schema)=="79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89"&&bool(vxunified::admit_program(p.policy.hardware,key)),"controlled program/schema/hardware identity");
 const PlannedNode*pn=nullptr;std::set<NodeId>ids;
 for(const auto&n:p.nodes){json::check(ids.insert(n.node.id).second,"duplicate node");if(n.node.id==id)pn=&n;}
 json::check(pn&&id,"unknown node");const auto&n=pn->node;uint32_t op=0;
 for(uint32_t i=4;i<=15;i++){auto k=hunet_operator_key(i,f);if(!(k<n.key)&&!(n.key<k)){op=i;break;}}
 json::check(op&&op!=6&&pn->job_count==1&&n.program_requirements==std::vector<Digest>{f.program_sha},"GPU identity required; CPU GELU stays CPU");
 const auto&bound=*s.bindings();const auto&root=*r.root;
 auto valid=[&](const BufferRef&b){return b.handle==root.handle&&b.object_generation==root.object_generation&&b.device_generation==r.epoch&&b.bytes&&b.offset>=r.prefix_bytes&&b.offset%4==0&&b.offset<=root.bytes&&b.bytes<=root.bytes-b.offset&&(b.access==1||b.access==2||b.access==3);};
 std::map<TensorId,const TensorDesc*> desc;
 for(const auto&t:p.tensors){json::check(desc.emplace(t.desc.id,&t.desc).second,"duplicate tensor");auto it=bound.tensors.find(t.desc.id);json::check(it!=bound.tensors.end()&&valid(it->second)&&it->second.bytes==t.desc.bytes&&it->second.access==t.desc.access,"tensor bound extent/access differs");}
 std::vector<TensorDesc>inputs;std::set<TensorId>unique;
 for(auto tid:n.inputs){json::check(desc.count(tid)&&unique.insert(tid).second,"declared unique inputs");inputs.push_back(*desc.at(tid));}
 auto call=inspect_hunet_operator(op,inputs,n.parameters,p.caps);if(!call)return Result<HunetLowered>::failure(*call.error);
 json::check(!call.value->cpu_gelu&&n.outputs.size()==1&&desc.count(n.outputs[0])&&!unique.count(n.outputs[0]),"one distinct GPU output");
 const auto&output=*desc.at(n.outputs[0]);const auto&invoke=*call.value;
 json::check(output.dtype==DType::FP32&&output.domain==AddressDomain::PL&&output.shape==invoke.output_shape&&output.layout==invoke.output_layout&&!output.constant&&(output.access&2)&&output.bytes==invoke.args.extent[VXH_Y]&&pn->workspace_bytes==invoke.workspace_bytes,"output/workspace metadata");
 json::check(bound.workspaces.count(id)&&invoke.read_roles.size()==n.inputs.size(),"workspace and read roles");
 HunetLowered lowered;lowered.node=id;lowered.args=invoke.args;
 auto x=bound.tensors.at(n.inputs[0]);x.access=1;for(unsigned k=0;k<6;k++)lowered.references[k]=x;
 for(size_t i=0;i<n.inputs.size();i++){json::check(invoke.read_roles[i]<6,"read role");auto b=bound.tensors.at(n.inputs[i]);json::check(b.access&1,"read permission");b.access=1;lowered.references[invoke.read_roles[i]]=b;}
 lowered.references[VXH_Y]=bound.tensors.at(n.outputs[0]);lowered.references[VXH_Y].access=2;
 lowered.references[VXH_WORK]=bound.workspaces.at(id);json::check(lowered.references[VXH_WORK].access==3,"read/write workspace");
 auto overlaps=[](const BufferRef&a,const BufferRef&b){return a.handle==b.handle&&a.offset<b.offset+b.bytes&&b.offset<a.offset+a.bytes;};
 for(unsigned k=0;k<8;k++){const auto&b=lowered.references[k];json::check(valid(b)&&b.bytes==invoke.args.extent[k],"exact private extent");if(k<6)json::check(!overlaps(b,lowered.references[VXH_Y])&&!overlaps(b,lowered.references[VXH_WORK]),"read/write alias");}
 json::check(!overlaps(lowered.references[VXH_Y],lowered.references[VXH_WORK]),"output/workspace alias");
 return Result<HunetLowered>::success(std::move(lowered));
 }catch(const std::bad_alloc&){return Result<HunetLowered>::failure({ENOMEM,"HUNET_LOWER","metadata allocation"});}
 catch(const std::exception&e){return Result<HunetLowered>::failure({EINVAL,"HUNET_LOWER",e.what()});}}
}

#include <vxnn/ops_int16.hpp>
#include "../package/json.hpp"
#include <cerrno>
namespace vxnn {
namespace {
const char*name(uint32_t code){switch(code){case VXNN_MATMUL_I16:return "MatMulI16.v1";case VXNN_BIAS_I48:return "BiasI48.v1";case VXNN_RELU_I48:return "ReLUI48.v1";default:return nullptr;}}
bool matrix(uint64_t m,uint64_t n){return m&&m<=64&&n&&n<=64&&!(m%8)&&!(n%8);}
Result<Shape>infer(uint32_t code,const std::vector<TensorDesc>&in,const ScalarParams&params,const DeviceCaps&caps){
 try{
  auto reject=[](){return Result<Shape>::failure({EINVAL,"INT16_OP","bounded dtype/layout/shape/address contract"});};
  if(!params.empty()||caps.contexts!=1||caps.preempt||caps.gpu_address_bits!=32||caps.max_args_bytes<128||caps.max_refs_per_job<3||caps.alignment<64||(caps.alignment&(caps.alignment-1))||in.size()!=(code==VXNN_RELU_I48?1u:2u))return reject();
  for(const auto&t:in)if(!t.id||t.id>=0xffff0000u||t.domain!=AddressDomain::PL||!(t.access&1)||(t.access&~3u)||(t.constant&&(t.access&2))||t.alignment<caps.alignment||(t.alignment&(t.alignment-1)))return reject();
  if(in.size()==2&&in[0].id==in[1].id)return reject();
  uint64_t m,n;
  if(code==VXNN_MATMUL_I16){
   if(in[0].dtype!=DType::I16||in[1].dtype!=DType::I16||in[0].layout!="RM_I16"||in[1].layout!="RM_I16"||in[0].shape.size()!=2||in[1].shape.size()!=2)return reject();
   m=in[0].shape[0];auto k=in[0].shape[1];n=in[1].shape[1];
   if(!matrix(m,n)||!k||k>64||in[1].shape[0]!=k||in[0].bytes!=m*k*2||in[1].bytes!=k*n*2)return reject();
  }else{
   if(code!=VXNN_BIAS_I48&&code!=VXNN_RELU_I48)return reject();
   if(in[0].dtype!=DType::I48||in[0].layout!="RM_I48S64"||in[0].shape.size()!=2)return reject();
   m=in[0].shape[0];n=in[0].shape[1];if(!matrix(m,n)||in[0].bytes!=m*n*8)return reject();
   if(code==VXNN_BIAS_I48&&(in[1].dtype!=DType::I32||in[1].layout!="ROW_I32"||in[1].shape!=Shape{n}||in[1].bytes!=n*4))return reject();
  }
  return Result<Shape>::success({m,n});
 }catch(const std::bad_alloc&){return Result<Shape>::failure({ENOMEM,"INT16_OP","allocation"});}
}
OperatorSpec spec(uint32_t code,const Int16Family&family){
 OperatorSpec s;s.key=int16_operator_key(code,family);s.available=true;
 s.supported_types=code==VXNN_MATMUL_I16?std::vector<DType>{DType::I16,DType::I48}:code==VXNN_BIAS_I48?std::vector<DType>{DType::I48,DType::I32}:std::vector<DType>{DType::I48};
 s.supported_layouts=code==VXNN_MATMUL_I16?std::vector<std::string>{"RM_I16","RM_I48S64"}:code==VXNN_BIAS_I48?std::vector<std::string>{"RM_I48S64","ROW_I32"}:std::vector<std::string>{"RM_I48S64"};
 s.validate=[code](const auto&in,const auto&p,const auto&c){auto shape=infer(code,in,p,c);return shape?Result<void>::success():Result<void>::failure(*shape.error);};
 s.infer_shapes=[code](const auto&in,const auto&p,const auto&c){auto shape=infer(code,in,p,c);return shape?Result<std::vector<Shape>>::success({*shape.value}):Result<std::vector<Shape>>::failure(*shape.error);};
 s.workspace=[code](const auto&in,const auto&p,const auto&c){auto shape=infer(code,in,p,c);return shape?Result<uint64_t>::success(0):Result<uint64_t>::failure(*shape.error);};
 auto key=s.key;
 s.prepare=[code,key,family](NodeId id,const auto&in,const auto&out,const auto&p,const auto&c)->Result<PreparedNode>{
  auto shape=infer(code,in,p,c);if(!shape)return Result<PreparedNode>::failure(*shape.error);
  if(!id||id>=0xffff0000u||out.size()!=1)return Result<PreparedNode>::failure({EINVAL,"INT16_OP","node/output arity"});
  const auto&t=out[0];if(!t.id||t.id>=0xffff0000u||t.dtype!=DType::I48||t.layout!="RM_I48S64"||t.shape!=*shape.value||t.bytes!=t.shape[0]*t.shape[1]*8||t.domain!=AddressDomain::PL||!(t.access&2)||(t.access&~3u)||t.constant||t.alignment<c.alignment||(t.alignment&(t.alignment-1)))return Result<PreparedNode>::failure({EINVAL,"INT16_OP","output storage differs"});
  PreparedNode n;n.id=id;n.key=key;n.parameters=p;n.outputs={t.id};n.program_requirements={family.program_sha};for(const auto&v:in){if(v.id==t.id)return Result<PreparedNode>::failure({EINVAL,"INT16_OP","unplanned in-place tensor"});n.inputs.push_back(v.id);}return Result<PreparedNode>::success(std::move(n));
 };
 s.execute=[code,key,family](const PreparedNode&n,ExecutionContext&ctx){
  if(n.key<key||key<n.key||!n.id||n.inputs.size()!=(code==VXNN_RELU_I48?1u:2u)||n.outputs.size()!=1||!n.parameters.empty()||n.program_requirements!=std::vector<Digest>{family.program_sha})return Result<Completion>::failure({EINVAL,"INT16_OP","prepared identity differs"});
  return ctx.submit(n,code);
 };return s;
}
}
OperatorKey int16_operator_key(uint32_t code,const Int16Family&family){
 const char*n=name(code);if(!n)return {};
 return {n,"1.0.0",content_digest(std::string("VXNN_INT16_OP:1:")+n+":"+digest_hex(family.implementation_sha)+":"+digest_hex(family.program_sha))};
}
Result<void> register_int16_operators(OperatorRegistry&r,const Int16Family&family){
 try{
  if(!vxunified::nonzero_digest(family.program_sha)||!vxunified::nonzero_digest(family.implementation_sha))return Result<void>::failure({EINVAL,"INT16_REGISTRY","exact source and program identities required"});
  OperatorRegistry candidate=r;
  for(uint32_t code:{uint32_t(VXNN_MATMUL_I16),uint32_t(VXNN_BIAS_I48),uint32_t(VXNN_RELU_I48)}){auto added=candidate.add(spec(code,family));if(!added)return added;}
  r=std::move(candidate);return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"INT16_REGISTRY","allocation"});}
}
Result<Plan> prepare_int16_model(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&){return Result<Plan>::failure({EINVAL,"INT16_PLAN","explicit cached backend identity required"});}
Result<Plan> prepare_int16_model(const ModelPackage&package,const OperatorRegistry&registry,const DeviceCaps&caps,const vxunified::BackendIdentity&hardware){
 try{
  json::check(hardware.provider_flags==1&&hardware.device_generation&&caps.gpu_address_bits==32&&caps.ps_dma_bits==32&&caps.pl_address_bits==40&&caps.cycle_counter_bits==32&&caps.max_args_bytes>=128&&caps.max_refs_per_job>=3&&caps.max_program_bytes>=65536,"integer family requires explicit S8 cached identity/capabilities");
  auto numeric=json::parse(package.numeric_contract_json());auto*n=numeric.get();json::fields(n,{"schema","reference_sha","threshold_sha","semantics","known_failures"});
  json::check(json::u64(json::member(n,"schema"))==1&&json::str(json::member(n,"semantics"))=="W16A16_NATIVE_I48_SIGNEXT64_EXACT_V1"&&json::digest(json::member(n,"threshold_sha"))==content_digest("{\"exact_integer\":true,\"max_absolute_error\":0}")&&json::count(json::member(n,"known_failures"))==0,"new integer model exact numerical contract");
  auto reference=json::digest(json::member(n,"reference_sha"));bool bound=false;for(const auto&asset:package.assets())if(asset.second.role=="reference"&&asset.second.type=="binary"&&asset.second.sha==reference)bound=true;
  json::check(bound,"integer reference content must be frozen in the package");
  OptimizationPolicy policy;policy.hardware=hardware;auto result=prepare_graph(package,registry,caps,std::move(policy));if(!result)return result;
  const auto&p=*result.value;json::check(p.programs.size()==1&&p.programs[0].namespace_id==1&&p.programs[0].logical_profile==1&&p.programs[0].logical_program==VXNN_MATMUL_I16,"only the new generic integer program family");
  // Private v1 schema is frozen independently of model content. A package
  // cannot substitute relocation rules while keeping the same native code.
  json::check(digest_hex(p.programs[0].schema)=="b4bcde7864f8b6cab955f2f98de2d558edefe27257a6465fe89aedba72a95f0d","integer v1 sealed argument schema differs");
  json::check(!p.nodes.empty()&&!p.inputs.empty()&&p.outputs.size()==1&&p.total_jobs==p.nodes.size(),"bounded integer graph requires one job per node");
  for(const auto&node:p.nodes)json::check(node.job_count==1&&node.workspace_bytes==0&&node.node.parameters.empty()&&node.node.program_requirements==std::vector<Digest>{p.programs[0].code}&&(node.node.key.name=="MatMulI16.v1"||node.node.key.name=="BiasI48.v1"||node.node.key.name=="ReLUI48.v1"),"only registered bounded integer operators/program");
  for(const auto&t:p.tensors)if(t.desc.id==p.outputs[0])json::check(t.desc.dtype==DType::I48&&t.desc.layout=="RM_I48S64","integer final output storage");
  return result;
 }catch(const std::bad_alloc&){return Result<Plan>::failure({ENOMEM,"INT16_PLAN","allocation"});}
 catch(const std::exception&e){return Result<Plan>::failure({EINVAL,"INT16_PLAN",e.what()});}
}
}

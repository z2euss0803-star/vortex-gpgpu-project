#include <vxnn/mosaic_registry.hpp>
#include <vxnn/mosaic_program_family.hpp>
#include "device/vxnn/mosaic_wire_reuse_v4.h"
#include <vxnn/mosaic_weight_layout.hpp>
#include <algorithm>
#include <cerrno>
#include <set>
#include <stdexcept>
namespace vxnn {namespace {
void check(bool yes,const char*why){if(!yes)throw std::runtime_error(why);}
const char*name(uint32_t op){switch(op){case 1:return "Mosaic.QuantI16.v1";case 2:return "Mosaic.ConvI16I48.v1";case 3:return "Mosaic.NormStats.v3";case 4:return "Mosaic.NormApply.v3";case 5:return "Mosaic.Relu.v1";case 6:return "Mosaic.Add.v1";case 7:return "Mosaic.NormReluQuant.v1";case 8:return "Mosaic.NormAddQuant.v1";default:return nullptr;}}
MosaicInvocation describe(uint32_t op,const ScalarParams&p){
 const bool reuse=p.count("schedule_version")||p.count("weight_layout_version");
 const bool cooperative=p.count("work_layout_version")||p.count("profiling_options")||reuse;
 check(name(op)&&p.size()==(reuse?8u:cooperative?6u:4u)&&p.count("shape")&&p.count("flags")&&p.count("scale_bits")&&p.count("budget"),"frozen geometry plus explicit optional cooperative work/profile policy only");
 uint32_t profiling_options=0;
 if(cooperative){
  check(p.count("work_layout_version")&&p.count("profiling_options"),"complete cooperative parameter policy required");
  const auto*layout=std::get_if<uint64_t>(&p.at("work_layout_version"));const auto*options=std::get_if<uint64_t>(&p.at("profiling_options"));
  check(layout&&*layout==MOSAIC_WORK_LAYOUT_V1&&options&&*options<=MOSAIC_OPTION_CYCLES_V1,"exact work layout1 and options bit0");profiling_options=uint32_t(*options);
 }
 if(reuse){
  check(p.count("schedule_version")&&p.count("weight_layout_version"),"complete explicit reuse schedule/weight policy required");
  const auto*schedule=std::get_if<uint64_t>(&p.at("schedule_version"));const auto*weights=std::get_if<uint64_t>(&p.at("weight_layout_version"));
  check(schedule&&*schedule==MOSAIC_SCHEDULE_SPATIAL_MAJOR_NB_MOD4_V2&&weights&&*weights==MOSAIC_WEIGHT_MB_KPAD_ROW8_I16_V2,"exact schedule2/weight-layout2 required, never inferred from byte size");
 }
 const auto*shape=std::get_if<Shape>(&p.at("shape"));const auto*flags=std::get_if<uint64_t>(&p.at("flags"));const auto*scale=std::get_if<uint64_t>(&p.at("scale_bits"));const auto*budget=std::get_if<uint64_t>(&p.at("budget"));
 check(shape&&shape->size()==8&&flags&&*flags<=UINT32_MAX&&scale&&*scale<=UINT32_MAX&&budget&&*budget&&*budget<=4096,"typed bounded parameters");
 for(auto n:*shape)check(n&&n<=UINT32_MAX,"nonzero bounded shape words");
 MosaicInvocation d;auto&a=d.args;a.abi=reuse?MOSAIC_PRIVATE_ABI_REUSE_V4:cooperative?3:op>=7?2:1;a.reserved=profiling_options;a.bytes=128;a.opcode=op;a.flags=uint32_t(*flags);a.scale_bits=uint32_t(*scale);a.ci=(*shape)[0];a.co=(*shape)[1];a.h=(*shape)[2];a.w=(*shape)[3];a.k=(*shape)[4];a.stride=(*shape)[5];a.upsample=(*shape)[6];a.kpad=(*shape)[7];a.count=1;d.budget=uint32_t(*budget);
 check(a.ci<=128&&a.co<=128&&a.h>=2&&a.h<=960&&a.w>=2&&a.w<=1280&&(a.k==3||a.k==9)&&(a.stride==1||a.stride==2)&&(a.upsample==1||a.upsample==2),"native geometry bounds");
 const uint64_t oh=(uint64_t(a.h)*a.upsample+a.stride-1)/a.stride,ow=(uint64_t(a.w)*a.upsample+a.stride-1)/a.stride;
 auto operand=[&](uint32_t role,DType type,const char*layout,Shape dimensions,bool constant){uint64_t bytes=type==DType::I16?2:4;for(auto v:dimensions){check(v&&bytes<=UINT32_MAX/v,"operand extent overflow");bytes*=v;}return MosaicOperand{role,type,layout,std::move(dimensions),bytes,constant};};
 if(op==1){d.inputs={operand(VXM_INPUT,DType::FP32,"CHW_F32",{a.ci,a.h,a.w},false)};d.outputs={operand(VXM_QUANT,DType::I16,"CHW_I16",{a.ci,a.h,a.w},false)};if(a.flags&1)d.outputs.push_back(operand(VXM_SKIP,DType::FP32,"CHW_F32",{a.ci,a.h,a.w},false));}
 if(op==2){d.inputs={operand(VXM_QUANT,DType::I16,"CHW_I16",{a.ci,a.h,a.w},false),operand(VXM_WEIGHTS,DType::I16,reuse?mosaic_weight_layout2_name:"CO_KPAD_I16",reuse?Shape{(a.co+7u)/8u,a.kpad,8}:Shape{a.co,a.kpad},true),operand(VXM_COEFF,DType::FP32,"MOSAIC_COEFF_F32",{a.co*2u},true)};d.outputs={operand(VXM_OUTPUT,DType::FP32,"CHW_F32",{a.co,oh,ow},false)};}
 if(op>=3&&op<=6){d.inputs={operand(VXM_INPUT,DType::FP32,"CHW_F32",{a.co,oh,ow},false)};d.outputs={operand(op==3?VXM_STATS:VXM_OUTPUT,DType::FP32,op==3?"CO_STATS3_F32":"CHW_F32",op==3?Shape{a.co,3}:Shape{a.co,oh,ow},false)};}
 if(op>=7){d.inputs={operand(VXM_INPUT,DType::FP32,"CHW_F32",{a.co,oh,ow},false)};d.outputs={operand(VXM_QUANT,DType::I16,"CHW_I16",{a.co,oh,ow},false)};if(a.flags&1)d.outputs.push_back(operand(VXM_OUTPUT,DType::FP32,"CHW_F32",{a.co,oh,ow},false));}
 if(op==4||op>=7){d.inputs.push_back(operand(VXM_COEFF,DType::FP32,"MOSAIC_COEFF_F32",{a.co*4u},true));d.inputs.push_back(operand(VXM_STATS,DType::FP32,"CO_STATS3_F32",{a.co,3},false));}
 if(op==6||op==8)d.inputs.push_back(operand(VXM_SKIP,DType::FP32,"CHW_F32",{a.co,oh,ow},false));
 for(auto&n:a.extent)n=64;
 for(const auto&i:d.inputs)a.extent[i.role]=uint32_t(i.bytes);
 for(const auto&o:d.outputs)a.extent[o.role]=uint32_t(o.bytes);
 if(op==2){check(a.kpad<=2592,"native padded K");a.extent[VXM_WORK]=64+4*((uint64_t(a.kpad)*32+512+63)/64*64);}
 if(op==3){uint64_t tree=1;while(tree<oh*ow)tree*=2;check(tree<=2097152,"norm tree bound");a.extent[VXM_WORK]=uint32_t(64+tree*8*4);}
 if(cooperative){
  uint32_t tree=0;if(op==3){tree=1;while(tree<oh*ow)tree*=2;}
  MosaicWorkLayoutV1 layout{};check(!(reuse?mosaic_reuse_work_layout_v4(op,a.kpad,tree,&layout):mosaic_work_layout_v1(op,a.kpad,tree,&layout)),"shared cooperative layout arithmetic");a.extent[VXM_WORK]=layout.total_work_bytes;
 }
 check(!(reuse?vxnn_mosaic_validate_reuse_v4(&a,false,&d.shape):cooperative?vxnn_mosaic_validate_coop_v3(&a,false,&d.shape):op>=7?vxnn_mosaic_validate_fused(&a,false,&d.shape):vxnn_mosaic_validate(&a,false,&d.shape)),"exact native argument contract");d.jobs=(uint64_t(d.shape.items)+d.budget-1)/d.budget;return d;
}
bool tensor(const TensorDesc&t,const MosaicOperand&e,const DeviceCaps&c,bool output){return t.id&&t.id<0xffff0000u&&t.dtype==e.type&&t.layout==e.layout&&t.shape==e.shape&&t.bytes==e.bytes&&t.domain==AddressDomain::PL&&t.alignment>=c.alignment&&!(t.alignment&(t.alignment-1))&&(t.access&(output?2u:1u))&&!(t.access&~3u)&&(!output||!t.constant)&&(!t.constant||t.access==1)&&(!e.constant||(t.constant&&!t.asset.empty()&&vxunified::nonzero_digest(t.content_sha)));}
OperatorSpec spec(uint32_t op,const MosaicFamily&f){
 OperatorSpec s;s.key=mosaic_operator_key(op,f);s.available=true;s.supported_types={DType::FP32,DType::I16};s.supported_layouts={"CHW_F32","CHW_I16","CO_KPAD_I16","MOSAIC_COEFF_F32","CO_STATS3_F32"};
 if(mosaic_program_variant(f.schema_sha)==MosaicProgramVariant::REUSE)s.supported_layouts.push_back(mosaic_weight_layout2_name);
 s.validate=[op,f](const auto&in,const auto&p,const auto&c){auto d=inspect_mosaic_operator(op,in,p,c,f);return d?Result<void>::success():Result<void>::failure(*d.error);};
 s.infer_shapes=[op,f](const auto&in,const auto&p,const auto&c)->Result<std::vector<Shape>>{auto d=inspect_mosaic_operator(op,in,p,c,f);if(!d)return Result<std::vector<Shape>>::failure(*d.error);std::vector<Shape> out;for(const auto&o:d.value->outputs)out.push_back(o.shape);return Result<std::vector<Shape>>::success(std::move(out));};
 s.workspace=[op,f](const auto&in,const auto&p,const auto&c){auto d=inspect_mosaic_operator(op,in,p,c,f);return d?Result<uint64_t>::success(d.value->args.extent[VXM_WORK]):Result<uint64_t>::failure(*d.error);};
 const auto key=s.key;
 s.prepare=[op,key,f](NodeId id,const auto&in,const auto&out,const auto&p,const auto&c)->Result<PreparedNode>{
  auto d=inspect_mosaic_operator(op,in,p,c,f);if(!d)return Result<PreparedNode>::failure(*d.error);
  if(!id||id>=0xffff0000u||out.size()!=d.value->outputs.size())return Result<PreparedNode>::failure({EINVAL,"MOSAIC_OP","node/output arity"});
  PreparedNode n;n.id=id;n.key=key;n.parameters=p;n.program_requirements={f.program_sha};std::set<TensorId>ids;for(const auto&t:in){ids.insert(t.id);n.inputs.push_back(t.id);}
  for(size_t i=0;i<out.size();i++){if(!tensor(out[i],d.value->outputs[i],c,true)||!ids.insert(out[i].id).second)return Result<PreparedNode>::failure({EINVAL,"MOSAIC_OP","output identity/type/shape/access or SSA alias"});n.outputs.push_back(out[i].id);}return Result<PreparedNode>::success(std::move(n));
 };
 s.execute=[op,key,f](const PreparedNode&n,ExecutionContext&ctx)->Result<Completion>{try{const auto d=describe(op,n.parameters);std::set<TensorId>ids;
  if(!mosaic_parameter_abi_matches(mosaic_program_variant(f.schema_sha),d.args.abi,op)||n.key<key||key<n.key||!n.id||n.id>=0xffff0000u||n.program_requirements!=std::vector<Digest>{f.program_sha}||n.inputs.size()!=d.inputs.size()||n.outputs.size()!=d.outputs.size())return Result<Completion>::failure({EINVAL,"MOSAIC_OP","prepared binding differs"});
  for(auto list:{n.inputs,n.outputs})for(auto id:list)if(!id||id>=0xffff0000u||!ids.insert(id).second)return Result<Completion>::failure({EINVAL,"MOSAIC_OP","distinct bounded logical tensor ids"});
  // The native request context dispatches one bounded subjob. Its completion
  // does not by itself imply retirement of the entire logical phase.
  return ctx.submit(n,op);
 }catch(const std::bad_alloc&){return Result<Completion>::failure({ENOMEM,"MOSAIC_OP","allocation"});}catch(const std::exception&e){return Result<Completion>::failure({EINVAL,"MOSAIC_OP",e.what()});}};return s;
}
}
OperatorKey mosaic_operator_key(uint32_t op,const MosaicFamily&f){const char*n=name(op);if(!n)return {};return {n,"1.0.0",content_digest(std::string("VXNN_MOSAIC_OP:1:")+n+":"+digest_hex(f.implementation_sha)+":"+digest_hex(f.program_sha)+":"+digest_hex(f.readonly_sha)+":"+digest_hex(f.schema_sha))};}
Result<void> register_mosaic_operators(OperatorRegistry&r,const MosaicFamily&f){try{
 const auto variant=mosaic_program_variant(f.schema_sha);
 if(!vxunified::nonzero_digest(f.program_sha)||!vxunified::nonzero_digest(f.readonly_sha)||!vxunified::nonzero_digest(f.implementation_sha)||variant==MosaicProgramVariant::UNKNOWN)return Result<void>::failure({EINVAL,"MOSAIC_REGISTRY","CODE/readonly/source and exact sealed schema required"});
 if((variant==MosaicProgramVariant::COOPERATIVE||variant==MosaicProgramVariant::REUSE)&&(!mosaic_program_code_matches(f.schema_sha,f.program_sha)||digest_hex(f.readonly_sha)!="30e0e46cb30629f4a7de447405225e208b37f15aaf2be23603bbe4cb975b5643"))return Result<void>::failure({EINVAL,"MOSAIC_REGISTRY","actual published cooperative CODE and frozen readonly required"});
 auto candidate=r;for(uint32_t op=1;op<=(variant==MosaicProgramVariant::RO64?6u:8u);op++){auto a=candidate.add(spec(op,f));if(!a)return a;}r=std::move(candidate);return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"MOSAIC_REGISTRY","allocation"});}}
Result<MosaicInvocation> inspect_mosaic_operator(uint32_t op,const std::vector<TensorDesc>&in,const ScalarParams&p,const DeviceCaps&c){try{
 check(c.contexts==1&&!c.preempt&&c.gpu_address_bits==32&&c.ps_dma_bits==32&&c.pl_address_bits==40&&c.cycle_counter_bits==32&&c.max_args_bytes>=128&&c.max_refs_per_job==8&&c.max_program_bytes>=65536+6307840&&c.alignment>=64&&!(c.alignment&(c.alignment-1)),"explicit S8 bounded native capabilities");
 auto d=describe(op,p);check(in.size()==d.inputs.size(),"logical operand arity");std::set<TensorId>ids;for(size_t i=0;i<in.size();i++)check(tensor(in[i],d.inputs[i],c,false)&&ids.insert(in[i].id).second,"exact dtype/layout/shape/domain/constant/read identity");return Result<MosaicInvocation>::success(std::move(d));
 }catch(const std::bad_alloc&){return Result<MosaicInvocation>::failure({ENOMEM,"MOSAIC_OP","allocation"});}catch(const std::exception&e){return Result<MosaicInvocation>::failure({EINVAL,"MOSAIC_OP",e.what()});}}
Result<MosaicInvocation> inspect_mosaic_operator(uint32_t op,const std::vector<TensorDesc>&in,const ScalarParams&p,const DeviceCaps&c,const MosaicFamily&f){
 auto result=inspect_mosaic_operator(op,in,p,c);if(!result)return result;
 const auto variant=mosaic_program_variant(f.schema_sha);
 if(!mosaic_parameter_abi_matches(variant,result.value->args.abi,op))return Result<MosaicInvocation>::failure({EINVAL,"MOSAIC_OP","operator parameter ABI differs from explicit program schema"});
 return result;
}
}

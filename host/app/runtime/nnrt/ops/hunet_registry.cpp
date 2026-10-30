#include <vxnn/hunet_registry.hpp>
#include "vendor/hunet/v5/include/hunet_job.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <set>
#include <stdexcept>
namespace vxnn {
namespace {
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
const char*name(uint32_t op){switch(op){case 4:return "Hunet.GnStats.v1";case 5:return "Hunet.GnApply.v1";case 6:return "Hunet.GeluErf4.v1";case 7:return "Hunet.Shift.v1";case 8:return "Hunet.Add.v1";case 9:return "Hunet.Gap.v1";case 10:return "Hunet.Sigmoid.v1";case 11:return "Hunet.MulChannel.v1";case 12:return "Hunet.Slice.v1";case 13:return "Hunet.Concat.v1";case 14:return "Hunet.Copy.v1";case 15:return "Hunet.FusedConv.v1";default:return nullptr;}}
struct Expected {uint32_t role;DType type;const char*layout;Shape shape;uint64_t bytes;bool constant,channel_view;};
struct Description {HunetInvocation call;std::vector<Expected> inputs;};
Description describe(uint32_t op,const ScalarParams&p){
 check(name(op)&&p.size()==3&&p.count("shape")&&p.count("flags")&&p.count("descriptors"),"only frozen shape/flags/descriptors parameters");
 auto*ss=std::get_if<Shape>(&p.at("shape"));auto*flags=std::get_if<uint64_t>(&p.at("flags"));auto*hex=std::get_if<std::string>(&p.at("descriptors"));check(ss&&ss->size()==12&&flags&&*flags<=UINT32_MAX&&hex,"bounded parameter types");
 for(auto v:*ss)check(v<=UINT32_MAX,"shape word overflow");
 Description d;auto&a=d.call.args;a.abi=1;a.bytes=128;a.op=op;a.flags=uint32_t(*flags);a.ci=(*ss)[0];a.co=(*ss)[1];a.ih=(*ss)[2];a.iw=(*ss)[3];a.oh=(*ss)[4];a.ow=(*ss)[5];a.k=(*ss)[6];a.stride=(*ss)[7];a.pad=(*ss)[8];a.groups=(*ss)[9];a.n_div=(*ss)[10];a.epsilon_bits=(*ss)[11];
 check(a.ci&&a.ci<=16384&&a.co&&a.co<=16384&&a.ih&&a.ih<=4096&&a.iw&&a.iw<=4096&&a.oh&&a.oh<=4096&&a.ow&&a.ow<=4096,"bounded CHW shape");
 const uint64_t nx=uint64_t(a.ci)*a.ih*a.iw,ny=uint64_t(a.co)*a.oh*a.ow;check(nx<=16777216&&ny<=16777216,"tensor byte overflow");
 uint64_t sizes[8]={nx*4,0,0,0,0,0,ny*4,16};
 auto add=[&](uint32_t role,DType type,const char*layout,Shape shape,uint64_t bytes,bool constant=false){d.inputs.push_back({role,type,layout,std::move(shape),bytes,constant,false});d.call.read_roles.push_back(role);sizes[role]=bytes;};
 add(VXH_X,DType::FP32,"CHW_F32",{a.ci,a.ih,a.iw},nx*4);d.call.output_shape={a.co,a.oh,a.ow};d.call.output_layout="CHW_F32";
 if(op==15){
  if(a.flags&2)d.inputs[0].channel_view=true;
  check(!hex->empty()&&hex->size()<=4096&&hex->size()%128==0,"complete bounded descriptor bytes");std::string bytes;bytes.reserve(hex->size()/2);
  auto nibble=[](char c)->uint32_t{check((c>='0'&&c<='9')||(c>='a'&&c<='f'),"canonical descriptor hex");return c<='9'?c-'0':c-'a'+10;};
  for(size_t i=0;i<hex->size();i+=2)bytes.push_back(char(nibble((*hex)[i])*16+nibble((*hex)[i+1])));
  uint64_t packed=0,tiles=0;
  for(size_t i=0;i<bytes.size();i+=64){uint32_t w[16];for(size_t k=0;k<16;k++){w[k]=0;for(unsigned b=0;b<4;b++)w[k]|=uint32_t(uint8_t(bytes[i+4*k+b]))<<(8*b);}HunetFusedDescriptorV1 v;std::memcpy(&v,w,64);check(v.mp<=16384&&v.kp<=16384&&v.np<=65536,"descriptor product bound");packed+=uint64_t(v.kp)*v.np*2;tiles+=uint64_t(v.mp/8)*(v.np/4);check(packed<=67108864&&tiles<=16777216,"workspace sum bound");d.call.descriptors.push_back(v);}
  const auto&first=d.call.descriptors[0];add(VXH_W,DType::FP16,"RM_FP16_PAD8",{first.mp,first.kp},uint64_t(first.mp)*first.kp*2,true);
  if(a.flags&16)add(VXH_BIAS,DType::FP32,"ROW_F32",{first.m},uint64_t(first.m)*4,true);
  add(VXH_STATS,DType::U8,"DESC64_BYTES",{bytes.size()},bytes.size(),true);sizes[VXH_WORK]=((packed+63)/64)*64+tiles*4;
 }else{
  check(hex->empty(),"scalar/CPU op cannot carry descriptor bytes");
  if(op==HUNET_GN_STATS){d.call.output_shape={2};d.call.output_layout="GN_MEAN_INVSTD_F32";sizes[VXH_Y]=8;}
  if(op==HUNET_GN_APPLY){add(VXH_GAMMA,DType::FP32,"ROW_F32",{a.ci},a.ci*4,true);add(VXH_BETA,DType::FP32,"ROW_F32",{a.ci},a.ci*4,true);add(VXH_STATS,DType::FP32,"GN_MEAN_INVSTD_F32",{2},8);}
  if(op==HUNET_ADD)add(VXH_W,DType::FP32,"CHW_F32",{a.co,a.oh,a.ow},ny*4);
  if(op==HUNET_MUL_CHANNEL){add(VXH_W,DType::FP32,"ROW_F32",{a.co},a.co*4);d.inputs.back().channel_view=true;}
  if(op==HUNET_CONCAT){check(a.co>a.ci,"concat channel difference");add(VXH_W,DType::FP32,"CHW_F32",{a.co-a.ci,a.ih,a.iw},uint64_t(a.co-a.ci)*a.ih*a.iw*4);}
 }
 d.call.cpu_gelu=op==HUNET_GELU;
 if(d.call.cpu_gelu){check(!a.flags&&!a.k&&!a.stride&&!a.pad&&!a.groups&&!a.n_div&&!a.epsilon_bits&&a.ci==a.co&&a.ih==a.oh&&a.iw==a.ow&&nx<=393216,"CPU GELU shape and borrowed-pool maximum");sizes[VXH_WORK]=0;}
 for(unsigned i=0;i<8;i++){if(!sizes[i]&&i<6)sizes[i]=sizes[0];check(sizes[i]<=67108864,"private extent bound");a.extent[i]=uint32_t(sizes[i]);}
 if(!d.call.cpu_gelu){VxnnHunetShape actual{};check(!vxnn_hunet_validate(&a,d.call.descriptors.empty()?nullptr:d.call.descriptors.data(),d.call.descriptors.size(),false,&actual),"controlled shader shape/descriptor contract");d.call.workspace_bytes=actual.required[VXH_WORK];}
 return d;
}
bool tensor(const TensorDesc&t,const Expected&e,const DeviceCaps&caps){
 bool layout=t.layout==e.layout&&t.shape==e.shape;
 // Only the two frozen contiguous channel-vector interpretations. No data
 // movement, axis permutation, new graph job or changed shader indexing.
 if(e.channel_view){const auto channels=e.role==VXH_X?e.shape.back():e.shape[0];layout=layout||(t.layout=="CHW_F32"&&t.shape==Shape{channels,1,1});}
 return t.id&&t.id<0xffff0000u&&t.domain==AddressDomain::PL&&(t.access&1)&&!(t.access&~3u)&&t.alignment>=caps.alignment&&!(t.alignment&(t.alignment-1))&&t.dtype==e.type&&layout&&t.bytes==e.bytes&&(!t.constant||t.access==1)&&(!e.constant||(t.constant&&!t.asset.empty()&&vxunified::nonzero_digest(t.content_sha)));
}
OperatorSpec spec(uint32_t op,const HunetFamily&family,bool cpu_stats=false){
 OperatorSpec s;s.key=cpu_stats?hunet_stats_cpu_key(family):hunet_operator_key(op,family);s.available=true;s.supported_types={DType::FP32,DType::FP16,DType::U8};s.supported_layouts={"CHW_F32","ROW_F32","GN_MEAN_INVSTD_F32","RM_FP16_PAD8","DESC64_BYTES"};
 s.validate=[op](const auto&in,const auto&p,const auto&c){auto x=inspect_hunet_operator(op,in,p,c);return x?Result<void>::success():Result<void>::failure(*x.error);};
 s.infer_shapes=[op](const auto&in,const auto&p,const auto&c){auto x=inspect_hunet_operator(op,in,p,c);return x?Result<std::vector<Shape>>::success({x.value->output_shape}):Result<std::vector<Shape>>::failure(*x.error);};
 s.workspace=[op,cpu_stats](const auto&in,const auto&p,const auto&c){auto x=inspect_hunet_operator(op,in,p,c);return x?Result<uint64_t>::success(cpu_stats?0:x.value->workspace_bytes):Result<uint64_t>::failure(*x.error);};
 auto key=s.key;
 s.prepare=[op,key,family,cpu_stats](NodeId id,const auto&in,const auto&out,const auto&p,const auto&caps)->Result<PreparedNode>{
  auto x=inspect_hunet_operator(op,in,p,caps);if(!x)return Result<PreparedNode>::failure(*x.error);
  if(!id||id>=0xffff0000u||out.size()!=1){return Result<PreparedNode>::failure({EINVAL,"HUNET_OP","node/output arity"});}
  const auto&t=out[0];
  if(!t.id||t.id>=0xffff0000u||t.dtype!=DType::FP32||t.layout!=x.value->output_layout||t.shape!=x.value->output_shape||t.bytes!=x.value->args.extent[VXH_Y]||t.domain!=AddressDomain::PL||!(t.access&2)||(t.access&~3u)||t.constant||t.alignment<caps.alignment||(t.alignment&(t.alignment-1)))return Result<PreparedNode>::failure({EINVAL,"HUNET_OP","output storage contract"});
  PreparedNode n;n.id=id;n.key=key;n.parameters=p;n.outputs={t.id};if(!x.value->cpu_gelu&&!cpu_stats)n.program_requirements={family.program_sha};for(const auto&i:in){if(i.id==t.id)return Result<PreparedNode>::failure({EINVAL,"HUNET_OP","no unplanned in-place alias"});n.inputs.push_back(i.id);}return Result<PreparedNode>::success(std::move(n));
 };
 s.execute=[op,key,family,cpu_stats](const PreparedNode&n,ExecutionContext&ctx)->Result<Completion>{
  try{
   auto d=describe(op,n.parameters);auto programs=(d.call.cpu_gelu||cpu_stats)?std::vector<Digest>{}:std::vector<Digest>{family.program_sha};std::set<TensorId>ids(n.inputs.begin(),n.inputs.end());
   if(n.key<key||key<n.key||!n.id||n.id>=0xffff0000u||n.inputs.size()!=d.inputs.size()||ids.size()!=n.inputs.size()||ids.count(0)||n.outputs.size()!=1||!n.outputs[0]||ids.count(n.outputs[0])||n.program_requirements!=programs)return Result<Completion>::failure({EINVAL,"HUNET_OP","prepared identity differs"});
   if(!d.call.cpu_gelu&&!cpu_stats)return ctx.submit(n,op);
   auto*cpu=dynamic_cast<HunetExecutionContext*>(&ctx);if(!cpu)return Result<Completion>::failure({EOPNOTSUPP,"HUNET_CPU","shared-pool CPU context required, no GPU fallback"});
   auto result=cpu_stats?cpu->cpu_gn_stats(n):cpu->cpu_gelu(n);
   if(result&&result.value->result){const int64_t raw=result.value->result,absolute=raw<0?-raw:raw;return Result<Completion>::failure({absolute<=INT_MAX?int(absolute):EIO,"HUNET_CPU","CPU completion result="+std::to_string(raw)});}
   if(result&&(result.value->cycles_valid||result.value->raw_cycles))return Result<Completion>::failure({EPROTO,"HUNET_CPU","CPU work cannot report GPU cycles"});
   return result;
  }catch(const std::bad_alloc&){return Result<Completion>::failure({ENOMEM,"HUNET_OP","allocation"});}catch(const std::exception&e){return Result<Completion>::failure({EINVAL,"HUNET_OP",e.what()});}
 };return s;
}
}
Result<HunetInvocation> inspect_hunet_operator(uint32_t op,const std::vector<TensorDesc>&in,const ScalarParams&p,const DeviceCaps&caps){
 try{
  check(caps.contexts==1&&!caps.preempt&&caps.gpu_address_bits==32&&caps.ps_dma_bits==32&&caps.pl_address_bits==40&&caps.cycle_counter_bits==32&&caps.max_args_bytes>=128&&caps.max_refs_per_job>=8&&caps.max_program_bytes>=65536&&caps.alignment>=64&&!(caps.alignment&(caps.alignment-1)),"explicit bounded S8 NN capabilities");
  auto d=describe(op,p);check(in.size()==d.inputs.size(),"logical operand arity");std::set<TensorId> ids;
  for(size_t i=0;i<in.size();i++){check(tensor(in[i],d.inputs[i],caps)&&ids.insert(in[i].id).second,"logical input dtype/layout/shape/domain/identity");
   if(op==15&&d.inputs[i].role==VXH_STATS){std::string raw;raw.resize(d.call.descriptors.size()*64);std::memcpy(&raw[0],d.call.descriptors.data(),raw.size());check(content_digest(raw)==in[i].content_sha,"descriptor bytes differ from immutable input asset digest");}
  }
  return Result<HunetInvocation>::success(std::move(d.call));
 }catch(const std::bad_alloc&){return Result<HunetInvocation>::failure({ENOMEM,"HUNET_OP","allocation"});}catch(const std::exception&e){return Result<HunetInvocation>::failure({EINVAL,"HUNET_OP",e.what()});}
}
OperatorKey hunet_operator_key(uint32_t op,const HunetFamily&family){
 const auto*n=name(op);if(!n)return {};return {n,"1.0.0",content_digest(std::string("VXNN_HUNET_OP:1:")+n+":"+digest_hex(family.implementation_sha)+":"+digest_hex(family.program_sha)+":"+digest_hex(family.schema_sha))};
}
OperatorKey hunet_stats_cpu_key(const HunetFamily&family){const char*n="Hunet.GnStatsCpuOrdered.v1";return {n,"1.0.0",content_digest(std::string("VXNN_HUNET_OP:1:")+n+":"+digest_hex(family.implementation_sha)+":"+digest_hex(family.program_sha)+":"+digest_hex(family.schema_sha))};}
Result<void> register_hunet_operators(OperatorRegistry&r,const HunetFamily&family){
 try{
  if(!vxunified::nonzero_digest(family.program_sha)||!vxunified::nonzero_digest(family.implementation_sha)||digest_hex(family.schema_sha)!="79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89")return Result<void>::failure({EINVAL,"HUNET_REGISTRY","exact code/source and sealed args8 schema required"});
  OperatorRegistry candidate=r;for(uint32_t op=4;op<=15;op++){auto added=candidate.add(spec(op,family));if(!added)return added;}auto stats=candidate.add(spec(4,family,true));if(!stats)return stats;r=std::move(candidate);return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"HUNET_REGISTRY","allocation"});}
}
}

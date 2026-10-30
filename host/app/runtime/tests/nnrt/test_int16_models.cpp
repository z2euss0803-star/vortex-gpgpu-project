#include <vxnn/ops_int16.hpp>
#include "nnrt/package/json.hpp"
#include "tests/mosaic_s8/fake_s8.h"
#include <vxl/v2/program.h>
#include "fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
using namespace vxnn;
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U11_MODEL_ASSERT %s\n",s);std::exit(64);}}
static std::string read(const Path&p){std::ifstream f(p.string(),std::ios::binary);need(bool(f),"open frozen bytes");return {std::istreambuf_iterator<char>(f),{}};}
// Explicit software PE protocol model, never a ComputeBackend or kernel handle.
struct MathContext:ExecutionContext{
 const Plan&plan;std::map<TensorId,std::vector<int64_t>>data;unsigned calls=0,tiles=0;
 explicit MathContext(const Plan&p):plan(p){}
 const TensorDesc&desc(TensorId id){for(const auto&t:plan.tensors)if(t.desc.id==id)return t.desc;std::abort();}
 void bytes(TensorId id,const std::vector<uint8_t>&v){need(v.size()==desc(id).bytes,"asset length equals planned storage");data[id].resize((v.size()+7)/8);std::memcpy(data[id].data(),v.data(),v.size());}
 Result<Completion>submit(const PreparedNode&node,uint32_t code)override{
  ++calls;auto&a=data.at(node.inputs[0]);const auto&t=desc(node.outputs[0]);auto&o=data[node.outputs[0]];o.resize(t.bytes/8);int rc=0;
  std::vector<int16_t>av,bv;std::vector<int32_t>bias;
  if(code==VXNN_MATMUL_I16){av.resize(desc(node.inputs[0]).bytes/2);bv.resize(desc(node.inputs[1]).bytes/2);std::memcpy(av.data(),a.data(),av.size()*2);std::memcpy(bv.data(),data.at(node.inputs[1]).data(),bv.size()*2);}
  if(code==VXNN_BIAS_I48){bias.resize(desc(node.inputs[1]).bytes/4);std::memcpy(bias.data(),data.at(node.inputs[1]).data(),bias.size()*4);}
  for(uint32_t core=0;core<4&&!rc;core++){
   if(code==VXNN_MATMUL_I16){const auto&ad=desc(node.inputs[0]);const auto&bd=desc(node.inputs[1]);fake_s8 f;auto io=f.ops();io.core=core;rc=vxnn_i16_matmul(av.data(),av.size(),bv.data(),bv.size(),ad.shape[0],bd.shape[1],ad.shape[1],o.data(),o.size(),&io);tiles+=f.begins;need(f.begins==f.releases,"successful native tile retires");}
   else if(code==VXNN_BIAS_I48)rc=vxnn_i48_bias(a.data(),a.size(),bias.data(),bias.size(),t.shape[0],t.shape[1],o.data(),o.size(),core);
   else if(code==VXNN_RELU_I48)rc=vxnn_i48_relu(a.data(),a.size(),o.data(),o.size(),core);
   else need(false,"unregistered opcode");
  }
  if(rc)return Result<Completion>::failure({-rc,"HOST_MATH","native mathematical source refused"});
  return Result<Completion>::success(Completion{});
 }
};
int main(int argc,char**argv){
 need(argc==3,"fixture and family paths");auto family_json=json::parse(read(Path(argv[2])/"family.json"));auto*f=family_json.get();Int16Family family{json::digest(json::member(f,"code_sha256")),json::digest(json::member(f,"implementation_sha256"))};OperatorRegistry registry;need(bool(register_int16_operators(registry,family)),"register exact actual source/program identities");
 auto schema=read(Path(argv[2])/"args-schema.bin");need(!vxl_v2_schema_check(schema.data(),schema.size(),128)&&content_digest(schema)==json::digest(json::member(f,"schema_sha256")),"real C core validates exact frozen relocation schema");
 vxunified::BackendIdentity hw;auto*h=json::member(f,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.device_generation=1;hw.provider_flags=1; // Explicit host fixture epoch, not a queried physical identity.
 DeviceCaps caps;caps.ps_base=0x10000000;caps.ps_bytes=1<<20;caps.pl_base=0x80000000;caps.pl_bytes=2ULL<<30;caps.gpu_address_bits=32;caps.ps_dma_bits=32;caps.pl_address_bits=40;caps.cycle_counter_bits=32;
 std::vector<Digest>ids;std::vector<vxunified::ProgramKey>keys;
 for(const char*name:{"A","B"}){
  auto package=load_package(Path(argv[1])/name);if(!package)std::fprintf(stderr,"PACKAGE %s %s\n",name,package.error->detail.c_str());need(bool(package),"real frozen readonly package loader");
  need(!prepare_int16_model(*package.value,registry,caps),"identity-less preparation fails closed");
  auto prepared=prepare_int16_model(*package.value,registry,caps,hw);if(!prepared)std::fprintf(stderr,"PLAN %s %s\n",name,prepared.error->detail.c_str());need(bool(prepared),"new network prepares without kernel or program change");
  const auto&p=*prepared.value;unsigned jobs=name[0]=='A'?2:3;need(p.nodes.size()==jobs&&p.total_jobs==jobs&&p.programs.size()==1,"bounded graph counts share one program");need(!p.policy.hardware.board_identity_verified,"host identity never upgrades to physical verification");ids.push_back(p.model_id);keys.push_back(p.programs[0]);
  MathContext context(p);
  for(const auto&t:p.tensors)if(t.desc.constant)context.bytes(t.desc.id,*package.value->assets().at(t.desc.asset).content);
  context.bytes(p.inputs[0],*package.value->assets().at("input.bin").content);
  for(const auto&n:p.nodes){auto op=registry.resolve(n.node.key.name,n.node.key.semantic_version,n.node.key.implementation_sha);need(bool(op),"same registered implementation");auto completion=op.value->execute(n.node,context);need(completion&&!completion.value->raw_cycles&&!completion.value->cycles_valid,"host math executes without invented GPU cycles");}
  const auto&out=context.data.at(p.outputs[0]);const auto&reference=*package.value->assets().at("reference.i64").content;need(out.size()*8==reference.size()&&!std::memcmp(out.data(),reference.data(),reference.size()),"entire output equals independent frozen Python integer reference");need(context.calls==jobs&&context.tiles==(name[0]=='A'?1u:2u),"actual math graph and tile count");
  for(unsigned fault=0;fault<6;fault++){auto wrong=hw;auto c=caps;if(fault==0)wrong.device_generation=0;if(fault==1)wrong.bit[0]^=1;if(fault==2)wrong.provider_flags=2;if(fault==3)c.preempt=true;if(fault==4)c.gpu_address_bits=64;if(fault==5)c.max_args_bytes=64;need(!prepare_int16_model(*package.value,registry,c,wrong),"identity/capability mismatch rejected before execution");}
  OperatorRegistry empty;need(!prepare_int16_model(*package.value,empty,caps,hw),"missing operator cannot run silently");
  std::printf("U11_MODEL_%s jobs=%u values=%zu EXACT_FROZEN_INT64_HOST_PE_MODEL gpu_raw_cycles=null\n",name,jobs,out.size());
 }
 need(ids[0]!=ids[1]&&keys[0].code==keys[1].code&&keys[0].schema==keys[1].schema&&keys[0].logical_program==keys[1].logical_program,"different graph/shape extends without new program or driver");
 for(const char*name:{"bad-shape","bad-layout","bad-dtype","bad-domain","bad-version","bad-code","bad-schema","old-program","no-job","burst-jobs","numeric-waiver","bad-threshold","bad-reference"}){auto package=load_package(Path(argv[1])/name);need(bool(package),"negative is a valid immutable package, not a loader error");auto plan=prepare_int16_model(*package.value,registry,caps,hw);if(plan)std::fprintf(stderr,"UNEXPECTED_PLAN %s\n",name);need(!plan,"semantic malformed graph/program/numerics rejected");}
 static_assert(int(DType::FP32)==0&&int(DType::I16)==1&&int(DType::I48)==2&&int(DType::U8)==3,"old dtype ordinals unchanged");
 auto legacy=identity_spec();legacy.key.name="test.legacy48";legacy.key.implementation_sha.fill(0x77);legacy.supported_types={DType::I48};legacy.supported_layouts={"PACKED_I48"};legacy.validate=[](const auto&in,const auto&,const auto&){return in.size()==1&&in[0].bytes==384?Result<void>::success():Result<void>::failure({EINVAL,"LEGACY_48","packed48 must retain six byte storage"});};auto legacy_key=legacy.key;legacy.prepare=[legacy_key](NodeId id,const auto&in,const auto&out,const auto&params,const auto&){need(in[0].bytes==384&&out[0].bytes==384,"existing packed48 input/output storage remains six bytes");return Result<PreparedNode>::success({id,legacy_key,{in[0].id},{out[0].id},params,{}});};OperatorRegistry legacy_registry;need(bool(legacy_registry.add(legacy)),"legacy test operator");auto old=load_package(Path(argv[1])/"legacy-packed48");need(bool(old),"load legacy48 fixture");OptimizationPolicy policy;policy.hardware=hw;need(bool(prepare_graph(*old.value,legacy_registry,caps,policy)),"new i48 signextended layout does not rewrite existing packed48");
 puts("U11_TWO_MODEL_EXTENSION_SOFTWARE_ONLY_PHYSICAL_ACCEPTANCE_OPEN");
}

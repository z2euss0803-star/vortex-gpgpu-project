#include <vxnn/ops_int16.hpp>
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U11_REGISTRY_ASSERT %s\n",s);std::exit(63);}}
static TensorDesc tensor(uint32_t id,DType type,const char*layout,Shape shape,uint64_t bytes){TensorDesc t;t.id=id;t.dtype=type;t.layout=layout;t.shape=std::move(shape);t.bytes=bytes;t.access=1;return t;}
struct RecordingContext:ExecutionContext{uint32_t op=0,id=0;Result<Completion>submit(const PreparedNode&n,uint32_t code)override{op=code;id=n.id;return Result<Completion>::success(Completion{});}};
int main(){
 Int16Family family;family.program_sha.fill(0x41);family.implementation_sha.fill(0x31);OperatorRegistry registry;
 need(bool(register_int16_operators(registry,family)),"register three native integer operators with one frozen family");
 need(!register_int16_operators(registry,family),"duplicate registration rejected transactionally");
 DeviceCaps caps;caps.gpu_address_bits=32;caps.ps_dma_bits=32;caps.cycle_counter_bits=32;
 auto a=tensor(1,DType::I16,"RM_I16",{8,8},128),b=tensor(2,DType::I16,"RM_I16",{8,8},128),out=tensor(3,DType::I48,"RM_I48S64",{8,8},512);out.access=2;
 for(uint32_t opcode:{uint32_t(VXNN_MATMUL_I16),uint32_t(VXNN_BIAS_I48),uint32_t(VXNN_RELU_I48)}){
  auto key=int16_operator_key(opcode,family);auto found=registry.resolve(key.name,key.semantic_version,key.implementation_sha);need(bool(found),"exact registered semantic key");auto spec=*found.value;
  std::vector<TensorDesc>inputs;
  if(opcode==VXNN_MATMUL_I16)inputs={a,b};
  else if(opcode==VXNN_BIAS_I48)inputs={tensor(1,DType::I48,"RM_I48S64",{8,8},512),tensor(2,DType::I32,"ROW_I32",{8},32)};
  else inputs={tensor(1,DType::I48,"RM_I48S64",{8,8},512)};
  need(bool(registry.validate(key,inputs,{},caps)),"native op accepts only explicit integer storage");auto shape=spec.infer_shapes(inputs,{},caps);need(shape&&*shape.value==std::vector<Shape>{{8,8}},"bounded shape inference");
  auto workspace=spec.workspace(inputs,{},caps);need(workspace&&*workspace.value==0,"scratch is in bounded program stack, no hidden graph allocation");
  auto prepared=spec.prepare(7,inputs,{out},{},caps);need(prepared&&prepared.value->program_requirements==std::vector<Digest>{family.program_sha},"all ops bind one exact program");
  RecordingContext context;auto result=spec.execute(*prepared.value,context);need(result&&context.id==7&&context.op==opcode&&!result.value->raw_cycles,"registered execute delegates typed opcode without invented GPU time");
  auto wrong=inputs;wrong[0].domain=AddressDomain::CPU;need(!registry.validate(key,wrong,{},caps),"no CPU pointer accepted as PL tensor");
  wrong=inputs;wrong[0].layout="PACKED_I48";need(!registry.validate(key,wrong,{},caps),"packed48 is not implicit signextended64");
  need(!registry.validate(key,inputs,{{"unbounded",uint64_t(1)}},caps),"unregistered scalar arguments rejected");
  auto wide=caps;wide.gpu_address_bits=64;need(!registry.validate(key,inputs,{},wide),"family has actual32bit address contract");
  auto invalid=out;invalid.dtype=DType::I16;need(!spec.prepare(7,inputs,{invalid},{},caps),"output dtype cannot change with matching shape");
 }
 auto key=int16_operator_key(VXNN_MATMUL_I16,family);auto inputs=std::vector<TensorDesc>{a,b};inputs[0].shape={7,8};inputs[0].bytes=112;need(!registry.validate(key,inputs,{},caps),"tail matrices forbidden before execution");
 inputs={a,b};inputs[1].shape={7,8};inputs[1].bytes=112;need(!registry.validate(key,inputs,{},caps),"K mismatch rejected");
 auto absent=family;absent.program_sha={};OperatorRegistry fresh;need(!register_int16_operators(fresh,absent),"no placeholder program identity");
 puts("U11_REGISTERED_INTEGER_OP_SHAPES_BINDING_DISPATCH_HOST_TEST_NOT_GPU gpu_raw_cycles=null");
}

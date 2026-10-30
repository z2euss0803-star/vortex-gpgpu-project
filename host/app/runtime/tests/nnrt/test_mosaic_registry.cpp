#include <vxnn/mosaic_registry.hpp>
#include <vxnn/mosaic_coverage.hpp>
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
static void need(bool v,const char*s){if(!v){std::fprintf(stderr,"U13_MOSAIC_REGISTRY_ASSERT %s\n",s);std::exit(80);}}
struct Recording:ExecutionContext {unsigned calls=0;Result<Completion>submit(const PreparedNode&,uint32_t)override{calls++;return Result<Completion>::success({});}};
static TensorDesc tensor(unsigned id,const MosaicOperand&o){TensorDesc t;t.id=id;t.dtype=o.type;t.layout=o.layout;t.shape=o.shape;t.bytes=o.bytes;t.access=o.constant?1:3;t.constant=o.constant;if(o.constant){t.asset="immutable-metadata-fixture";t.content_sha=content_digest("fixture");}return t;}
int main(){
 MosaicFamily family;family.program_sha=content_digest("actual program bound later");family.readonly_sha=content_digest("exact RO bound later");family.implementation_sha=content_digest("registry source");
 const char*hash="c6564333cf23245cedd82391cdf5054fdcf3fa677388e7ba6d05ca3d795df43d";
 for(unsigned i=0;i<32;i++)family.schema_sha[i]=uint8_t(std::strtoul(std::string(hash+i*2,2).c_str(),nullptr,16));
 OperatorRegistry registry;need(bool(register_mosaic_operators(registry,family)),"all six native Mosaic operators registered");
 DeviceCaps caps;caps.gpu_address_bits=32;caps.ps_dma_bits=32;caps.pl_address_bits=40;caps.cycle_counter_bits=32;
 Recording context;unsigned seen=0;
 for(auto dimensions:{Shape{12,8},Shape{256,256},Shape{1280,960}}){
  uint64_t w=dimensions[0],h=dimensions[1],ci=3;const unsigned channels[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};
  for(unsigned layer=0,phase=0;layer<16;layer++){
   const uint64_t co=channels[layer],k=layer==0||layer==15?9:3,stride=layer==1||layer==2?2:1,up=layer==13||layer==14?2:1,kpad=(ci*k*k+31)/32*32,oh=(h*up+stride-1)/stride,ow=(w*up+stride-1)/stride;
   for(unsigned part=0;part<(layer==15?2u:5u);part++,phase++){
    unsigned op=part+1;if(part==4)op=layer>=4&&layer<=12&&!(layer&1)?6:5;
    uint64_t flags=op<3?(op==1&&layer>=3&&layer<=11&&(layer&1)?1u:0u):2u;
    ScalarParams params{{"shape",Shape{ci,co,h,w,k,stride,up,kpad}},{"flags",flags},{"scale_bits",uint64_t(op<3?0x3f800000:0)},{"budget",uint64_t(4)}};
    // Independent operand contract derived from original model geometry.
    std::vector<MosaicOperand> expected,outputs;auto operand=[](unsigned role,DType type,const char*layout,Shape shape,bool constant=false){uint64_t n=type==DType::I16?2:4;for(auto v:shape)n*=v;return MosaicOperand{role,type,layout,std::move(shape),n,constant};};
    if(op==1){expected.push_back(operand(VXM_INPUT,DType::FP32,"CHW_F32",{ci,h,w}));outputs.push_back(operand(VXM_QUANT,DType::I16,"CHW_I16",{ci,h,w}));if(flags)outputs.push_back(operand(VXM_SKIP,DType::FP32,"CHW_F32",{ci,h,w}));}
    if(op==2){expected={operand(VXM_QUANT,DType::I16,"CHW_I16",{ci,h,w}),operand(VXM_WEIGHTS,DType::I16,"CO_KPAD_I16",{co,kpad},true),operand(VXM_COEFF,DType::FP32,"MOSAIC_COEFF_F32",{co*2},true)};outputs={operand(VXM_OUTPUT,DType::FP32,"CHW_F32",{co,oh,ow})};}
    if(op>=3){expected={operand(VXM_INPUT,DType::FP32,"CHW_F32",{co,oh,ow})};outputs={operand(op==3?VXM_STATS:VXM_OUTPUT,DType::FP32,op==3?"CO_STATS3_F32":"CHW_F32",op==3?Shape{co,3}:Shape{co,oh,ow})};}
    if(op==4){expected.push_back(operand(VXM_COEFF,DType::FP32,"MOSAIC_COEFF_F32",{co*4},true));expected.push_back(operand(VXM_STATS,DType::FP32,"CO_STATS3_F32",{co,3}));}
    if(op==6)expected.push_back(operand(VXM_SKIP,DType::FP32,"CHW_F32",{co,oh,ow}));
    std::vector<TensorDesc> in,out;for(size_t i=0;i<expected.size();i++)in.push_back(tensor(10+i,expected[i]));for(size_t i=0;i<outputs.size();i++)out.push_back(tensor(100+i,outputs[i]));
    auto key=mosaic_operator_key(op,family);auto found=registry.resolve(key.name,key.semantic_version,key.implementation_sha);need(bool(found),"version/code/RO/source bound key");auto spec=*found.value;
    auto call=inspect_mosaic_operator(op,in,params,caps);need(bool(call),"all77 phases for each geometry");auto cover=describe_mosaic_phase(dimensions[0],dimensions[1],phase);need(cover&&cover.value->opcode==op&&call.value->shape.items==cover.value->items&&call.value->jobs==(cover.value->items+3)/4,"exact full geometry item and bounded job counts");
    auto infer=spec.infer_shapes(in,params,caps);std::vector<Shape> shapes;for(const auto&o:outputs)shapes.push_back(o.shape);need(infer&&*infer.value==shapes,"independent exact output shape/arity");
    uint64_t work=64;if(op==2)work+=4*((kpad*32+512+63)/64*64);if(op==3){uint64_t tree=1;while(tree<oh*ow)tree*=2;work+=tree*8*4;}
    auto ws=spec.workspace(in,params,caps);need(ws&&*ws.value==work,"workspace includes dummy64 and every core");auto prepared=spec.prepare(1,in,out,params,caps);need(bool(prepared)&&prepared.value->program_requirements==std::vector<Digest>{family.program_sha},"prepare exact output metadata and program");need(bool(spec.execute(*prepared.value,context)),"typed dispatch");seen++;
    auto bad=in;bad[0].bytes--;need(!spec.validate(bad,params,caps),"short extent");bad=in;bad[0].domain=AddressDomain::CPU;need(!spec.validate(bad,params,caps),"CPU pointer domain");bad=in;bad[0].layout="SAME_BYTES_WRONG_LAYOUT";need(!spec.validate(bad,params,caps),"same bytes wrong layout");auto wrong=params;wrong["budget"]=uint64_t(4097);need(!spec.validate(in,wrong,caps),"original max4096 retained");wrong=params;wrong["raw_gpu_address"]=uint64_t(1);need(!spec.validate(in,wrong,caps),"untrusted raw address parameter");auto alias=out;alias[0].id=in[0].id;need(!spec.prepare(1,in,alias,params,caps),"SSA distinct outputs");auto low=caps;low.max_refs_per_job=7;need(!spec.validate(in,params,low),"eight refs required");
   }
   ci=co;h=oh;w=ow;
  }
 }
 need(seen==231&&context.calls==231,"77 phases by3 actual geometry profiles");auto broken=family;broken.readonly_sha={};OperatorRegistry untouched;need(!register_mosaic_operators(untouched,broken),"missing readonly identity refused");
 std::puts("U13_MOSAIC_REGISTERED_77_PHASES_THREE_GEOMETRIES_BOUNDED_DISPATCH_PASS_NOT_EXECUTION gpu_raw_cycles=null");
}

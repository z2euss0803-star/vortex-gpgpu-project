#include <vxnn/mosaic_registry.hpp>
#include <vxnn/mosaic_program_family.hpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace vxnn;
static unsigned checks;
static void ck(bool yes,const char*why){++checks;if(!yes){std::fprintf(stderr,"COOP_HOST_ASSERT %s check=%u\n",why,checks);std::exit(75);}}
static Digest digest(const char*s){Digest d{};for(unsigned i=0;i<32;i++)d[i]=uint8_t(std::strtoul(std::string(s+i*2,2).c_str(),nullptr,16));return d;}
static TensorDesc tensor(unsigned id,DType dtype,const char*layout,Shape shape,bool constant=false){TensorDesc t;t.id=id;t.dtype=dtype;t.layout=layout;t.shape=std::move(shape);t.bytes=dtype==DType::I16?2:4;for(auto n:t.shape)t.bytes*=n;t.access=constant?1:3;t.constant=constant;if(constant){t.asset="HOST_METADATA_ONLY";t.content_sha=content_digest("HOST operand fixture not a published program");}return t;}
static std::vector<TensorDesc>inputs(unsigned op,const Shape&g){
 uint64_t ci=g[0],co=g[1],h=g[2],w=g[3],oh=(h*g[6]+g[5]-1)/g[5],ow=(w*g[6]+g[5]-1)/g[5];std::vector<TensorDesc>in;
 if(op==1)return {tensor(1,DType::FP32,"CHW_F32",{ci,h,w})};
 if(op==2)return {tensor(1,DType::I16,"CHW_I16",{ci,h,w}),tensor(2,DType::I16,"CO_KPAD_I16",{co,g[7]},true),tensor(3,DType::FP32,"MOSAIC_COEFF_F32",{co*2},true)};
 in.push_back(tensor(1,DType::FP32,"CHW_F32",{co,oh,ow}));
 if(op==4||op>=7){in.push_back(tensor(2,DType::FP32,"MOSAIC_COEFF_F32",{co*4},true));in.push_back(tensor(3,DType::FP32,"CO_STATS3_F32",{co,3}));}
 if(op==6||op==8){in.push_back(tensor(4,DType::FP32,"CHW_F32",{co,oh,ow}));}return in;
}
int main(){
 DeviceCaps caps;caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
 MosaicFamily cooperative;cooperative.schema_sha=digest(mosaic_reuse_schema_sha256);cooperative.readonly_sha=digest("30e0e46cb30629f4a7de447405225e208b37f15aaf2be23603bbe4cb975b5643");cooperative.implementation_sha=content_digest("HOST metadata fixture without admission");
 MosaicFamily legacy=cooperative;legacy.schema_sha=digest("c6564333cf23245cedd82391cdf5054fdcf3fa677388e7ba6d05ca3d795df43d");legacy.program_sha=digest(mosaic_ro64_code_sha256);
 MosaicFamily fused=cooperative;fused.schema_sha=digest(mosaic_fused_schema_sha256);fused.program_sha=digest(mosaic_fused_code_sha256);
 ck(mosaic_program_variant(cooperative.schema_sha)==MosaicProgramVariant::REUSE,"explicit cooperative schema mapping");
 auto unknown=cooperative;unknown.schema_sha=content_digest("UNKNOWN schema negative fixture");ck(mosaic_published_code(unknown.schema_sha)==nullptr&&!mosaic_program_code_matches(unknown.schema_sha,legacy.program_sha),"unknown schema never falls back to legacy CODE");OperatorRegistry never;ck(!register_mosaic_operators(never,unknown),"unknown family refuses registration");
 ck(!mosaic_program_code_matches(cooperative.schema_sha,fused.program_sha),"R6 CODE cannot masquerade as ABI4");ck(!register_mosaic_operators(never,cooperative),"missing cooperative CODE identity refuses registration");
 cooperative.program_sha=fused.program_sha;ck(!register_mosaic_operators(never,cooperative),"old CODE with new schema refuses registration");
 const uint64_t channels[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};unsigned invocations=0;
 for(const auto&size:{Shape{12,8},Shape{16,20},Shape{1280,960}}){uint64_t ci=3,h=size[1],w=size[0];for(unsigned layer=0;layer<16;layer++){
  uint64_t co=channels[layer],k=layer==0||layer==15?9:3,stride=layer==1||layer==2?2:1,up=layer==13||layer==14?2:1,kpad=(ci*k*k+31)/32*32;Shape g{ci,co,h,w,k,stride,up,kpad};
  for(unsigned op=1;op<=8;op++)for(unsigned save=0;save<((op==1||op>=7)?2u:1u);save++){
   uint64_t flags=op>=7?save:op>=3?2:op==1?save:0;ScalarParams old{{"shape",g},{"flags",flags},{"scale_bits",uint64_t(op<3||op>=7?0x3f800000:0)},{"budget",uint64_t(3)}};auto old_in=inputs(op,g);auto original=inspect_mosaic_operator(op,old_in,old,caps);auto in=old_in;if(op==2)in[1]=tensor(2,DType::I16,"MB_KPAD_ROW8_I16_LE_V1",{(co+7)/8,kpad,8},true);ck(bool(original),"all old operand paths still inspect");
   for(uint64_t options:{0u,1u}){auto p=old;p["work_layout_version"]=uint64_t(1);p["profiling_options"]=options;p["schedule_version"]=uint64_t(2);p["weight_layout_version"]=uint64_t(2);auto result=inspect_mosaic_operator(op,in,p,caps,cooperative);if(!result)std::fprintf(stderr,"op%u layer%u %s\n",op,layer,result.error->detail.c_str());ck(bool(result),"all cooperative op/geometry/options inspect");const auto&a=result.value->args;
    ck(a.abi==4&&a.reserved==options&&a.bytes==128,"ABI4 controlled options word and128 bytes");uint64_t work=op==2?original.value->args.extent[VXM_WORK]+2560:op==3?original.value->args.extent[VXM_WORK]+1024:1088;
    ck(a.extent[VXM_WORK]==work&&result.value->shape.required[VXM_WORK]==work,"exact four-core raw/profile expansion");ck(result.value->shape.items==original.value->shape.items&&result.value->jobs==original.value->jobs,"same actual tiles/jobs and tail cardinality");
    for(unsigned role=0;role<7;role++)ck(a.extent[role]==(role==VXM_WEIGHTS&&op==2?((co+7)/8)*kpad*16:original.value->args.extent[role]),"numeric extents except explicit padded weight layout");
    ck(!inspect_mosaic_operator(op,in,p,caps,legacy)&&!inspect_mosaic_operator(op,in,p,caps,fused),"old families refuse cooperative parameter ABI");
    auto bad=p;bad["profiling_options"]=uint64_t(2);ck(!inspect_mosaic_operator(op,in,bad,caps),"unknown option bit rejected");bad=p;bad["profiling_options"]=true;ck(!inspect_mosaic_operator(op,in,bad,caps),"boolean options cannot masquerade as integer");bad=p;bad.erase("work_layout_version");ck(!inspect_mosaic_operator(op,in,bad,caps),"partial work policy rejected");bad=p;bad["work_layout_version"]=uint64_t(2);ck(!inspect_mosaic_operator(op,in,bad,caps),"unimplemented layout rejected");bad=p;bad["schedule_version"]=uint64_t(1);ck(!inspect_mosaic_operator(op,in,bad,caps),"schedule1 refused");bad=p;bad["weight_layout_version"]=uint64_t(1);ck(!inspect_mosaic_operator(op,in,bad,caps),"weight1 refused");bad=p;bad.erase("weight_layout_version");ck(!inspect_mosaic_operator(op,in,bad,caps),"complete explicit schedule/weights required");bad=p;bad["spatial_major"]=uint64_t(1);ck(!inspect_mosaic_operator(op,in,bad,caps),"schedule change cannot hide in same parameters");
    ck(!inspect_mosaic_operator(op,in,old,caps,cooperative),"cooperative family requires explicit work/profile parameters");++invocations;
   }
  }
  ci=co;h=(h*up+stride-1)/stride;w=(w*up+stride-1)/stride;
 }}
 std::printf("MOSAIC_REUSE_HOST_INSPECT_WORK_WEIGHT_PADDING_EXPLICIT_ABI4_PASS checks=%u invocations=%u gpu_raw_cycles=null\n",checks,invocations);
}

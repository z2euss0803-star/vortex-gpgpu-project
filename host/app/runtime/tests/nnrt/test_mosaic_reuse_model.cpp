// HOST metadata ownership fixture only. No transport, device I/O or arithmetic.
#include <vxnn/model_mosaic.hpp>
#include <vxnn/mosaic_lowering.hpp>
#include <vxnn/mosaic_program_family.hpp>
#include "device/vxnn/mosaic_wire_reuse_v4.h"
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
static unsigned checks;
static void ck(bool v,const char*s){++checks;if(!v){std::fprintf(stderr,"COOP_MODEL_ASSERT %s check=%u\n",s,checks);std::exit(76);}}
struct MetadataArenaBackend:IGpuBackend {
 DeviceCaps caps;unsigned allocations=0,releases=0,forbidden=0;BufferRef owned{};
 explicit MetadataArenaBackend(DeviceCaps c):caps(c){}
 Error denied(){++forbidden;return {ENOSYS,"HOST_METADATA_ONLY","GPU/transport operation forbidden"};}
 Result<DeviceCaps>capabilities()override{return Result<DeviceCaps>::success(caps);}
 Result<BufferRef>allocate(uint64_t n,AddressDomain d,uint32_t a)override{ck(!owned.handle&&d==AddressDomain::PL&&a==3,"only one declared metadata arena");++allocations;owned={77,3,0,n,a,1};return Result<BufferRef>::success(owned);}
 Result<void>release_buffer(BufferRef r)override{ck(r.handle==owned.handle&&r.object_generation==owned.object_generation,"metadata release exact owner");++releases;owned={};return Result<void>::success();}
 Result<void>copy(BufferRef,Bytes)override{return Result<void>::failure(denied());}
 Result<void>copy(BufferRef,MutableBytes)override{return Result<void>::failure(denied());}
 Result<void>copy(BufferRef,BufferRef)override{return Result<void>::failure(denied());}
 Result<ProgramRef>program_load(const Digest&)override{return Result<ProgramRef>::failure(denied());}
 Result<void>release_program(ProgramRef)override{return Result<void>::failure(denied());}
 Result<uint64_t>submit(const JobDesc&)override{return Result<uint64_t>::failure(denied());}
 Result<void>wait(uint64_t,uint64_t)override{return Result<void>::failure(denied());}
 Result<Completion>read_completion(uint64_t)override{return Result<Completion>::failure(denied());}
 Result<void>ack_reap(uint64_t)override{return Result<void>::failure(denied());}
};
int main(int argc,char**argv){try{
 ck(argc==2||argc==3,"package and optional expected rejection");auto package=load_package(Path(argv[1]));if(!package){std::fprintf(stderr,"LOAD %s\n",package.error->detail.c_str());}ck(bool(package),"actual secure package loader");const auto&pkg=*package.value;
 const auto&bytes=*pkg.assets().at("binding.json").content;auto parsed=json::parse(std::string(bytes.begin(),bytes.end()));auto*b=parsed.get();auto*h=json::member(b,"hardware");
 MosaicFamily f{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"readonly_sha256")),json::digest(json::member(b,"schema_sha256")),json::digest(json::member(b,"implementation_sha256"))};
 vxunified::BackendIdentity hw;hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=json::u32(json::member(h,"provider_flags"));hw.device_generation=1;
 DeviceCaps caps;caps.ps_base=json::u64(json::member(h,"ps_base"));caps.ps_bytes=json::u64(json::member(h,"ps_bytes"));caps.pl_base=json::u64(json::member(h,"pl_base"));caps.pl_bytes=json::u64(json::member(h,"pl_bytes"));caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
 OperatorRegistry registry;auto registered=register_mosaic_operators(registry,f);if(!registered){ck(argc==3,"valid package registration");std::puts("MOSAIC_HOST_EXPECTED_FAMILY_REJECTION_PASS");return 0;}
 auto prepared=prepare_mosaic(pkg,registry,caps,hw);if(argc==3){ck(!prepared,"content-valid semantic attack refused");std::printf("MOSAIC_HOST_EXPECTED_POLICY_REJECTION_PASS detail=%s\n",prepared.error->detail.c_str());return 0;}
 if(!prepared){std::fprintf(stderr,"PREPARE %s\n",prepared.error->detail.c_str());}ck(bool(prepared),"real model prepare");const auto&plan=*prepared.value;bool reuse=mosaic_program_variant(f.schema_sha)==MosaicProgramVariant::REUSE;bool coop=mosaic_program_variant(f.schema_sha)==MosaicProgramVariant::COOPERATIVE;ck(plan.nodes.size()==(json::has(b,"fusion_policy")?47u:77u),"full fused47 or legacy77 retained");
 MemoryBudget budget;budget.pl_bytes=caps.pl_bytes;budget.ps_bytes=caps.ps_bytes;budget.pl_reserved=8u<<20;auto arena=plan_arenas(plan,budget);ck(bool(arena),"declared guarded arena fits");MetadataArenaBackend backend(caps);auto created=ArenaStorage::create(plan,*arena.value,1,backend);ck(bool(created),"owned metadata storage");auto storage=*created.value;ck(bool(storage->allocate()),"metadata allocation yields READY bindings");
 std::map<TensorId,const TensorDesc*>desc;for(const auto&t:plan.tensors)desc[t.desc.id]=&t.desc;uint64_t lowered=0;
 for(const auto&pn:plan.nodes){unsigned op=0;for(unsigned i=1;i<=8;i++){auto key=mosaic_operator_key(i,f);if(!(key<pn.node.key)&&!(pn.node.key<key))op=i;}ck(op!=0,"registered node identity");std::vector<TensorDesc>in;for(auto id:pn.node.inputs)in.push_back(*desc.at(id));auto invocation=inspect_mosaic_operator(op,in,pn.node.parameters,caps,f);ck(bool(invocation),"family-aware typed inspect");ck(pn.workspace_bytes==invocation.value->args.extent[VXM_WORK],"model and inspector exact shared WORK");
  for(uint64_t batch:std::set<uint64_t>{0,pn.job_count-1}){
   auto low=lower_mosaic_batch(plan,f,*storage,pn.node.id,batch);if(!low)std::fprintf(stderr,"LOWER %s\n",low.error->detail.c_str());ck(bool(low),"real lowering first and last batch");const auto&v=*low.value;ck(v.args.abi==(reuse?4u:coop?3u:op>=7?2u:1u),"all opcodes correct explicit ABI");ck(v.args.first==batch*invocation.value->budget&&v.args.count==std::min<uint64_t>(invocation.value->budget,invocation.value->shape.items-v.args.first),"global tile first/count exact including tail");ck(v.references[VXM_WORK].bytes==pn.workspace_bytes,"lowered full workspace has exact extent");
   auto relocated=v.args;for(unsigned role=0;role<8;role++){ck(v.references[role].handle==77&&v.references[role].object_generation==3&&v.references[role].device_generation==1&&v.references[role].bytes==v.args.extent[role],"all8 exact owned refs");relocated.address[role]=uint32_t(0x80000000ULL+v.references[role].offset);}
   VxnnMosaicShape shape{};ck(!(reuse?vxnn_mosaic_validate_reuse_v4(&relocated,true,&shape):coop?vxnn_mosaic_validate_coop_v3(&relocated,true,&shape):vxnn_mosaic_validate_fused(&relocated,true,&shape)),"actual target validator accepts lowered offset geometry only");
   if(coop||reuse){ck(v.args.reserved==json::u32(json::member(json::member(b,"workspace_policy"),"profiling_options")),"options agree with explicit package policy");}++lowered;
  }
  ck(!lower_mosaic_batch(plan,f,*storage,pn.node.id,pn.job_count),"no batch beyond endpoint");
 }
 auto wrong=f;wrong.schema_sha=content_digest("unknown schema negative");ck(!lower_mosaic_batch(plan,wrong,*storage,1,0),"READY storage cannot admit unknown schema");wrong=f;wrong.program_sha[0]^=1;ck(!lower_mosaic_batch(plan,wrong,*storage,1,0),"READY storage cannot admit wrong CODE");auto bad_plan=plan;bad_plan.nodes[0].workspace_bytes++;ck(!lower_mosaic_batch(bad_plan,f,*storage,1,0),"changed planned WORK refused");ck(!lower_mosaic_batch(plan,f,*storage,1,UINT64_MAX),"huge batch fails before multiplication");
 ck(backend.allocations==1&&!backend.forbidden,"pure lowering emits no copies/programs/GPU calls");ck(bool(storage->release())&&backend.releases==1,"explicit metadata root retirement");ck(!lower_mosaic_batch(plan,f,*storage,1,0),"retired arena cannot lower");
 std::printf("MOSAIC_HOST_REAL_PACKAGE_PLAN_AND_LOWERING_PASS modern=%u nodes=%zu jobs=%llu lowered=%llu checks=%u gpu_raw_cycles=null\n",unsigned(coop||reuse),plan.nodes.size(),(unsigned long long)plan.total_jobs,(unsigned long long)lowered,checks);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"COOP_MODEL_EXCEPTION %s\n",e.what());return 77;}}

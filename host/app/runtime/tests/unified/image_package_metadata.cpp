/* Build-host package admission with DECLARED frozen caps only. No transport,
 * device owner, GPU execution, rendering or target performance measurement. */
#include <vxnn/model_hunet.hpp>
#include <vxnn/model_mosaic.hpp>
#include <vxnn/arena.hpp>
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <string>
using namespace vxnn;
int main(int argc,char**argv){
 try{
  json::check(argc==4||argc==5||argc==7,"kind/package/target plus optional explicit entry and declared HUNet presets directory/SHA required");
  const std::string kind=argv[1],destination=argv[3];json::check((kind=="hunet"||kind=="mosaic")&&destination.rfind("/usr/share/vxgpu/models/",0)==0&&destination.find("..") == std::string::npos,"bounded trusted installation request");
  auto loaded=load_package(Path(argv[2]));if(!loaded)throw std::runtime_error(loaded.error->category+":"+loaded.error->detail);const auto&package=*loaded.value;
  const auto&bytes=*package.assets().at("binding.json").content;auto parsed=json::parse(std::string(bytes.begin(),bytes.end()));auto*b=parsed.get();auto*h=json::member(b,"hardware");
  vxunified::BackendIdentity hw;hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=json::u32(json::member(h,"provider_flags"));hw.device_generation=1;hw.board_identity_verified=false;
  // DeviceCaps contains the Linux/system memory pools. The RV32 GPU aperture
  // (0x80000000) is a distinct address domain and must not enter this plan key.
  DeviceCaps caps;caps.ps_base=json::u64(json::member(h,"ps_base"));caps.ps_bytes=json::u64(json::member(h,"ps_bytes"));caps.pl_base=json::u64(json::member(h,"pl_base"));caps.pl_bytes=json::u64(json::member(h,"pl_bytes"));caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
  OperatorRegistry registry;
  auto code=json::digest(json::member(b,"code_sha256")),schema=json::digest(json::member(b,"schema_sha256")),implementation=json::digest(json::member(b,"implementation_sha256"));
  auto prepare=[&]()->Result<Plan>{
   if(kind=="hunet"){
    HunetFamily family{code,implementation,schema};auto registration=register_hunet_operators(registry,family);if(!registration)throw std::runtime_error(registration.error->detail);return prepare_hunet(package,registry,caps,hw);
   }
   MosaicFamily family{code,json::digest(json::member(b,"readonly_sha256")),schema,implementation};auto registration=register_mosaic_operators(registry,family);if(!registration)throw std::runtime_error(registration.error->detail);return prepare_mosaic(package,registry,caps,hw);
  };
  auto prepared=prepare();
  if(!prepared){throw std::runtime_error(prepared.error->category+":"+prepared.error->detail);}
  const auto&plan=*prepared.value;
  MemoryBudget memory;memory.ps_bytes=caps.ps_bytes;memory.pl_bytes=caps.pl_bytes;memory.pl_reserved=8u<<20;auto arena=plan_arenas(plan,memory);if(!arena)throw std::runtime_error(arena.error->category+":"+arena.error->detail);
  const std::string entry=argc>=5?argv[4]:kind;
  json::check(!entry.empty()&&entry.size()<=128&&std::all_of(entry.begin(),entry.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}),"bounded explicit installed entry");
  if(argc>=5){
   if(kind=="hunet"){
    std::vector<uint32_t>chosen;auto*policy=json::member(b,"cpu_gn_stats_nodes");for(size_t i=0;i<json::count(policy,16);i++)chosen.push_back(json::u32(json::at(policy,i)));
    const std::vector<uint32_t>all{2,9,19,25,32,42,52,55,62,69,76,83,90,97,100,107};
    json::check((entry=="hunet"&&chosen.empty())||(entry=="hunet-gn9"&&chosen==std::vector<uint32_t>{9})||(entry=="hunet-gn16"&&chosen==all),"entry must match explicit GN_STATS backend policy");
   }else{
    const bool smoke=entry=="mosaic-smoke"||entry=="mosaic-smoke-profile";const bool profile=entry=="mosaic-profile"||entry=="mosaic-smoke-profile";
    json::check(entry=="mosaic"||entry=="mosaic-profile"||entry=="mosaic-smoke"||entry=="mosaic-smoke-profile"||entry=="mosaic-b1024"||entry=="mosaic-b4096","known explicit Mosaic entry");
    auto*w=json::member(b,"workspace_policy");auto*batch=json::member(b,"batch_policy");const uint32_t budget=smoke?64:entry=="mosaic-b1024"?1024:entry=="mosaic-b4096"?4096:256;
    json::check(json::u32(json::member(b,"width"))==(smoke?12:1280)&&json::u32(json::member(b,"height"))==(smoke?8:960)&&json::u32(json::member(b,"budget"))==budget&&json::u32(json::member(batch,"convolution_budget"))==budget&&json::u32(json::member(batch,"pointwise_budget"))==(smoke?64:4096)&&json::u32(json::member(w,"abi"))==4&&json::u32(json::member(w,"schedule_version"))==2&&json::u32(json::member(w,"weight_layout_version"))==2&&json::u32(json::member(w,"profiling_options"))==uint32_t(profile),"entry must match full/smoke geometry, actual work budget and profiling policy");
   }
  }
  auto doc=json::parse("{}");auto put=[](json_object*o,const char*k,const std::string&v){json_object_object_add(o,k,json_object_new_string_len(v.data(),int(v.size())));};auto number=[](json_object*o,const char*k,uint64_t v){json_object_object_add(o,k,json_object_new_uint64(v));};
  auto*o=doc.get();put(o,"scope","ACTUAL_HOST_PACKAGE_PREPARATION_DECLARED_CAPS_NOT_DEVICE_OR_GPU");json_object_object_add(o,"gpu_raw_cycles",nullptr);json_object_object_add(o,"board_identity_verified",json_object_new_boolean(false));number(o,"nodes",plan.nodes.size());number(o,"gpu_jobs",plan.total_jobs);number(o,"arena_pl_peak_bytes",arena.value->pl_peak);number(o,"arena_ps_peak_bytes",arena.value->ps_peak);number(o,"arena_objects",arena.value->object_count);
  auto*m=json_object_new_object();put(m,"id",entry);put(m,"input_id",kind=="hunet"?"frozen-roi0":("frozen-"+std::to_string(json::u32(json::member(b,"width")))+"x"+std::to_string(json::u32(json::member(b,"height")))));put(m,"kind",kind);put(m,"package",destination);put(m,"model",digest_hex(package.model_id()));put(m,"plan",digest_hex(plan.plan_id));const auto&input=package.assets().at("input.f32");put(m,"input",digest_hex(input.sha));number(m,"input_bytes",input.content->size());number(m,"resident_bytes",package.resident_bytes());number(m,"job_budget_ns",60000000000ULL);
  auto*f=json_object_new_object();put(f,"code",digest_hex(code));put(f,"schema",digest_hex(schema));put(f,"implementation",digest_hex(implementation));if(kind=="mosaic")put(f,"readonly",json::str(json::member(b,"readonly_sha256")));json_object_object_add(m,"family",f);json_object_object_add(o,"installed_model",m);
  if(argc==7){
   const std::string directory=argv[5],pin=argv[6];json::check(kind=="hunet"&&directory=="/usr/share/vxgpu/hunet-fixed-inputs"&&pin.size()==64&&pin!=std::string(64,'0')&&std::all_of(pin.begin(),pin.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"explicit bounded installed reviewed preset pin");
   auto*p=json_object_new_object();put(p,"directory",directory);put(p,"sha256",pin);json_object_object_add(m,"presets",p);put(o,"preset_admission_scope","DECLARED_INSTALLED_PIN_ACTUAL_CONTENT_CHECKED_SEPARATELY");
  }

  std::puts(json::dump(o).c_str());return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"U24_PACKAGE_PREPARATION_ERROR %s\n",e.what());return 1;}
}

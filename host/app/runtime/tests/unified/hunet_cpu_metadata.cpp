/* Build-host package admission with DECLARED frozen caps only. No transport,
 * device owner, GPU execution, rendering or target performance measurement. */
#include <vxnn/model_hunet.hpp>
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <string>
using namespace vxnn;
int main(int argc,char**argv){
 try{
  json::check(argc==4,"kind, real package and target absolute installation path required");
  const std::string kind=argv[1],destination=argv[3];json::check(kind=="hunet"&&destination.rfind("/usr/share/vxgpu/models/",0)==0&&destination.find("..") == std::string::npos,"bounded trusted installation request");
  auto loaded=load_package(Path(argv[2]));if(!loaded)throw std::runtime_error(loaded.error->category+":"+loaded.error->detail);std::fprintf(stderr,"HUNET_HOST_SECURE_LOADER_ACCEPTED\n");const auto&package=*loaded.value;
  const auto&bytes=*package.assets().at("binding.json").content;auto parsed=json::parse(std::string(bytes.begin(),bytes.end()));auto*b=parsed.get();auto*h=json::member(b,"hardware");
  vxunified::BackendIdentity hw;hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=json::u32(json::member(h,"provider_flags"));hw.device_generation=1;hw.board_identity_verified=false;
  // DeviceCaps contains the Linux/system memory pools. The RV32 GPU aperture
  // (0x80000000) is a distinct address domain and must not enter this plan key.
  DeviceCaps caps;caps.ps_base=json::u64(json::member(h,"ps_base"));caps.ps_bytes=json::u64(json::member(h,"ps_bytes"));caps.pl_base=json::u64(json::member(h,"pl_base"));caps.pl_bytes=json::u64(json::member(h,"pl_bytes"));caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
  OperatorRegistry registry;
  auto code=json::digest(json::member(b,"code_sha256")),schema=json::digest(json::member(b,"schema_sha256")),implementation=json::digest(json::member(b,"implementation_sha256"));
  HunetFamily family{code,implementation,schema};auto registration=register_hunet_operators(registry,family);if(!registration)throw std::runtime_error(registration.error->detail);
  auto prepared=prepare_hunet(package,registry,caps,hw);
  if(!prepared){throw std::runtime_error(prepared.error->category+":"+prepared.error->detail);}
  const auto&plan=*prepared.value;
  for(const auto&n:plan.nodes)if(n.node.key.name=="Hunet.GnStatsCpuOrdered.v1")json::check(n.job_count==0&&n.workspace_bytes==0&&n.node.program_requirements.empty(),"CPU stats must own no GPU job, program or scratch");
  auto doc=json::parse("{}");auto put=[](json_object*o,const char*k,const std::string&v){json_object_object_add(o,k,json_object_new_string_len(v.data(),int(v.size())));};auto number=[](json_object*o,const char*k,uint64_t v){json_object_object_add(o,k,json_object_new_uint64(v));};
  auto*o=doc.get();put(o,"scope","ACTUAL_HOST_PACKAGE_PREPARATION_DECLARED_CAPS_NOT_DEVICE_OR_GPU");json_object_object_add(o,"gpu_raw_cycles",nullptr);json_object_object_add(o,"board_identity_verified",json_object_new_boolean(false));number(o,"nodes",plan.nodes.size());number(o,"gpu_jobs",plan.total_jobs);
  auto*m=json_object_new_object();put(m,"id",kind);put(m,"input_id",kind=="hunet"?"frozen-roi0":"frozen-1280x960");put(m,"kind",kind);put(m,"package",destination);put(m,"model",digest_hex(package.model_id()));put(m,"plan",digest_hex(plan.plan_id));const auto&input=package.assets().at("input.f32");put(m,"input",digest_hex(input.sha));number(m,"input_bytes",input.content->size());number(m,"resident_bytes",package.resident_bytes());number(m,"job_budget_ns",60000000000ULL);
  auto*f=json_object_new_object();put(f,"code",digest_hex(code));put(f,"schema",digest_hex(schema));put(f,"implementation",digest_hex(implementation));json_object_object_add(m,"family",f);json_object_object_add(o,"installed_model",m);
  std::puts(json::dump(o).c_str());return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"U24_PACKAGE_PREPARATION_ERROR %s\n",e.what());return 1;}
}

#include <vxunified/texture.hpp>
#include "scheduler_fixture.hpp"
#include "nnrt/package/json.hpp"
#include <fstream>
#include <unistd.h>
int main(int argc,char**argv){
 require(argc==4||argc==5,"native export source, texture package and expected ID required");
 auto source=value(import_result(argv[1]),"native exported source accepted by original file importer");
 require(source.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"native HOST scope must survive the legacy OFFLINE envelope");
 if(argc==4)require(source.lease.producer.boot_id=="boot"&&source.lease.producer.client_id=="B"&&source.lease.producer.request_id=="image"&&source.lease.plan==digest(21),"exact original native producer and plan survive file bridge");
 else require(std::string(argv[4])=="installed"&&source.lease.producer.boot_id=="nativeboot"&&source.lease.producer.client_id=="B"&&source.lease.producer.request_id=="one","actual installed producer survives file bridge");
 TexturePolicy policy;policy.package_directory=argv[2];auto id=vxnn::json::parse(std::string("\"")+argv[3]+"\"");policy.expected_package_id=vxnn::json::digest(id.get());
 auto prepared=make_texture(source,policy);if(!prepared)std::fprintf(stderr,"NATIVE_TEXTURE_BIND_ERROR code=%d category=%s detail=%s\n",prepared.error->code,prepared.error->category.c_str(),prepared.error->detail.c_str());
 auto package=value(prepared,"existing P8 dualLOD admission consumes native export");
 require(package->source_scope()==source.lease.scope&&package->source_result_sha()==source.result_sha&&package->source_tensor_sha()==source.tensor_sha,"P8 texture preserves original scope and tensor provenance");
 require(package->lod(0).size==1048576&&package->lod(1).size==16777216,"original P8 512 and 2048 opaque LOD extents");
 for(unsigned level=0;level<2;++level){auto bytes=package->lod(level);for(size_t i=3;i<bytes.size;i+=4)require(bytes.data[i]==255,"original opaque alpha unchanged");}
 char dir[]="/tmp/vx-origin-reject-XXXXXX";require(mkdtemp(dir),"private malformed origin fixtures");std::string root=dir;
 {std::ofstream f(root+"/output.f32",std::ios::binary);f.write(reinterpret_cast<const char*>(source.raw_fp32.data()),source.raw_fp32.size());require(bool(f),"fixture tensor write");}
 for(const auto*field:{"evidence_scope","plan","raw_legacy_sha256","device_generation","quant_binding_format"}){
  auto meta=vxnn::json::parse(source.original_json);auto*origin=vxnn::json::member(meta.get(),"native_origin");
  if(std::string(field)=="device_generation")json_object_object_add(origin,field,json_object_new_int(2));
  else json_object_object_add(origin,field,json_object_new_string("FORGED"));
  {std::ofstream f(root+"/result.json");f<<vxnn::json::dump(meta.get());require(bool(f),"fixture malformed metadata write");}
  require(!import_result(root+"/result.json"),"malformed native provenance cannot be silently downgraded to legacy metadata");
 }
 unlink((root+"/result.json").c_str());unlink((root+"/output.f32").c_str());rmdir(root.c_str());
 puts("U20_NATIVE_MOSAIC_FILE_TO_ORIGINAL_P8_TEXTURE_SCOPE_LINEAGE_AND_REJECTIONS_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");
}

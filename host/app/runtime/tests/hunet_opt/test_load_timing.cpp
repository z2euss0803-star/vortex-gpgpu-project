#include "../../tools/hunet_opt_timing.h"
#include <vxl/hunet_fused_client.h>
#include <vxl/hunet.h>
#include <cassert>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sys/ioctl.h>
namespace fs=std::filesystem;
static uint64_t weights_copied,model_copied;static unsigned copies,handles;static bool seal_fault;
extern "C" int __wrap_ioctl(int,unsigned long command,...){
 va_list ap;va_start(ap,command);auto*r=va_arg(ap,vxl_request*);va_end(ap);
 if(command==VXL_ALLOC||command==VXL_RESERVE){r->handle=(uint64_t(2)<<32)|++handles;return 0;}
 if(command==VXL_COPY_TO){copies++;if(r->length==23685056)weights_copied+=r->length;if(r->length==47541809)model_copied+=r->length;return 0;}
 if(command==VXL_HYBRID_SEAL){if(seal_fault){errno=EIO;return -1;}return 0;}
 assert(false);return -1;
}
static void hash_file(const fs::path&p,uint8_t sha[32]){
 std::ifstream f(p,std::ios::binary);assert(f);vxl_hunet_sha s;vxl_hunet_sha_init(&s);char b[4096];
 while(f){f.read(b,sizeof(b));vxl_hunet_sha_update(&s,b,(uint64_t)f.gcount());}vxl_hunet_sha_final(&s,sha);
}
static void asset(const fs::path&r,const std::string&name,uint64_t n,uint8_t sha[32]){
 fs::create_directories((r/name).parent_path());std::ofstream f(r/name,std::ios::binary);f.seekp(n-1);f.put(0);f.close();hash_file(r/name,sha);
}
static vxl_opt_load_timing measured;
int main(int argc,char**argv){
 assert(argc==2);fs::path root=argv[1];assert(!fs::exists(root));fs::create_directories(root);
 vxl_fused_binding_v3 b;memset(&b,0x11,sizeof(b));memset(b.v2.reserved,0,32);memset(b.reserved,0,32);
 auto&v=b.v2.v1;v.magic=VXL_HUNET_BINDING_MAGIC;v.version=3;v.bytes=512;v.profile_id=4;v.program_id=9;v.features=15;v.generation=v.state=0;
 fs::create_directories(root/"cpu");fs::copy_file("/proc/self/exe",root/"cpu/gelu.elf");hash_file(root/"cpu/gelu.elf",b.v2.cpu_binary_sha256);
 asset(root,"cpu-contract.json",1,b.v2.cpu_contract_sha256);asset(root,"cpu-stages.json",1,b.v2.cpu_stages_sha256);
 asset(root,"jobs.bin",1,v.jobs_sha256);asset(root,"base/model.onnx",47541809,v.model_sha256);asset(root,"base/matrix_descriptors.bin",1,v.aux_sha256);
 asset(root,"fusion-contract.json",1,b.fusion_contract_sha256);asset(root,"descriptors.bin",1,b.descriptors_sha256);asset(root,"layout.json",1,b.layout_sha256);
 asset(root,"program9/code.bin",64,v.program_sha256);asset(root,"base/weights.bin",23685056,v.weights_sha256);
 {std::ofstream f(root/"binding-fused.bin",std::ios::binary);f.write((const char*)&b,sizeof(b));}
 for(unsigned timed=0;timed<2;timed++){
  vxl_fused_client c{};c.gpu.fd=99;c.binding=b;copies=handles=0;weights_copied=model_copied=0;
  assert(!vxl_fused_client_load_timed(&c,root.c_str(),timed?&measured:nullptr));
  assert(c.gpu.loaded&&!c.gpu.stopped&&copies==2&&weights_copied==23685056&&model_copied==0);
 }
 assert(!(measured.flags&VXL_LOAD_INCOMPLETE)&&measured.result==0);
 uint64_t sum=0,model_read=0,weight_copy=0;
 for(unsigned i=0;i<measured.used;i++){auto&r=measured.rows[i];sum+=r.ns;if(r.asset_id==VXL_LOAD_MODEL&&r.field==VXL_LOAD_READ)model_read+=r.bytes;if(r.field==VXL_LOAD_WEIGHTS_COPY)weight_copy+=r.bytes;}
 assert(sum<=measured.end_ns-measured.start_ns&&model_read==47541809&&weight_copy==23685056);
 assert((weight_copy+4095)/4096==5783 && weight_copy%4096==1984); // Calculated block count, not measured CDMA.
 fs::rename(root/"base/model.onnx",root/"base/model.saved");
 {vxl_fused_client c{};c.gpu.fd=99;c.binding=b;copies=0;assert(vxl_fused_client_load_timed(&c,root.c_str(),&measured)<0);assert(copies==0&&measured.result<0&&(measured.flags&VXL_LOAD_INCOMPLETE));}
 fs::rename(root/"base/model.saved",root/"base/model.onnx");seal_fault=true;
 {vxl_fused_client c{};c.gpu.fd=99;c.binding=b;assert(vxl_fused_client_load_timed(&c,root.c_str(),&measured)==-EIO);assert(measured.result==-EIO&&(measured.flags&VXL_LOAD_INCOMPLETE));}
 puts("SYNTHETIC_LOADER_READ_HASH_UPLOAD_SEAL_TIMING_PASS_NOT_MODEL_NOT_GPU");return 0;
}

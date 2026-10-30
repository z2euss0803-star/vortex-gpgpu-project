#include <vxl/hunet_hybrid_client.h>
#include "../../tools/hunet_hybrid_session.h"
#include "../../tools/hunet_tool.h"
#include <cassert>
#include <filesystem>
extern "C" {
void runtime_fixture_init(int);
void runtime_fixture_free(void);
unsigned runtime_gpu_count(void);
unsigned runtime_cpu_count(void);
unsigned runtime_consumed_count(void);
}
static void execute(const std::string &path,HybridMode mode,int fault){
 runtime_fixture_init(fault);vxl_hybrid_client c{};c.gpu.fd=-1;assert(!vxl_hybrid_client_open(&c,"/dev/vortex0"));c.gpu.loaded=1;c.gpu.program_id=8;
 /* Handles are supplied by the control fixture, not an accepted release. */
 for(unsigned i=0;i<4;i++)c.gpu.handles[i]=(uint64_t(2)<<32)|(i==3?4:i+1);
 std::vector<uint8_t> input(196608);assert(!vxl_hunet_read_asset("../hunet_original_tcu_20261008_v1","data/board_inputs/p1_001.f32",input.data(),input.size()));
 vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.run_id[0]=0x12;b.sample_kind=4;b.input_ptr=(uintptr_t)input.data();b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
 ArtifactWriter writer(path);HybridRaw raw;raw.checkpoint=[&](const std::string &name,const void *p,size_t n){writer.write(name,p,n);};int rc=hunet_hybrid_execute(&c,b,mode,&raw);
 if(mode==HybridMode::FORMAL){assert(rc==-EPERM&&runtime_gpu_count()==0&&runtime_cpu_count()==0);}
 else if(fault){assert(rc==-EDOM&&runtime_gpu_count()==295&&runtime_cpu_count()==0);assert(writer.files.count("failure.json")&&writer.files.count("cpu/008-input.f32")&&!writer.files.count("cpu/008-output.f32"));}
 else if(mode==HybridMode::FIRST_GELU){assert(!rc&&raw.prefix_stop&&raw.logical_completed==392&&raw.gpu.size()==391&&raw.cpu.size()==1);assert(runtime_gpu_count()==391&&runtime_cpu_count()==1&&runtime_consumed_count()==1);assert(writer.files.count("stages/009.f32")&&!writer.files.count("end.bin"));assert(writer.files.count("packs/0297.f16"));}
 else {assert(!rc&&!raw.prefix_stop&&raw.logical_completed==1023&&raw.gpu.size()==1010&&raw.cpu.size()==13);assert(runtime_gpu_count()==1010&&runtime_cpu_count()==13&&runtime_consumed_count()==13);assert(writer.files.count("end.bin")&&writer.files.count("cpu/098-reference.f32")&&writer.files.count("stages/099.f32"));}
 assert(!vxl_hybrid_client_close(&c));runtime_fixture_free();
}
int main(int argc,char **argv){
 assert(argc==2);std::string root=argv[1];std::filesystem::create_directories(root);
 execute(root+"/prefix",HybridMode::FIRST_GELU,0);execute(root+"/diagnostic",HybridMode::DIAGNOSTIC,0);execute(root+"/cpu-failure",HybridMode::DIAGNOSTIC,1);execute(root+"/formal-refused",HybridMode::FORMAL,0);
 runtime_fixture_init(2);vxl_hybrid_client c{};assert(vxl_hybrid_client_open(&c,"/dev/vortex0")<0);runtime_fixture_free();
 puts("HOST_LIVE_BUFFER_PREFIX_DIAGNOSTIC_CPU_ERROR_CHECKPOINT_PASS_NOT_MODEL_OR_BOARD");return 0;
}

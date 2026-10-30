#include <vxl/hunet_fused_client.h>
#include "../../tools/hunet_fused_session.h"
#include "../../tools/hunet_fused_timing.h"
#include "../../tools/hunet_tool.h"
#include <cassert>
#include <filesystem>
extern "C" {void runtime_fixture_init(int);void runtime_fixture_free(void);unsigned runtime_gpu_count(void);unsigned runtime_cpu_count(void);unsigned runtime_consumed_count(void);}
static void run(const std::string&path,FusedMode mode,int fault,bool allow=false){
 runtime_fixture_init(fault);vxl_fused_client c{};c.gpu.fd=-1;assert(!vxl_fused_client_open(&c,"/dev/vortex0"));c.gpu.loaded=1;c.gpu.program_id=9;
 for(unsigned i=0;i<4;i++)c.gpu.handles[i]=(uint64_t(2)<<32)|(i==3?4:i+1);
 std::vector<uint8_t> input(196608);assert(!vxl_hunet_read_asset("../hunet_original_tcu_20261008_v1","data/board_inputs/p1_001.f32",input.data(),input.size()));
 vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.run_id[0]=0x12;b.sample_kind=4;b.input_ptr=(uintptr_t)input.data();b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
 ArtifactWriter writer(path);FusedRaw r;r.checkpoint=[&](const std::string&n,const void*p,size_t bytes){writer.write(n,p,bytes);};
 if(allow){auto binding=c.binding;binding.v2.v1.generation=binding.v2.v1.state=0;vxl_hunet_hash(&binding,512,r.numeric_gate_binding.data());r.measured_authorized=true;}
 int rc=hunet_fused_execute(&c,b,mode,&r);writer.write("timing.json",hunet_fused_timings(r));
 assert(r.cpu_detail.size()==r.cpu.size());
 for(size_t i=0;i<r.cpu.size();i++){
  const auto&d=r.cpu_detail[i];const auto&p=r.cpu[i];assert(d.handler_ns>=d.checkpoint_ns);
  if(!fault)assert(d.input_sha_ns&&d.output_sha_ns&&d.gelu_call_ns>=p.timing.duration_ns&&d.handler_ns>=d.checkpoint_ns+d.input_sha_ns+d.output_sha_ns+d.gelu_call_ns+p.begin_call_ns+p.commit_call_ns+p.evidence_call_ns+p.reference_ns+p.writeback_capture_ns);
 }
 if(mode!=FusedMode::MEASURED||allow)assert(r.roi_begin_call_ns);
 if(!fault&&mode!=FusedMode::STAGE0&&(mode!=FusedMode::MEASURED||allow))assert(r.roi_end_call_ns);
 if(mode==FusedMode::MEASURED&&!allow){assert(rc==-EPERM&&!runtime_gpu_count()&&!runtime_cpu_count());}
 else if(fault){assert(rc==-EDOM&&r.gpu.size()==10&&runtime_gpu_count()==10&&!runtime_cpu_count());assert(writer.files.count("failure.json"));}
 else if(mode==FusedMode::STAGE0){assert(!rc&&r.prefix_stop&&r.gpu.size()==1&&r.cpu.empty()&&r.logical_completed==96);assert(writer.files.count("stages/000.f32")&&!writer.files.count("end.bin"));}
 else{assert(!rc&&r.gpu.size()==103&&r.cpu.size()==13&&r.logical_completed==1023);assert(runtime_gpu_count()==103&&runtime_cpu_count()==13&&runtime_consumed_count()==13);assert(r.stage_count==100&&writer.files.count("end.bin"));
  if(allow){assert(r.captured_stages==0&&!r.diagnostic_capture_ns&&!writer.files.count("stages/099.f32")&&!writer.files.count("cpu/008-input.f32"));for(const auto&p:r.cpu)assert(!p.reference_ns&&!p.writeback_capture_ns);}
  else assert(r.captured_stages==100&&writer.files.count("stages/099.f32"));}
 for(const auto&f:writer.files)assert(f.first.find("packs/")==std::string::npos&&f.first.find("traces/")==std::string::npos&&f.first.find("logical/")==std::string::npos);
 assert(!vxl_fused_client_close(&c));runtime_fixture_free();
}
int main(int argc,char**argv){assert(argc==2);std::string p=argv[1];std::filesystem::create_directories(p);run(p+"/stage0",FusedMode::STAGE0,0);run(p+"/diagnostic",FusedMode::DIAGNOSTIC,0);run(p+"/fault",FusedMode::DIAGNOSTIC,1);run(p+"/measured-refused",FusedMode::MEASURED,0);run(p+"/measured-TEST-ONLY-gate",FusedMode::MEASURED,0,true);puts("FUSED_LIVE_CPU_BUFFERS_103_GPU_13_CPU_STAGE_COVERAGE_FAILSTOP_PASS_NOT_GPU");}

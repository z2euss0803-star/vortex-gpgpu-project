#include <vxl/hunet_opt_client.h>
#include "../../tools/hunet_opt_session.h"
#include "../../tools/hunet_opt_timing_json.h"
#include "../../tools/hunet_tool.h"
#include <cassert>
#include <filesystem>
extern "C" {void runtime_fixture_init(int);void runtime_fixture_free(void);unsigned runtime_gpu_count(void);unsigned runtime_cpu_count(void);unsigned runtime_consumed_count(void);}
static void run(const std::string&path,OptMode mode,int fault,bool allow=false){
 runtime_fixture_init(fault);vxl_opt_client c{};c.gpu.fd=-1;assert(!vxl_opt_client_open(&c,"/dev/vortex0",vxl_opt_catalog_default(),(vxl_opt_catalog_default()->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE));c.gpu.loaded=1;c.gpu.program_id=10;
 for(unsigned i=0;i<4;i++)c.gpu.handles[i]=(uint64_t(2)<<32)|(i==3?4:i+1);
 std::vector<uint8_t> input(196608);assert(!vxl_hunet_read_asset("../hunet_original_tcu_20261008_v1","data/board_inputs/p1_001.f32",input.data(),input.size()));
 vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.run_id[0]=0x12;b.sample_kind=4;b.input_ptr=(uintptr_t)input.data();b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
 ArtifactWriter writer(path);OptRunResult r;r.scope="HOST_SOFTWARE_NOT_GPU";r.checkpoint=[&](const std::string&n,const void*p,size_t bytes){writer.write(n,p,bytes);};
 if(allow){auto binding=c.binding;binding.v3.v2.v1.generation=binding.v3.v2.v1.state=0;vxl_hunet_hash(&binding,512,r.numeric_gate_binding.data());r.measured_authorized=true;}
 if(mode==OptMode::EXPLORE)r.exploration_authorized=allow;
 int rc=hunet_opt_execute(&c,b,mode,&r);writer.write("timing.json",hunet_opt_timings(r));
 assert(r.cpu_detail.size()==r.cpu.size());
 for(size_t i=0;i<r.cpu.size();i++){
  const auto&d=r.cpu_detail[i];const auto&p=r.cpu[i];assert(d.handler_ns>=d.checkpoint_ns);
  if(!fault)assert(d.input_sha_ns&&d.output_sha_ns&&d.gelu_call_ns>=p.timing.duration_ns&&d.handler_ns>=d.checkpoint_ns+d.input_sha_ns+d.output_sha_ns+d.gelu_call_ns+p.begin_call_ns+p.commit_call_ns+p.evidence_call_ns+p.reference_ns+p.writeback_capture_ns);
 }
 if((mode!=OptMode::MEASURED&&mode!=OptMode::EXPLORE)||allow)assert(r.roi_begin_call_ns);
 if(!fault&&mode!=OptMode::STAGE0&&((mode!=OptMode::MEASURED&&mode!=OptMode::EXPLORE)||allow))assert(r.roi_end_call_ns);
 if((mode==OptMode::MEASURED||mode==OptMode::EXPLORE)&&!allow){assert(rc==-EPERM&&!runtime_gpu_count()&&!runtime_cpu_count());}
 else if(fault==4){assert(rc==-EIO&&r.gpu.size()==1&&runtime_gpu_count()==1&&!runtime_cpu_count()&&writer.files.count("failure.json"));}
 else if(fault){assert(rc==-EDOM&&r.gpu.size()>0&&runtime_gpu_count()==r.gpu.size()&&!runtime_cpu_count());assert(writer.files.count("failure.json"));}
 else if(mode==OptMode::STAGE0){assert(!rc&&r.prefix_stop&&r.gpu.size()==1&&r.cpu.empty()&&r.logical_completed==96);assert(!writer.files.count("stages/000.f32")&&!writer.files.count("end.bin"));}
 else{assert(!rc&&r.gpu.size()==c.catalog->gpu_jobs&&r.cpu.size()==13&&r.logical_completed==1023);assert(runtime_gpu_count()==c.catalog->gpu_jobs&&runtime_cpu_count()==13&&runtime_consumed_count()==13);assert(r.stage_count==100&&writer.files.count("end.bin"));
  if(allow){assert(r.captured_stages==0&&!r.diagnostic_capture_ns&&!writer.files.count("stages/099.f32")&&!writer.files.count("cpu/008-input.f32"));for(const auto&p:r.cpu)assert(!p.reference_ns&&!p.writeback_capture_ns);for(const auto&f:writer.files)assert(f.first.find("internal/")!=0);}
  else{assert(r.captured_stages>0&&writer.files.count("stages/099.f32"));if(c.catalog->runtime_flags&4u){unsigned detail=0;for(const auto&f:writer.files)if(f.first.find("internal/")==0){assert(f.second.first==448);detail++;}assert(detail==16);}}}
 for(const auto&f:writer.files)assert(f.first.find("packs/")==std::string::npos&&f.first.find("traces/")==std::string::npos&&f.first.find("logical/")==std::string::npos);
 assert(!vxl_opt_client_close(&c));runtime_fixture_free();
}
int main(int argc,char**argv){for(int fail:{2,3}){runtime_fixture_init(fail);vxl_opt_client wrong{};wrong.gpu.fd=-1;assert(vxl_opt_client_open(&wrong,"/dev/vortex0",vxl_opt_catalog_default(),(vxl_opt_catalog_default()->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE));assert(!runtime_gpu_count());runtime_fixture_free();}
 assert(argc==2);std::string p=argv[1];std::filesystem::create_directories(p);run(p+"/stage0",OptMode::STAGE0,0);run(p+"/diagnostic",OptMode::DIAGNOSTIC,0);run(p+"/fault",OptMode::DIAGNOSTIC,1);run(p+"/owner-fault",OptMode::EXPLORE,4,true);run(p+"/explore",OptMode::EXPLORE,0,true);run(p+"/explore-refused",OptMode::EXPLORE,0);run(p+"/measured-refused",OptMode::MEASURED,0);run(p+"/measured-TEST-ONLY-gate",OptMode::MEASURED,0,true);puts("HOST_OPT_LIVE_CPU_BUFFERS_CATALOG_GPU_13_CPU_RANGE_FAILSTOP_PASS_NOT_GPU");}

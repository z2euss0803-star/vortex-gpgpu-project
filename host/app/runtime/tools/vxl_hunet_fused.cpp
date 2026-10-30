#include "hunet_tool.h"
#include "hunet_fused_session.h"
#include "hunet_fused_timing.h"
#include "hunet_timing.h"
#include <memory>
#include <cmath>
#include <dlfcn.h>
#include <limits.h>
struct FusedClientGuard {vxl_fused_client c{};FusedClientGuard(){c.gpu.fd=-1;}~FusedClientGuard(){vxl_fused_client_close(&c);}};
static std::string cpu_runtime(uint8_t out_sha[32]){
 Dl_info info{};char path[PATH_MAX];if(!dladdr(reinterpret_cast<void *>(::erff),&info)||!info.dli_fname||!realpath(info.dli_fname,path))throw std::runtime_error("LIBM_IDENTITY");
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)throw std::runtime_error("LIBM_OPEN");vxl_hunet_sha hash{};vxl_hunet_sha_init(&hash);uint8_t bytes[4096];ssize_t n;uint64_t total=0;
 while((n=read(fd,bytes,sizeof(bytes)))!=0){if(n<0){if(errno==EINTR)continue;close(fd);throw std::runtime_error("LIBM_READ");}total+=uint64_t(n);vxl_hunet_sha_update(&hash,bytes,uint64_t(n));}
 if(close(fd))throw std::runtime_error("LIBM_CLOSE");
 uint8_t sha[32];vxl_hunet_sha_final(&hash,sha);
 memcpy(out_sha,sha,32);return "{\"compiler\":"+quote(__VERSION__)+",\"algorithm\":\"ERF_NATIVE_V1\",\"threads\":4,\"precision_flags\":[\"-fno-fast-math\",\"-ffp-contract=off\"],\"libm_path\":"+quote(path)+",\"libm_bytes\":"+std::to_string(total)+",\"libm_sha256\":"+quote(hex(sha,32))+"}\n";
}
static void measured_gate(const std::string&path,const vxl_fused_binding_v3&binding,unsigned roi,FusedRaw&r){
 /* Controller-created record: magic8, flags7, roi, bindingSHA, auditSHA,
    diagnostic run16, reserved32. It is authority evidence, not a signature. */
 uint8_t gate[128],sha[32];auto slash=path.find_last_of('/');if(path.empty()||path[0]!='/'||slash==std::string::npos)throw std::runtime_error("GATE_ABSOLUTE_PATH_REQUIRED");
 require(vxl_hunet_read_asset(path.substr(0,slash).c_str(),path.substr(slash+1).c_str(),gate,128),"gate");
 uint32_t flags,gate_roi;memcpy(&flags,gate+8,4);memcpy(&gate_roi,gate+12,4);auto b=binding;b.v2.v1.generation=b.v2.v1.state=0;vxl_hunet_hash(&b,512,sha);
 uint8_t audit=0,run=0,reserved=0;for(unsigned i=0;i<32;i++){audit|=gate[48+i];reserved|=gate[96+i];}for(unsigned i=0;i<16;i++)run|=gate[80+i];
 if(memcmp(gate,"VXFUSG1",8)||flags!=7||gate_roi!=roi||memcmp(gate+16,sha,32)||!audit||!run||reserved)throw std::runtime_error("NUMERIC_AUTHORITY_GATE_MISMATCH");
 r.measured_authorized=true;memcpy(r.numeric_gate_binding.data(),sha,32);
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--help")){puts("NO_DEFAULT_COMPUTE: --release DIR --roi 0 --mode stage0|diagnostic|measured --out NEWDIR [--run-id 32HEX --kind first|warmup|measured|diagnostic --index N --gate ABSOLUTE_FILE]; measured requires numeric and human authority gate");return 0;}
 std::unique_ptr<ArtifactWriter>writer;
 try{
  auto o=options(argc,argv);for(auto k:{"--release","--roi","--mode","--out"})if(!o.count(k))throw std::runtime_error("MISSING_OPTION");writer.reset(new ArtifactWriter(o["--out"]));
  for(const auto&p:o)if(p.first!="--release"&&p.first!="--roi"&&p.first!="--mode"&&p.first!="--out"&&p.first!="--run-id"&&p.first!="--kind"&&p.first!="--index"&&p.first!="--gate")throw std::runtime_error("UNKNOWN_OPTION");
  unsigned roi=number(o["--roi"]),index=o.count("--index")?number(o["--index"]):0;if(roi!=0)throw std::runtime_error("ROI0_ONLY");
  FusedMode mode;if(o["--mode"]=="stage0")mode=FusedMode::STAGE0;else if(o["--mode"]=="diagnostic")mode=FusedMode::DIAGNOSTIC;else if(o["--mode"]=="measured")mode=FusedMode::MEASURED;else throw std::runtime_error("BAD_MODE");
  if(mode==FusedMode::MEASURED&&!o.count("--gate"))throw std::runtime_error("MEASURED_EXTERNAL_NUMERIC_AND_AUTH_GATE_REQUIRED");
  vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.roi_index=roi;b.sample_index=index;auto kind=o.count("--kind")?o["--kind"]:"diagnostic";b.sample_kind=kind=="first"?1:kind=="warmup"?2:kind=="measured"?3:kind=="diagnostic"?4:0;if(!b.sample_kind)throw std::runtime_error("BAD_KIND");
  if(mode!=FusedMode::MEASURED&&b.sample_kind!=4)throw std::runtime_error("DIAGNOSTIC_TIMING_NOT_FORMAL_SAMPLE");
  if(mode==FusedMode::MEASURED&&b.sample_kind==4)throw std::runtime_error("MEASURED_REQUIRES_FORMAL_SAMPLE_KIND");
  if(o.count("--run-id")){auto id=o["--run-id"];if(id.size()!=32||id.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("BAD_RUN_ID");for(unsigned i=0;i<16;i++)b.run_id[i]=uint8_t(strtoul(id.substr(i*2,2).c_str(),nullptr,16));}
  else{int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0||read(fd,b.run_id,16)!=16)throw std::runtime_error("RUN_ID_RANDOM");close(fd);}
  std::vector<uint8_t>input(196608);require(vxl_hunet_read_asset(o["--release"].c_str(),"base/inputs/000.f32",input.data(),input.size()),"input");b.input_ptr=(uintptr_t)input.data();b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
  std::string boot=text_file("/proc/sys/kernel/random/boot_id"),build=text_file("/etc/codex-build-id");utsname un{};if(uname(&un))throw std::runtime_error("UNAME");
  FusedClientGuard client;require(vxl_fused_client_open(&client.c,"/dev/vortex0"),"open");writer->write("binding-fused.bin",&client.c.binding,512);
  uint8_t runtime_sha[32],expected[65];writer->write("cpu-runtime.json",cpu_runtime(runtime_sha));require(vxl_hunet_read_asset(o["--release"].c_str(),"cpu/libm.sha256",expected,65),"libm expected");if(std::string((char*)expected,64)!=hex(runtime_sha,32)||expected[64]!='\n')throw std::runtime_error("CPU_LIBM_CHANGED");
  FusedRaw r;if(mode==FusedMode::MEASURED)measured_gate(o["--gate"],client.c.binding,roi,r);
  uint64_t start=hunet_now();require(vxl_fused_client_load(&client.c,o["--release"].c_str()),"load");r.load_ns=hunet_duration(start,hunet_now());
  r.checkpoint=[&](const std::string&n,const void*p,size_t size){writer->write(n,p,size);};int rc=hunet_fused_execute(&client.c,b,mode,&r);writer->write("timing.json",hunet_fused_timings(r));
  std::string meta="{\"scope\":\"HUNET_FUSED_LINUX_V1\",\"state\":"+quote(rc?"FAIL_STOP":r.prefix_stop?"PREFIX_STOP":"RAW_COMPLETE_REQUIRES_AUDIT")+",\"error\":"+std::to_string(rc)+",\"boot_id\":"+quote(boot)+",\"build_id\":"+quote(build)+",\"kernel\":"+quote(un.release)+",\"profile\":4,\"program_id\":9,\"generation\":"+std::to_string(client.c.binding.v2.v1.generation)+",\"run_id\":"+quote(hex(b.run_id,16))+",\"roi_index\":"+std::to_string(roi)+",\"sample_kind\":"+std::to_string(b.sample_kind)+",\"sample_index\":"+std::to_string(index)+",\"mode\":"+quote(o["--mode"])+",\"logical_completed\":"+std::to_string(r.logical_completed)+",\"gpu_records\":"+std::to_string(r.gpu.size())+",\"cpu_records\":"+std::to_string(r.cpu.size())+",\"gpu_record_bytes\":"+std::to_string(sizeof(FusedGpuRecord))+",\"cpu_record_bytes\":"+std::to_string(sizeof(HybridCpuRecord))+",\"stage_count\":"+std::to_string(r.stage_count)+",\"captured_stages\":"+std::to_string(r.captured_stages)+",\"T_load_ns\":"+std::to_string(r.load_ns)+",\"T_resident_ns\":"+std::to_string(r.resident_ns)+",\"T_checkpoint_ns\":"+std::to_string(r.checkpoint_ns)+",\"T_diagnostic_capture_ns\":"+std::to_string(r.diagnostic_capture_ns)+",\"T_final_read_ns\":"+std::to_string(r.final_read_ns)+",\"gpu_seconds\":null,\"files\":"+writer->manifest()+"}\n";
  writer->write("run.json",meta);int close_rc=vxl_fused_client_close(&client.c);if(close_rc){writer->write("close-failure.json","{\"error\":"+std::to_string(close_rc)+"}\n");return 1;}
  puts(rc?"HUNET_FUSED_FAIL_STOP_EVIDENCE_RETAINED":r.prefix_stop?"HUNET_FUSED_STAGE0_RAW_REQUIRES_AUDIT":"HUNET_FUSED_RAW_COMPLETE_REQUIRES_AUDIT");return rc?1:0;
 }catch(const std::exception&e){if(writer){try{writer->write("startup-failure.json","{\"error\":"+quote(e.what())+"}\n");}catch(...){}}fprintf(stderr,"FAIL_STOP: %s\n",e.what());return 1;}
}

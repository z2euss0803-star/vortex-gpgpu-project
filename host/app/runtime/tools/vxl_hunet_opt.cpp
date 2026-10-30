#include "hunet_tool.h"
#include "hunet_opt_session.h"
#include "hunet_opt_timing_json.h"
#include "hunet_timing.h"
#include <memory>
#include <cmath>
#include <dlfcn.h>
#include <limits.h>
struct OptClientGuard {vxl_opt_client c{};OptClientGuard(){c.gpu.fd=-1;}~OptClientGuard(){vxl_opt_client_close(&c);}};
static void envelope(ArtifactWriter&w,const char*name,uint32_t magic,const void*p,size_t bytes){
 uint32_t header[4]={magic,1,uint32_t(bytes+16),0};std::vector<uint8_t>blob(bytes+16);memcpy(blob.data(),header,16);memcpy(blob.data()+16,p,bytes);w.write(name,blob.data(),blob.size());
}
static void driver_snapshots(ArtifactWriter&w){
 for(auto entry:{std::make_pair("management.csv","/sys/kernel/debug/vxl_mgmt_timing/transactions.csv"),std::make_pair("submit.csv","/sys/kernel/debug/vxl_submit_timing/jobs.csv"),std::make_pair("kernel-digest.csv","/sys/kernel/debug/vxl_mgmt_timing/digest.csv")}){
  std::ifstream f(entry.second);if(!f)throw std::runtime_error("DRIVER_SNAPSHOT_OPEN");std::string text((std::istreambuf_iterator<char>(f)),{});if(f.bad()||text.empty()||text.size()>16*1024*1024)throw std::runtime_error("DRIVER_SNAPSHOT_READ");w.write(entry.first,text);
 }
}
static std::string cpu_runtime(uint8_t out_sha[32]){
 Dl_info info{};char path[PATH_MAX];if(!dladdr(reinterpret_cast<void *>(::erff),&info)||!info.dli_fname||!realpath(info.dli_fname,path))throw std::runtime_error("LIBM_IDENTITY");
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)throw std::runtime_error("LIBM_OPEN");vxl_hunet_sha hash{};vxl_hunet_sha_init(&hash);uint8_t bytes[4096];ssize_t n;uint64_t total=0;
 while((n=read(fd,bytes,sizeof(bytes)))!=0){if(n<0){if(errno==EINTR)continue;close(fd);throw std::runtime_error("LIBM_READ");}total+=uint64_t(n);vxl_hunet_sha_update(&hash,bytes,uint64_t(n));}
 if(close(fd))throw std::runtime_error("LIBM_CLOSE");
 uint8_t sha[32];vxl_hunet_sha_final(&hash,sha);
 memcpy(out_sha,sha,32);return "{\"compiler\":"+quote(__VERSION__)+",\"algorithm\":\"ERF_NATIVE_V1\",\"threads\":4,\"precision_flags\":[\"-fno-fast-math\",\"-ffp-contract=off\"],\"libm_path\":"+quote(path)+",\"libm_bytes\":"+std::to_string(total)+",\"libm_sha256\":"+quote(hex(sha,32))+"}\n";
}
static void measured_gate(const std::string&path,const vxl_opt_binding_v4&binding,unsigned roi,OptRunResult&r){
 /* Controller-created record: magic8, flags7, roi, bindingSHA, auditSHA,
    diagnostic run16, reserved32. It is authority evidence, not a signature. */
 uint8_t gate[128],sha[32];auto slash=path.find_last_of('/');if(path.empty()||path[0]!='/'||slash==std::string::npos)throw std::runtime_error("GATE_ABSOLUTE_PATH_REQUIRED");
 require(vxl_hunet_read_asset(path.substr(0,slash).c_str(),path.substr(slash+1).c_str(),gate,128),"gate");
 uint32_t flags,gate_roi;memcpy(&flags,gate+8,4);memcpy(&gate_roi,gate+12,4);auto b=binding;b.v3.v2.v1.generation=b.v3.v2.v1.state=0;vxl_hunet_hash(&b,512,sha);
 uint8_t audit=0,run=0,reserved=0;for(unsigned i=0;i<32;i++){audit|=gate[48+i];reserved|=gate[96+i];}for(unsigned i=0;i<16;i++)run|=gate[80+i];
 if(memcmp(gate,"VXFUSG1",8)||flags!=7||gate_roi!=roi||memcmp(gate+16,sha,32)||!audit||!run||reserved)throw std::runtime_error("NUMERIC_AUTHORITY_GATE_MISMATCH");
 r.measured_authorized=true;memcpy(r.numeric_gate_binding.data(),sha,32);
}
#include "hunet_opt_timing.h"
#ifndef VXL_OPT_CANDIDATE
#define VXL_OPT_CANDIDATE "G1"
#endif
int main(int argc,char**argv){
 if(argc==1||(argc==2&&!strcmp(argv[1],"--help"))){puts("NO_DEFAULT_COMPUTE: --release DIR --candidate COMPILED_ID --roi 0 --mode stage0|diagnostic|explore|measured --out NEWDIR [--run-id 32HEX --kind first|warmup|measured|diagnostic --index N --gate ABSOLUTE_FILE --exploration-authorized yes]; measured requires numeric and human authority gate");return 0;}
 std::unique_ptr<ArtifactWriter>writer;
 try{
  auto o=options(argc,argv);for(auto k:{"--release","--candidate","--roi","--out"})if(!o.count(k))throw std::runtime_error("MISSING_OPTION");writer.reset(new ArtifactWriter(o["--out"]));
  for(const auto&p:o)if(p.first!="--release"&&p.first!="--roi"&&p.first!="--mode"&&p.first!="--out"&&p.first!="--run-id"&&p.first!="--kind"&&p.first!="--index"&&p.first!="--gate"&&p.first!="--candidate"&&p.first!="--exploration-authorized")throw std::runtime_error("UNKNOWN_OPTION");
  unsigned roi=number(o["--roi"]),index=o.count("--index")?number(o["--index"]):0;if(roi!=0)throw std::runtime_error("ROI0_ONLY");
  if(o["--candidate"]!=VXL_OPT_CANDIDATE)throw std::runtime_error("COMPILED_CANDIDATE_MISMATCH");
  if(!o.count("--mode"))o["--mode"]="explore";
  OptMode mode;if(o["--mode"]=="stage0")mode=OptMode::STAGE0;else if(o["--mode"]=="diagnostic")mode=OptMode::DIAGNOSTIC;else if(o["--mode"]=="explore")mode=OptMode::EXPLORE;else if(o["--mode"]=="measured")mode=OptMode::MEASURED;else throw std::runtime_error("BAD_MODE");
  if(mode==OptMode::MEASURED&&!o.count("--gate"))throw std::runtime_error("MEASURED_EXTERNAL_NUMERIC_AND_AUTH_GATE_REQUIRED");
  vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.roi_index=roi;b.sample_index=index;auto kind=o.count("--kind")?o["--kind"]:"diagnostic";b.sample_kind=kind=="first"?1:kind=="warmup"?2:kind=="measured"?3:kind=="diagnostic"?4:0;if(!b.sample_kind)throw std::runtime_error("BAD_KIND");
  if(mode!=OptMode::MEASURED&&b.sample_kind!=4)throw std::runtime_error("DIAGNOSTIC_TIMING_NOT_FORMAL_SAMPLE");
  if(mode==OptMode::MEASURED&&b.sample_kind==4)throw std::runtime_error("MEASURED_REQUIRES_FORMAL_SAMPLE_KIND");
  if(o.count("--run-id")){auto id=o["--run-id"];if(id.size()!=32||id.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("BAD_RUN_ID");for(unsigned i=0;i<16;i++)b.run_id[i]=uint8_t(strtoul(id.substr(i*2,2).c_str(),nullptr,16));}
  else{int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0||read(fd,b.run_id,16)!=16)throw std::runtime_error("RUN_ID_RANDOM");close(fd);}
  std::vector<uint8_t>input(196608);require(vxl_hunet_read_asset(o["--release"].c_str(),"base/inputs/000.f32",input.data(),input.size()),"input");b.input_ptr=(uintptr_t)input.data();b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
  std::string boot=text_file("/proc/sys/kernel/random/boot_id"),build=text_file("/etc/codex-build-id");utsname un{};if(uname(&un))throw std::runtime_error("UNAME");
  OptClientGuard client;require(vxl_opt_client_open(&client.c,"/dev/vortex0",vxl_opt_catalog_default(),(vxl_opt_catalog_default()->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE),"open");writer->write("binding-opt.bin",&client.c.binding,512);
  uint8_t runtime_sha[32],expected[65];writer->write("cpu-runtime.json",cpu_runtime(runtime_sha));require(vxl_hunet_read_asset(o["--release"].c_str(),"cpu/libm.sha256",expected,65),"libm expected");if(std::string((char*)expected,64)!=hex(runtime_sha,32)||expected[64]!='\n')throw std::runtime_error("CPU_LIBM_CHANGED");
  OptRunResult r;r.exploration_authorized=o.count("--exploration-authorized")&&o["--exploration-authorized"]=="yes";if(mode==OptMode::EXPLORE&&!r.exploration_authorized)throw std::runtime_error("EXPLORATION_AUTHORITY_REQUIRED");if(mode==OptMode::MEASURED)measured_gate(o["--gate"],client.c.binding,roi,r);
  static_assert(sizeof(vxl_opt_load_timing)==2616&&offsetof(vxl_opt_load_timing,rows)==56,"LOAD_TIMING_V1_LAYOUT");static_assert(sizeof(vxl_digest_audit)==392,"DIGEST_AUDIT_V1_LAYOUT");
  uint64_t start=hunet_now();vxl_opt_load_timing load{};int load_rc=vxl_opt_client_load_timed(&client.c,o["--release"].c_str(),&load);r.load_ns=hunet_duration(start,hunet_now());envelope(*writer,"load-timing.bin",0x4c4f5856,&load,sizeof(load));if(load_rc){envelope(*writer,"load-digest-audit.bin",0x44475856,&client.c.digest_audit,sizeof(client.c.digest_audit));try{driver_snapshots(*writer);}catch(...){writer->write("capture-failure.json","{\"state\":\"FAIL_STOP_CAPTURE_INCOMPLETE\"}\n");}require(load_rc,"load");}
  r.checkpoint=[&](const std::string&n,const void*p,size_t size){writer->write(n,p,size);};OptRunOptions run;run.client=&client.c;run.begin=b;run.mode=mode;int rc=vxl_opt_run(run,&r);envelope(*writer,"user-digest-audit.bin",0x44475856,&client.c.digest_audit,sizeof(client.c.digest_audit));writer->write("timing.json",hunet_opt_timings(r));driver_snapshots(*writer);
  std::string meta="{\"scope\":\"HUNET_OPT_LINUX_V2\",\"state\":"+quote(rc?"FAIL_STOP":r.prefix_stop?"PREFIX_STOP":"RAW_COMPLETE_NUMERIC_UNREVIEWED_REQUIRES_AUDIT")+",\"error\":"+std::to_string(rc)+",\"boot_id\":"+quote(boot)+",\"build_id\":"+quote(build)+",\"kernel\":"+quote(un.release)+",\"profile\":4,\"program_id\":10,\"generation\":"+std::to_string(client.c.binding.v3.v2.v1.generation)+",\"run_id\":"+quote(hex(b.run_id,16))+",\"roi_index\":"+std::to_string(roi)+",\"sample_kind\":"+std::to_string(b.sample_kind)+",\"sample_index\":"+std::to_string(index)+",\"mode\":"+quote(o["--mode"])+",\"logical_completed\":"+std::to_string(r.logical_completed)+",\"gpu_records\":"+std::to_string(r.gpu.size())+",\"cpu_records\":"+std::to_string(r.cpu.size())+",\"gpu_record_bytes\":"+std::to_string(sizeof(OptGpuRecord))+",\"cpu_record_bytes\":"+std::to_string(sizeof(HybridCpuRecord))+",\"stage_count\":"+std::to_string(r.stage_count)+",\"captured_stages\":"+std::to_string(r.captured_stages)+",\"T_load_ns\":"+std::to_string(r.load_ns)+",\"T_resident_ns\":"+std::to_string(r.resident_ns)+",\"T_checkpoint_ns\":"+std::to_string(r.checkpoint_ns)+",\"T_diagnostic_capture_ns\":"+std::to_string(r.diagnostic_capture_ns)+",\"T_final_read_ns\":"+std::to_string(r.final_read_ns)+",\"gpu_seconds\":null,\"files\":"+writer->manifest()+"}\n";
  writer->write("run.json",meta);int close_rc=vxl_opt_client_close(&client.c);if(close_rc){writer->write("close-failure.json","{\"error\":"+std::to_string(close_rc)+"}\n");return 1;}
  puts(rc?"HUNET_OPT_FAIL_STOP_EVIDENCE_RETAINED":r.prefix_stop?"HUNET_OPT_STAGE0_RAW_REQUIRES_AUDIT":"HUNET_OPT_RAW_COMPLETE_NUMERIC_UNREVIEWED_REQUIRES_AUDIT");return rc?1:0;
 }catch(const std::exception&e){if(writer){try{writer->write("startup-failure.json","{\"error\":"+quote(e.what())+"}\n");}catch(...){}}fprintf(stderr,"FAIL_STOP: %s\n",e.what());return 1;}
}

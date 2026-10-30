#include "hunet_tool.h"
#include "hunet_hybrid_session.h"
#include "hunet_timing.h"
#include <memory>
#include <cmath>
#include <dlfcn.h>
#include <limits.h>
struct HybridClientGuard {vxl_hybrid_client c{};HybridClientGuard(){c.gpu.fd=-1;}~HybridClientGuard(){vxl_hybrid_client_close(&c);}};
static std::string cpu_runtime(){
 Dl_info info{};char path[PATH_MAX];if(!dladdr(reinterpret_cast<void *>(::erff),&info)||!info.dli_fname||!realpath(info.dli_fname,path))throw std::runtime_error("LIBM_IDENTITY");
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)throw std::runtime_error("LIBM_OPEN");vxl_hunet_sha hash{};vxl_hunet_sha_init(&hash);uint8_t bytes[4096];ssize_t n;uint64_t total=0;
 while((n=read(fd,bytes,sizeof(bytes)))!=0){if(n<0){if(errno==EINTR)continue;close(fd);throw std::runtime_error("LIBM_READ");}total+=uint64_t(n);vxl_hunet_sha_update(&hash,bytes,uint64_t(n));}
 if(close(fd))throw std::runtime_error("LIBM_CLOSE");
 uint8_t sha[32];vxl_hunet_sha_final(&hash,sha);
 return "{\"compiler\":"+quote(__VERSION__)+",\"algorithm\":\"ERF_NATIVE_V1\",\"threads\":4,\"precision_flags\":[\"-fno-fast-math\",\"-ffp-contract=off\"],\"libm_path\":"+quote(path)+",\"libm_bytes\":"+std::to_string(total)+",\"libm_sha256\":"+quote(hex(sha,32))+"}\n";
}
static std::string timings(const HybridRaw &r){
 std::string s="{\"gpu\":[";bool first=true;
 for(const auto &g:r.gpu){if(!first)s+=",";first=false;s+="{\"sequence\":"+std::to_string(g.job.sequence)+",\"T_submit_wait_ns\":"+std::to_string(g.submit_wait_ns)+",\"T_evidence_ns\":"+std::to_string(g.evidence_ns)+",\"T_reap_ns\":"+std::to_string(g.reap_ns)+",\"T_capture_ns\":"+std::to_string(g.capture_ns)+"}";}
 s+="],\"cpu\":[";first=true;
 for(const auto &c:r.cpu){if(!first)s+=",";first=false;s+="{\"sequence\":"+std::to_string(c.begin.sequence)+",\"stage\":"+std::to_string(c.begin.stage)+",\"T_begin_call_ns\":"+std::to_string(c.begin_call_ns)+",\"T_compute_ns\":"+std::to_string(c.timing.duration_ns)+",\"T_commit_call_ns\":"+std::to_string(c.commit_call_ns)+",\"T_evidence_call_ns\":"+std::to_string(c.evidence_call_ns)+",\"T_writeback_capture_ns\":"+std::to_string(c.writeback_capture_ns)+",\"T_cpu_reference_ns\":"+std::to_string(c.reference_ns)+"}";}return s+"]}";
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--help")){puts("NO_DEFAULT_COMPUTE: --release DIR --roi N --run-id 32HEX --kind first|warmup|measured|diagnostic --index N --mode first-gelu|diagnostic|formal --out NEWDIR; formal requires an externally approved numeric gate");return 0;}
 std::unique_ptr<ArtifactWriter> writer;
 try{
  auto o=options(argc,argv);for(auto k:{"--release","--roi","--run-id","--kind","--index","--mode","--out"})if(!o.count(k))throw std::runtime_error("MISSING_OPTION");if(o.size()!=7)throw std::runtime_error("UNKNOWN_OPTION");
  unsigned roi=number(o["--roi"]),index=number(o["--index"]);HybridMode mode=o["--mode"]=="first-gelu"?HybridMode::FIRST_GELU:o["--mode"]=="diagnostic"?HybridMode::DIAGNOSTIC:HybridMode::FORMAL;
  if((mode==HybridMode::FORMAL&&o["--mode"]!="formal")||roi>=127||o["--run-id"].size()!=32)throw std::runtime_error("BAD_CONTRACT");
  bool formal_authorized=false;
  /* Task10 controller validates signed-off numeric/run/release gates first;
     this environment bit is explicit dispatch authority, never a default. */
  if(mode==HybridMode::FORMAL){const char *gate=getenv("VXL_HYBRID_FORMAL_AUTHORIZED");formal_authorized=gate&&!strcmp(gate,"1");if(!formal_authorized)throw std::runtime_error("FORMAL_EXTERNAL_NUMERIC_AND_AUTH_GATE_REQUIRED");}
  vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.roi_index=roi;b.sample_index=index;auto kind=o["--kind"];b.sample_kind=kind=="first"?1:kind=="warmup"?2:kind=="measured"?3:kind=="diagnostic"?4:0;if(!b.sample_kind)throw std::runtime_error("BAD_KIND");
  for(unsigned i=0;i<16;i++){auto p=o["--run-id"].substr(i*2,2);if(p.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("BAD_RUN_ID");b.run_id[i]=uint8_t(strtoul(p.c_str(),nullptr,16));}
  char asset[80];snprintf(asset,sizeof(asset),"base/inputs/%03u.f32",roi);std::vector<uint8_t> input(196608);require(vxl_hunet_read_asset(o["--release"].c_str(),asset,input.data(),input.size()),"input");b.input_ptr=reinterpret_cast<uintptr_t>(input.data());b.input_bytes=input.size();vxl_hunet_hash(input.data(),input.size(),b.input_sha256);
  std::string boot=text_file("/proc/sys/kernel/random/boot_id"),build=text_file("/etc/codex-build-id");utsname un{};if(uname(&un))throw std::runtime_error("UNAME");writer.reset(new ArtifactWriter(o["--out"]));
  HybridClientGuard client;require(vxl_hybrid_client_open(&client.c,"/dev/vortex0"),"open");writer->write("binding-hybrid.bin",&client.c.binding,384);writer->write("cpu-runtime.json",cpu_runtime());uint64_t load_start=hunet_now();require(vxl_hybrid_client_load(&client.c,o["--release"].c_str()),"load");uint64_t load_ns=hunet_duration(load_start,hunet_now());
  HybridRaw r;r.formal_authorized=formal_authorized;r.checkpoint=[&](const std::string &name,const void *p,size_t n){writer->write(name,p,n);};int rc=hunet_hybrid_execute(&client.c,b,mode,&r);
  writer->write("logical_args.bin",r.logical.data(),r.logical.size()*128);writer->write("timing.json",timings(r));
  std::string meta="{\"scope\":\"HUNET_HYBRID_LINUX_V1\",\"transport\":\"SCP_CAPTURE\",\"state\":"+quote(rc?"FAIL_STOP":r.prefix_stop?"PREFIX_STOP":"RAW_COMPLETE_REQUIRES_AUDIT")+",\"error\":"+std::to_string(rc)+",\"boot_id\":"+quote(boot)+",\"build_id\":"+quote(build)+",\"kernel\":"+quote(un.release)+",\"profile\":4,\"program_id\":8,\"generation\":"+std::to_string(client.c.binding.v1.generation)+",\"run_id\":"+quote(hex(b.run_id,16))+",\"roi_index\":"+std::to_string(roi)+",\"sample_kind\":"+std::to_string(b.sample_kind)+",\"sample_index\":"+std::to_string(index)+",\"mode\":"+quote(o["--mode"])+",\"logical_completed\":"+std::to_string(r.logical_completed)+",\"gpu_records\":"+std::to_string(r.gpu.size())+",\"cpu_records\":"+std::to_string(r.cpu.size())+",\"T_load_ns\":"+std::to_string(load_ns)+",\"T_resident_ns\":"+std::to_string(r.resident_ns)+",\"T_checkpoint_ns\":"+std::to_string(r.checkpoint_ns)+",\"T_diagnostic_capture_ns\":"+std::to_string(r.diagnostic_capture_ns)+",\"T_transfer_ns\":null,\"gpu_seconds\":null,\"files\":"+writer->manifest()+"}\n";
  writer->write("run.json",meta);int close_rc=vxl_hybrid_client_close(&client.c);if(close_rc){writer->write("close-failure.json","{\"error\":"+std::to_string(close_rc)+"}\n");return 1;}
  puts(rc?"HUNET_HYBRID_FAIL_STOP_EVIDENCE_RETAINED":r.prefix_stop?"HUNET_HYBRID_PREFIX_STOP_REQUIRES_EXTERNAL_AUDIT":"HUNET_HYBRID_RAW_COMPLETE_REQUIRES_EXTERNAL_AUDIT");return rc?1:0;
 }catch(const std::exception &e){if(writer){try{writer->write("startup-failure.json","{\"error\":"+quote(e.what())+"}\n");}catch(...){}}fprintf(stderr,"FAIL_STOP: %s\n",e.what());return 1;}
}

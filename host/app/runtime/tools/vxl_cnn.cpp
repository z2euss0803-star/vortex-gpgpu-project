/* Diagnostic real-driver entry only. No CPU/mock selection exists. */
#include "cnn_session.h"
#include "cnn_identity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>
#include <sys/utsname.h>
#include <unistd.h>
#ifndef VXL_CNN_SOURCE_COMMIT
#define VXL_CNN_SOURCE_COMMIT "UNSEALED"
#endif
static std::string read(const char *path){std::ifstream f(path);std::ostringstream o;if(f)o<<f.rdbuf();return o.str();}
static std::string trim(std::string s){while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'))s.pop_back();return s;}
int main(int argc,char **argv){
 const bool check_only=argc==5&&!strcmp(argv[1],"--check-identity");
 if(!check_only&&(argc!=3||strcmp(argv[1],"--evidence-dir")||!argv[2][0])){
  std::fputs("usage: vxl_cnn --evidence-dir <new-directory> | --check-identity <commit> <build> <boot>\n",stderr);return 2;
 }
 for(const char *name:{"LD_PRELOAD","LD_AUDIT","VXL_TRANSPORT","VORTEX_DRIVER"}){
  const char *value=getenv(name);if(value&&*value){std::fprintf(stderr,"Rejected environment: %s\n",name);return 2;}
 }
 CnnRun run;run.source_commit=VXL_CNN_SOURCE_COMMIT;
 run.build_id=trim(read("/etc/codex-build-id"));run.boot_id=trim(read("/proc/sys/kernel/random/boot_id"));
 run.manifest_json=read("/etc/codex-cnn-manifest.json");
 if(check_only&&(run.source_commit!=argv[2]||run.build_id!=argv[3]||run.boot_id!=argv[4])){
  std::fputs("Expected candidate identity rejected; no device opened.\n",stderr);return 1;
 }
 utsname u{};if(uname(&u)){std::perror("uname");return 1;}run.kernel=std::string(u.release)+" "+u.version+" "+u.machine;
 std::string identity_reason;
 if(!cnn_identity_check(run.manifest_json,run.source_commit,run.build_id,run.boot_id,u.machine,u.release,&identity_reason)){
  std::fprintf(stderr,"Candidate identity rejected (%s); no device opened.\n",identity_reason.c_str());return 1;
 }
 if(check_only){std::puts("CNN_INSTALLED_IDENTITY_MATCH_ONLY_NO_DEVICE_OPENED");return 0;}
 std::error_code error;
 if(!std::filesystem::create_directory(argv[2],error)){std::fprintf(stderr,"Evidence directory must be new and writable: %s\n",error.message().c_str());return 1;}
 std::time_t wall=std::time(nullptr);std::tm utc{};gmtime_r(&wall,&utc);char stamp[32];strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%SZ",&utc);run.utc_start=stamp;
 for(const char *path:{"/proc/uptime","/proc/cpuinfo","/proc/self/status","/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor","/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"}){
  run.environment+=std::string("FILE ")+path+"\n"+read(path)+"\n";
 }
 run.environment+="CLOCK=CLOCK_MONOTONIC\nDEVICE_CYCLES=UNSUPPORTED\nPID="+std::to_string(getpid())+"\n";
 CnnSession session;int result=cnn_run_suite(session,cnn_fixed_weights(),&run);
 int saved=cnn_save_run(argv[2],&run);
 // Keep the path in stderr; arbitrary directory bytes must not corrupt JSON stdout.
 std::fprintf(stderr,"Evidence directory: %s\n",argv[2]);
 std::printf("{\"scope\":\"REAL_DRIVER_CNN_I32_V1\",\"iterations\":%zu,\"result\":%d,\"save_result\":%d}\n",run.iterations.size(),result,saved);
 return result||saved?1:0;
}

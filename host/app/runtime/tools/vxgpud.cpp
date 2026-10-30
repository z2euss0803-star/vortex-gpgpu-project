#include <vxunified/startup.hpp>
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace vxunified;
namespace {
volatile std::sig_atomic_t stop_requested=0;
void stop(int){stop_requested=1;}
void report(const char*stage,const vxnn::Error&e){std::fprintf(stderr,"vxgpud: stage=%s code=%d category=%s detail=%s\n",stage,e.code,e.category.c_str(),e.detail.c_str());}
std::string hex(const Digest&d){const char*h="0123456789abcdef";std::string s;for(auto b:d){s+=h[b>>4];s+=h[b&15];}return s;}
Result<std::string>private_config(const char*path){
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return Result<std::string>::failure({errno,"STARTUP_CONFIG","open private configuration"});
 struct Close{int fd;~Close(){close(fd);}}guard{fd};struct stat st{};
 if(fstat(fd,&st)<0)return Result<std::string>::failure({errno,"STARTUP_CONFIG","stat configuration"});
 if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||((st.st_mode&07777)!=0600&&(st.st_mode&07777)!=0400)||st.st_size<=0||st.st_size>65536)return Result<std::string>::failure({EACCES,"STARTUP_CONFIG","same-uid regular config0600/0400, one link and <=64KiB required"});
 std::string raw(size_t(st.st_size),'\0');size_t done=0;while(done<raw.size()){ssize_t n=read(fd,&raw[done],raw.size()-done);if(n<0&&errno==EINTR)continue;if(n<=0)return Result<std::string>::failure({n<0?errno:EIO,"STARTUP_CONFIG","read exact configuration bytes"});done+=size_t(n);}char extra;ssize_t n=read(fd,&extra,1);if(n!=0)return Result<std::string>::failure({ESTALE,"STARTUP_CONFIG","configuration changed during read"});return Result<std::string>::success(std::move(raw));
}
bool directory(const std::string&p){return mkdir(p.c_str(),0700)==0||errno==EEXIST;}
}
int main(int argc,char**argv){
 if(argc==2&&std::string(argv[1])=="--help"){std::puts("vxgpud --check-config /absolute/config.json | --serve /absolute/config.json\nNo automatic start, mode fallback, fixture, raw task, PL or reset option.");return 0;}
 if(argc!=3||(std::string(argv[1])!="--check-config"&&std::string(argv[1])!="--serve")){std::fprintf(stderr,"usage: vxgpud --check-config CONFIG | --serve CONFIG\n");return 2;}
 try{
  auto bytes=private_config(argv[2]);if(!bytes){report("config",*bytes.error);return 1;}
  std::ifstream bootfile("/proc/sys/kernel/random/boot_id");std::string boot;if(!std::getline(bootfile,boot)){report("boot",{EIO,"STARTUP_CONFIG","kernel boot_id unavailable"});return 1;}
  auto parsed=decode_daemon_config(*bytes.value,boot);if(!parsed){report("config",*parsed.error);return 1;}const auto&c=*parsed.value;
  if(std::string(argv[1])=="--check-config"){
   // Deliberately no directories, device opens, probe, worker or GPU work.
   std::printf("{\"scope\":\"OFFLINE_CONFIG_VALIDATION\",\"gpu_raw_cycles\":null,\"device_opened\":false,\"backend_mode\":\"%s\",\"config_sha256\":\"%s\"}\n",c.service.mode==BackendMode::COMPUTE_V2?"compute-v2":"legacy-compat",hex(c.config_sha).c_str());return 0;
  }
  umask(0077);if(!directory(c.service.private_dir)||!directory(c.evidence_dir)){report("directories",{errno,"STARTUP_CONFIG","create private service/evidence directories"});return 1;}
  struct sigaction action{};action.sa_handler=stop;sigemptyset(&action.sa_mask);if(sigaction(SIGINT,&action,nullptr)||sigaction(SIGTERM,&action,nullptr)){report("signals",{errno,"STARTUP_CONFIG","install bounded shutdown signal handlers"});return 1;}
  auto started=Daemon::start(c,installed_applications());if(!started){report("startup",*started.error);return 1;}auto daemon=*started.value;
  std::puts("{\"event\":\"service_ready\",\"scope\":\"REAL_BOARD_STARTUP_ONLY\",\"compute_validation\":\"NOT_RUN\",\"gpu_raw_cycles\":null}");std::fflush(stdout);bool reported=false;
  while(!stop_requested){auto peer=daemon->serve_one(500);if(!peer&&peer.error->code!=ETIMEDOUT&&peer.error->code!=EINTR)report("rpc_peer",*peer.error);if(!reported){auto error=daemon->first_error();if(error){report("execution_first_error",*error.value);reported=true;}}}
  auto stopped=daemon->shutdown();if(!stopped){report("shutdown_retained",*stopped.error);return 1;}return 0;
 }catch(const std::exception&e){report("exception",{EIO,"DAEMON",e.what()});return 1;}catch(...){report("exception",{EIO,"DAEMON","non-standard exception"});return 1;}
}

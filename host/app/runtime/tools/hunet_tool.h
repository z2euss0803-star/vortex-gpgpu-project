#pragma once
#include <vxl/hunet_client.h>
#include <vxl/hunet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
inline std::string hex(const void *p,size_t n){const auto*b=static_cast<const uint8_t*>(p);std::string s;for(size_t i=0;i<n;i++){s+="0123456789abcdef"[b[i]>>4];s+="0123456789abcdef"[b[i]&15];}return s;}
inline std::string quote(const std::string &s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){char b[8];snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=char(c);}return out+'"';}
inline unsigned number(const std::string &s){char *end;errno=0;unsigned long n=strtoul(s.c_str(),&end,10);if(errno||s.empty()||*end||s[0]=='-'||n>0xffffffffUL)throw std::runtime_error("BAD_NUMBER");return unsigned(n);}
inline std::map<std::string,std::string> options(int argc,char **argv){std::map<std::string,std::string> m;for(int i=1;i<argc;i+=2){if(i+1>=argc||m.count(argv[i]))throw std::runtime_error("BAD_ARGS");m[argv[i]]=argv[i+1];}return m;}
inline void require(int rc,const char *what){if(rc)throw std::runtime_error(std::string(what)+":"+std::to_string(rc));}
inline std::string text_file(const char *p){std::ifstream f(p);std::string s;if(!f||!std::getline(f,s)||s.empty())throw std::runtime_error(std::string("IDENTITY_MISSING:")+p);return s;}
struct ToolClient {vxl_hunet_client c{};ToolClient(){c.fd=-1;}~ToolClient(){vxl_hunet_client_close(&c);}};
struct ArtifactWriter {
 std::string root;std::map<std::string,std::pair<uint64_t,std::string>> files;
 explicit ArtifactWriter(std::string r):root(std::move(r)){if(root.empty()||mkdir(root.c_str(),0700))throw std::runtime_error("OUT_NOT_FRESH");}
 void write(const std::string &name,const void *p,size_t n){
  if(name.find("..")!=std::string::npos||name.empty()||name[0]=='/')throw std::runtime_error("ARTIFACT_PATH");
  auto slash=name.find('/');if(slash!=std::string::npos){std::string sub=root+'/'+name.substr(0,slash);if(mkdir(sub.c_str(),0700)&&errno!=EEXIST)throw std::runtime_error("OUT_SUBDIR");}
  int fd=open((root+'/'+name).c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("ARTIFACT_EXISTS");size_t off=0;while(off<n){ssize_t k=::write(fd,static_cast<const uint8_t*>(p)+off,n-off);if(k<0&&errno==EINTR)continue;if(k<=0){close(fd);throw std::runtime_error("ARTIFACT_WRITE");}off+=size_t(k);}if(fsync(fd)||close(fd))throw std::runtime_error("ARTIFACT_SYNC");uint8_t sha[32];vxl_hunet_hash(p,n,sha);files[name]={n,hex(sha,32)};
 }
 void write(const std::string &name,const std::string &s){write(name,s.data(),s.size());}
 std::string manifest(){std::string s="{";bool first=true;for(auto &f:files){if(!first)s+=",";first=false;s+=quote(f.first)+":{\"bytes\":"+std::to_string(f.second.first)+",\"sha256\":"+quote(f.second.second)+"}";}return s+"}";}
};

#pragma once
#include <vxunified/types.hpp>
#include <array>
#include <functional>
#include <string>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {
// Diagnostic readback of four already-owned accumulator regions. This does
// not submit work, write device memory, ACK, release, reset, or assert utilization.
inline Result<void>mosaic_core_probe_capture(const std::string&directory,uint64_t index,bool after,BufferRef work,uint32_t kpad,uint32_t core_bytes,const std::function<Result<void>(BufferRef,vxnn::MutableBytes)>&read){
 auto fail=[](int e,const char*s){return Result<void>::failure({e,"MOSAIC_CORE_PROBE",s});};
 if(directory.empty()||directory[0]!='/'||directory.size()>4096||directory.find('\0')!=std::string::npos||!read||!work.handle||!work.object_generation||!work.device_generation||!(work.access&1)||!kpad||kpad>4096||kpad%32||uint64_t(kpad)*32+512>core_bytes||uint64_t(64)+4ULL*core_bytes>work.bytes||work.offset>UINT64_MAX-work.bytes)return fail(EINVAL,"bounded owned four-core accumulator layout required");
 struct Fd{int n;explicit Fd(int v):n(v){}~Fd(){if(n>=0)close(n);}Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;};
 Fd dir(open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));struct stat st{};
 if(dir.n<0)return fail(errno,"open trusted diagnostic directory");
 if(fstat(dir.n,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0700)return fail(EACCES,"same-owner0700 diagnostic directory required");
 auto name="core-accum-index"+std::to_string(index)+(after?"-after.bin":"-before.bin");
 Fd file(openat(dir.n,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0400));
 if(file.n<0)return fail(errno,"new immutable snapshot required; existing evidence retained");
 std::array<uint8_t,2048>bytes{};
 for(uint32_t core=0;core<4;core++){
  BufferRef ref=work;ref.offset+=64+uint64_t(core)*core_bytes+uint64_t(kpad)*32;ref.bytes=512;ref.access=1;
  auto r=read(ref,{bytes.data()+core*512,512});if(!r)return r;
 }
 size_t done=0;
 while(done<bytes.size()){
  auto n=write(file.n,bytes.data()+done,bytes.size()-done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return fail(n<0?errno:EIO,"diagnostic output write");
  done+=size_t(n);
 }
 if(fsync(file.n))return fail(errno,"diagnostic output fsync");
 return Result<void>::success();
}
}

#pragma once
#include <vxunified/texture.hpp>
#include "nnrt/package/json.hpp"
#include <vxl/hunet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {namespace result_io {
namespace io_json=vxnn::json;
struct Fd {int value=-1;explicit Fd(int x):value(x){}~Fd(){if(value>=0)close(value);}Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;};
inline Digest hash(const std::vector<uint8_t>&b){Digest d{};vxl_hunet_hash(b.data(),b.size(),d.data());return d;}
inline std::vector<std::string>parts(const std::string&name){
 io_json::check(!name.empty()&&name.size()<=4096&&name.find('\\')==std::string::npos&&name.find(':')==std::string::npos&&name.find('\0')==std::string::npos,"unsafe asset name");std::vector<std::string>out;size_t begin=0;
 while(begin<=name.size()){auto end=name.find('/',begin);auto part=name.substr(begin,end==std::string::npos?end:end-begin);io_json::check(!part.empty()&&part!="."&&part!="..","unsafe asset path component");out.push_back(part);if(end==std::string::npos)break;begin=end+1;}return out;
}
inline int child(int directory,const std::string&name,bool last_directory=false){auto path=parts(name);Fd current(dup(directory));io_json::check(current.value>=0,"root descriptor duplicate");for(size_t i=0;i<path.size();i++){Fd next(openat(current.value,path[i].c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|(i+1<path.size()||last_directory?O_DIRECTORY:0)));io_json::check(next.value>=0,"asset descriptor open");close(current.value);current.value=next.value;next.value=-1;}int result=current.value;current.value=-1;return result;}
inline std::vector<uint8_t>read(int fd,uint64_t maximum,uint64_t exact=0){
 struct stat before{},after{};io_json::check(fd>=0&&!fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_size>0&&uint64_t(before.st_size)<=maximum&&(!exact||uint64_t(before.st_size)==exact),"bounded regular file required");std::vector<uint8_t>b(size_t(before.st_size));size_t off=0;
 while(off<b.size()){ssize_t n=::read(fd,b.data()+off,b.size()-off);if(n<0&&errno==EINTR)continue;io_json::check(n>0,"asset read incomplete");off+=size_t(n);}uint8_t extra;io_json::check(::read(fd,&extra,1)==0&&!fstat(fd,&after)&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec,"asset changed during read");return b;
}
Result<MosaicResultLease>import_at(int directory,const std::string&leaf);
}}

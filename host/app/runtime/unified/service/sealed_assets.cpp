#include <vxunified/sealed_assets.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {
namespace {
struct Fd {int n=-1;explicit Fd(int f=-1):n(f){}~Fd(){if(n>=0)close(n);}Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;void reset(int f){if(n>=0)close(n);n=f;}};
bool path_ok(const std::string&p){if(p.size()<2||p.size()>4096||p[0]!='/'||p.back()=='/'||p.find('\0')!=std::string::npos) { return false; }size_t i=1;while(i<p.size()){auto j=p.find('/',i);if(j==std::string::npos)j=p.size();auto part=p.substr(i,j-i);if(part.empty()||part=="."||part=="..") { return false; }i=j+1;}return true;}
}
Result<std::string>read_sealed_native_asset(const std::string&path,size_t max){
 using R=Result<std::string>;if(!path_ok(path)) { return R::failure({EINVAL,"INSTALLATION_FILE","normalized absolute installed path required"}); }
 Fd dir(open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));if(dir.n<0) { return R::failure({errno,"INSTALLATION_FILE","open root directory"}); }size_t i=1;
 while(true){auto j=path.find('/',i);if(j==std::string::npos)break;auto name=path.substr(i,j-i);int fd=openat(dir.n,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0) { return R::failure({errno,"INSTALLATION_FILE","open installed parent without symlink"}); }dir.reset(fd);struct stat s{};if(fstat(fd,&s)<0) { return R::failure({errno,"INSTALLATION_FILE","stat installed parent"}); }if((s.st_uid!=0&&s.st_uid!=geteuid())||((s.st_mode&0022)&&!(s.st_uid==0&&(s.st_mode&S_ISVTX)))) { return R::failure({EACCES,"INSTALLATION_FILE","installed parent ownership or write permissions"}); }i=j+1;}
 Fd file(openat(dir.n,path.substr(i).c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));if(file.n<0) { return R::failure({errno,"INSTALLATION_FILE","open installed immutable file"}); }struct stat st{};if(fstat(file.n,&st)<0) { return R::failure({errno,"INSTALLATION_FILE","stat installed file"}); }
 if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||((st.st_mode&07777)!=0400&&(st.st_mode&07777)!=0444)||st.st_size<=0||uint64_t(st.st_size)>max) { return R::failure({EACCES,"INSTALLATION_FILE","same-uid immutable regular0400/0444, one link and bounded length required"}); }
 std::string raw(size_t(st.st_size),'\0');size_t done=0;while(done<raw.size()){auto n=read(file.n,&raw[done],raw.size()-done);if(n<0&&errno==EINTR)continue;if(n<=0) { return R::failure({n<0?errno:EIO,"INSTALLATION_FILE","read exact installed bytes"}); }done+=size_t(n);}char extra;auto n=read(file.n,&extra,1);struct stat after{};if(n!=0||fstat(file.n,&after)<0||after.st_size!=st.st_size||after.st_mtim.tv_sec!=st.st_mtim.tv_sec||after.st_mtim.tv_nsec!=st.st_mtim.tv_nsec) { return R::failure({ESTALE,"INSTALLATION_FILE","installed file changed while reading"}); }return R::success(std::move(raw));
}
}

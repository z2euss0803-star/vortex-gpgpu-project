#include <vxunified/service.hpp>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {
namespace {
struct LocalFd {int value=-1;~LocalFd(){if(value>=0)close(value);}};
Result<int> native_open(const std::string&path){
 int fd=open(path.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return Result<int>::failure({errno,"DEVICE","open owner device"});
 struct stat st{};if(fstat(fd,&st)<0){int e=errno;close(fd);return Result<int>::failure({e,"DEVICE","stat owner device"});}
 if(!S_ISCHR(st.st_mode)){close(fd);return Result<int>::failure({ENODEV,"DEVICE","owner device is not a character device"});}
 return Result<int>::success(fd);
}
}
struct DeviceOwner::State {
 int lock_fd=-1,device_fd=-1;BackendMode mode=BackendMode::COMPUTE_V2;DeviceClose close_device;
 ~State(){if(device_fd>=0){try{close_device(device_fd);}catch(...){/* never retry an ambiguous close */}}if(lock_fd>=0)close(lock_fd);}
};
DeviceOwner::DeviceOwner(std::unique_ptr<State>s):state_(std::move(s)){}
DeviceOwner::~DeviceOwner()=default;
Result<std::shared_ptr<DeviceOwner>> DeviceOwner::acquire(const ServiceConfig&cfg,DeviceOpen opener,DeviceClose closer){
 try{
  if(cfg.private_dir.empty()||cfg.private_dir[0]!='/'||cfg.private_dir.find('\0')!=std::string::npos||cfg.device_path.empty()||cfg.device_path[0]!='/'||cfg.device_path.find('\0')!=std::string::npos||(cfg.mode!=BackendMode::COMPUTE_V2&&cfg.mode!=BackendMode::LEGACY_COMPAT))return Result<std::shared_ptr<DeviceOwner>>::failure({EINVAL,"SERVICE","explicit startup paths/mode required"});
  LocalFd dir;dir.value=open(cfg.private_dir.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir.value<0)return Result<std::shared_ptr<DeviceOwner>>::failure({errno,"SERVICE","open private owner directory"});
  struct stat st{};if(fstat(dir.value,&st)<0)return Result<std::shared_ptr<DeviceOwner>>::failure({errno,"SERVICE","stat private owner directory"});
  if(st.st_uid!=geteuid()||(st.st_mode&07777)!=0700)return Result<std::shared_ptr<DeviceOwner>>::failure({EACCES,"SERVICE","owner directory must be same uid and mode0700"});
  auto s=std::make_unique<State>();s->mode=cfg.mode;s->close_device=closer?std::move(closer):DeviceClose([](int fd){close(fd);});
  s->lock_fd=openat(dir.value,"owner.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);if(s->lock_fd<0)return Result<std::shared_ptr<DeviceOwner>>::failure({errno,"SERVICE","open owner lock"});
  if(fstat(s->lock_fd,&st)<0)return Result<std::shared_ptr<DeviceOwner>>::failure({errno,"SERVICE","stat owner lock"});
  if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0600||st.st_nlink!=1)return Result<std::shared_ptr<DeviceOwner>>::failure({EACCES,"SERVICE","unsafe owner lock"});
  if(flock(s->lock_fd,LOCK_EX|LOCK_NB)<0){int e=errno;return Result<std::shared_ptr<DeviceOwner>>::failure({e==EAGAIN||e==EWOULDBLOCK?EBUSY:e,"SERVICE","device owner already held"});}
  auto fd=opener?opener(cfg.device_path):native_open(cfg.device_path);if(!fd)return Result<std::shared_ptr<DeviceOwner>>::failure(*fd.error);if(*fd.value<0)return Result<std::shared_ptr<DeviceOwner>>::failure({EBADF,"DEVICE","invalid device fd"});s->device_fd=*fd.value;
  int flags=fcntl(s->device_fd,F_GETFD);if(flags<0||fcntl(s->device_fd,F_SETFD,flags|FD_CLOEXEC)<0)return Result<std::shared_ptr<DeviceOwner>>::failure({errno,"DEVICE","set owner fd CLOEXEC"});
  return Result<std::shared_ptr<DeviceOwner>>::success(std::shared_ptr<DeviceOwner>(new DeviceOwner(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<DeviceOwner>>::failure({ENOMEM,"SERVICE","owner allocation"});}catch(...){return Result<std::shared_ptr<DeviceOwner>>::failure({EIO,"SERVICE","owner startup exception"});}
}
int DeviceOwner::fd()const{return state_->device_fd;}
BackendMode DeviceOwner::mode()const{return state_->mode;}
}

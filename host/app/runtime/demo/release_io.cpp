#include "release_io.h"
#include <vxl/hunet.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
struct Fd {int value;explicit Fd(int v):value(v){}~Fd(){if(value>=0)close(value);}int take(){int v=value;value=-1;return v;}};
bool path_ok(const char *path,bool absolute){
 if(!path||!*path||strlen(path)>4096||(*path=='/')!=absolute)return false;
 const char *p=path+(absolute?1:0);if(!*p)return false;
 for(const char *s=p;;++p){
  unsigned char c=static_cast<unsigned char>(*p);
  if(c=='/'||!c){size_t n=size_t(p-s);if(!n||n>255||(n==1&&*s=='.')||(n==2&&s[0]=='.'&&s[1]=='.'))return false;if(!c)return true;s=p+1;}
  else if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
 }
}
int directory(int fd,uid_t uid,bool ancestor){struct stat s{};if(fstat(fd,&s))return -errno;return S_ISDIR(s.st_mode)&&!(s.st_mode&0022)&&(s.st_uid==uid||(ancestor&&s.st_uid==0))?0:-EPERM;}
bool same(const struct stat &a,const struct stat &b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
}
int demo_release_open_root(const char *path,uid_t uid){
 if(!path_ok(path,true))return -EINVAL;
 Fd current(open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));if(current.value<0)return -errno;int rc=directory(current.value,uid,true);if(rc)return rc;
 try{
  std::string name(path+1);size_t offset=0;
  while(offset<name.size()){
   size_t end=name.find('/',offset);bool final=end==std::string::npos;if(final)end=name.size();std::string part=name.substr(offset,end-offset);
   int next=openat(current.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(next<0)return -errno;
   close(current.value);current.value=next;rc=directory(next,uid,!final);if(rc)return rc;offset=end+1;
  }
 }catch(...){return -ENOMEM;}
 return current.take();
}
int demo_release_read(int root,const char *path,size_t expected,const uint8_t *sha,uid_t uid,std::vector<uint8_t> &result){
 result.clear();if(root<0||!path_ok(path,false)||!expected||expected>64u*1024u*1024u)return -EINVAL;
 int rc=directory(root,uid,false);if(rc)return rc;
 Fd current(fcntl(root,F_DUPFD_CLOEXEC,0));if(current.value<0)return -errno;
 try{
  std::string name(path);size_t offset=0;
  while(true){
   size_t end=name.find('/',offset);if(end==std::string::npos)break;std::string part=name.substr(offset,end-offset);
   int next=openat(current.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(next<0)return -errno;close(current.value);current.value=next;
   rc=directory(next,uid,false);if(rc)return rc;offset=end+1;
  }
  Fd file(openat(current.value,name.c_str()+offset,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));if(file.value<0)return -errno;
  struct stat before{},after{};if(fstat(file.value,&before))return -errno;
  if(!S_ISREG(before.st_mode)||before.st_uid!=uid||(before.st_mode&0022)||before.st_nlink!=1)return -EPERM;
  if(before.st_size<0||uint64_t(before.st_size)!=expected)return -ESTALE;
  std::vector<uint8_t> bytes(expected);size_t at=0;
  while(at<expected){ssize_t n=read(file.value,bytes.data()+at,expected-at);if(n<0&&errno==EINTR)continue;if(n<=0)return n<0?-errno:-EIO;at+=size_t(n);}
  uint8_t tail;ssize_t n;do{n=read(file.value,&tail,1);}while(n<0&&errno==EINTR);if(n)return n<0?-errno:-ESTALE;
  if(fstat(file.value,&after))return -errno;
  if(!same(before,after))return -ESTALE;
  if(sha){uint8_t actual[32];vxl_hunet_hash(bytes.data(),bytes.size(),actual);if(memcmp(actual,sha,32))return -ESTALE;}
  result.swap(bytes);return 0;
 }catch(...){return -ENOMEM;}
}

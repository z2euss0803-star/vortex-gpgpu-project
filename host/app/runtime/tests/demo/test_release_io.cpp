#include "../../demo/release_io.h"
#include <vxl/hunet.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>
int main(int argc,char **argv){
 assert(argc==2);std::string root=argv[1];assert(mkdir(root.c_str(),0700)==0);
 std::string file=root+"/data.bin";int fd=open(file.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0&&write(fd,"abc",3)==3);close(fd);
 uint8_t sha[32];vxl_hunet_hash("abc",3,sha);std::vector<uint8_t> bytes;
 int directory=demo_release_open_root(root.c_str(),geteuid());assert(directory>=0);
 assert(!demo_release_read(directory,"data.bin",3,sha,geteuid(),bytes)&&bytes.size()==3&&!memcmp(bytes.data(),"abc",3));
 auto expect=[&](const char *p,size_t n,const uint8_t *s){bytes.assign(1,99);assert(demo_release_read(directory,p,n,s,geteuid(),bytes)<0&&bytes.empty());};
 expect("../data.bin",3,sha);expect("/data.bin",3,sha);expect("./data.bin",3,sha);expect("data.bin/",3,sha);expect("a//b",3,sha);
 expect("data.bin",2,sha);expect("data.bin",4,sha);expect("data.bin",0,sha);expect("data.bin",SIZE_MAX,sha);
 sha[0]^=1;expect("data.bin",3,sha);sha[0]^=1;
 assert(!symlink("data.bin",(root+"/link").c_str()));expect("link",3,sha);
 assert(!mkdir((root+"/sub").c_str(),0700));assert(!symlink("sub",(root+"/dirlink").c_str()));expect("dirlink/file",3,sha);
 assert(!chmod(file.c_str(),0666));expect("data.bin",3,sha);assert(!chmod(file.c_str(),0600));
 assert(!mkfifo((root+"/fifo").c_str(),0600));expect("fifo",3,sha);
 assert(!link(file.c_str(),(root+"/hardlink").c_str()));expect("data.bin",3,sha);assert(!unlink((root+"/hardlink").c_str()));
 close(directory);assert(!symlink(root.c_str(),(root+"-link").c_str()));assert(demo_release_open_root((root+"-link").c_str(),geteuid())<0);
 assert(!chmod(root.c_str(),0777));assert(demo_release_open_root(root.c_str(),geteuid())<0);
 puts("RELEASE_IO_EXACT_HASH_SIZE_OWNER_NO_SYMLINK_NO_FIFO_NO_TRAVERSAL_PASS_NOT_GPU");
}

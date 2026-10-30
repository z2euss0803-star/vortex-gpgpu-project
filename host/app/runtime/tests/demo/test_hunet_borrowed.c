#include <vxl/hunet_fused_client.h>
#include <vxl/engine.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
static int device_fd,opens,closes,frees,busy,bad_binding;
int __wrap_open(const char *p,int flags,...){(void)flags;assert(!strcmp(p,"/dev/vortex0"));++opens;return device_fd;}
int __wrap_close(int fd){assert(fd==device_fd);++closes;return 0;}
int __wrap_ioctl(int fd,unsigned long cmd,...){assert(fd==device_fd);va_list ap;va_start(ap,cmd);struct vxl_request *r=va_arg(ap,struct vxl_request *);va_end(ap);if(cmd==VXL_GET_INFO){r->profile_id=4;r->generation=9;r->state=busy?VXL_RUNNING:VXL_READY;return 0;}if(cmd==VXL_HYBRID_GET_BINDING){struct vxl_fused_binding_v3 b={0};b.v2.v1.magic=VXL_HUNET_BINDING_MAGIC;b.v2.v1.version=3;b.v2.v1.bytes=512;b.v2.v1.profile_id=4;b.v2.v1.program_id=bad_binding?10:9;b.v2.v1.features=15;b.v2.v1.generation=9;b.v2.v1.state=VXL_READY;memset((vxl_u8*)&b+32,1,224);memset(b.v2.cpu_binary_sha256,1,32);memset(b.v2.cpu_contract_sha256,1,32);memset(b.v2.cpu_stages_sha256,1,32);memset(b.fusion_contract_sha256,1,32);memset(b.descriptors_sha256,1,32);memset(b.layout_sha256,1,32);memcpy((void*)(uintptr_t)r->user_ptr,&b,512);return 0;}if(cmd==VXL_FREE){++frees;return 0;}assert(0);return -1;}
extern int __real_close(int);
int main(void){int fd[2];assert(!pipe(fd));device_fd=fd[0];struct vxl_fused_client c;assert(!vxl_fused_client_attach_fd(&c,device_fd));assert(c.gpu.fd==device_fd&&opens==0&&closes==0&&c.gpu.program_id==9);for(unsigned i=0;i<4;++i)c.gpu.handles[i]=i+1;c.gpu.loaded=1;busy=1;assert(vxl_fused_client_close(&c)==-EBUSY&&!closes&&!frees&&c.gpu.fd==device_fd);busy=0;assert(!vxl_fused_client_close(&c)&&frees==4&&!closes&&fcntl(device_fd,F_GETFD)>=0);
 bad_binding=1;assert(vxl_fused_client_attach_fd(&c,device_fd)<0&&!closes&&!opens);bad_binding=0;assert(!vxl_fused_client_open(&c,"/dev/vortex0")&&opens==1);assert(!vxl_fused_client_close(&c)&&closes==1);__real_close(fd[0]);__real_close(fd[1]);puts("HUNET_PROGRAM9_BORROWED_OWNER_FD_BUSY_KEEP_RESIDENT_RELEASE_OWNED_CLI_UNCHANGED_PASS_NOT_BOARD");return 0;}

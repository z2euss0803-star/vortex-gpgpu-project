#include <vxl/hunet_client.h>
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef VXL_HUNET_SOURCE_BOUND
#include <vxl_hunet_source.h>
#endif
static struct vxl_request request(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=176;return r;}
static int call(struct vxl_hunet_client *c,vxl_u32 cmd,struct vxl_request *r){if(!c||c->fd<0)return -EBADF;return ioctl(c->fd,cmd,r)<0?-errno:0;}
static int payload(struct vxl_hunet_client *c,vxl_u32 cmd,void *p,vxl_u64 n,vxl_u64 id){struct vxl_request r=request();r.user_ptr=(uintptr_t)p;r.length=n;r.job_id=id;return call(c,cmd,&r);}
int vxl_hunet_read_asset(const char *root,const char *name,void *p,vxl_u64 n){
 char *copy,*part,*next;int fd,child,rc=0;struct stat s;vxl_u64 off=0;ssize_t got;
 if(!root||!name||!p||!n||name[0]=='/'||strchr(name,'\\'))return -EINVAL;
 fd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -errno;
 copy=strdup(name);if(!copy){close(fd);return -ENOMEM;}part=copy;
 for(;;){next=strchr(part,'/');if(next)*next=0;if(!*part||!strcmp(part,".")||!strcmp(part,"..")){rc=-EACCES;break;}child=openat(fd,part,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|(next?O_DIRECTORY:0));if(child<0){rc=-errno;break;}close(fd);fd=child;if(!next)break;part=next+1;}
 if(!rc&&(fstat(fd,&s)||!S_ISREG(s.st_mode)||(vxl_u64)s.st_size!=n))rc=-EINVAL;
 while(!rc&&off<n){got=read(fd,(vxl_u8 *)p+off,n-off);if(got<0){if(errno==EINTR)continue;rc=-errno;}else if(!got)rc=-EIO;else off+=(vxl_u64)got;}
 close(fd);free(copy);return rc;
}
int vxl_hunet_client_open(struct vxl_hunet_client *c,const char *device){
 struct vxl_request r=request();int rc;if(!c||!device||strcmp(device,"/dev/vortex0"))return -EINVAL;memset(c,0,sizeof(*c));c->fd=-1;c->program_id=6;
 c->fd=open(device,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(c->fd<0)return -errno;
 rc=call(c,VXL_GET_INFO,&r);if(!rc&&(r.profile_id!=4||r.state!=VXL_READY))rc=-ENODEV;
 if(!rc)rc=payload(c,VXL_HUNET_GET_BINDING,&c->binding,256,0);
 if(!rc&&(c->binding.magic!=VXL_HUNET_BINDING_MAGIC||c->binding.version!=1||c->binding.bytes!=256||c->binding.profile_id!=4||c->binding.program_id!=6||c->binding.features!=3))rc=-ESTALE;
 if(rc){vxl_hunet_client_close(c);return rc;}return 0;
}
int vxl_hunet_client_reserve(struct vxl_hunet_client *c,vxl_u32 purpose,vxl_u64 a,vxl_u64 n,vxl_u64 *out){struct vxl_request r=request();int rc;r.purpose=purpose;r.domain=purpose==VXL_ARGS?VXL_PS:VXL_PL;r.length=n;r.address=a;rc=call(c,purpose==VXL_ARGS||(purpose==VXL_DATA&&!a)?VXL_ALLOC:VXL_RESERVE,&r);if(!rc)*out=r.handle;return rc;}
int vxl_hunet_client_free(struct vxl_hunet_client *c,vxl_u64 h){struct vxl_request r=request();r.handle=h;return call(c,VXL_FREE,&r);}
int vxl_hunet_client_copy(struct vxl_hunet_client *c,vxl_u64 h,vxl_u64 off,void *p,vxl_u64 n,int to){struct vxl_request r=request();r.handle=h;r.offset=off;r.length=n;r.user_ptr=(uintptr_t)p;return call(c,to?VXL_COPY_TO:VXL_COPY_FROM,&r);}
static int install(struct vxl_hunet_client *c,const char *dir,const char *name,vxl_u64 h,vxl_u64 bytes,const vxl_u8 want[32]){vxl_u8 sha[32];void *p=malloc(bytes);int rc;if(!p)return -ENOMEM;rc=vxl_hunet_read_asset(dir,name,p,bytes);if(!rc){vxl_hunet_hash(p,bytes,sha);if(memcmp(sha,want,32))rc=-EACCES;}if(!rc)rc=vxl_hunet_client_copy(c,h,0,p,bytes,1);free(p);return rc;}
static int load(struct vxl_hunet_client *c,const char *dir,int wmma){
 struct vxl_hunet_binding_v1 binding;struct vxl_hunet_seal_v1 s={0};vxl_u8 sha[32];int rc;
 if(!c||c->fd<0||c->loaded||c->stopped)return -EINVAL;
 rc=vxl_hunet_read_asset(dir,"binding.bin",&binding,256);if(rc)return rc;
 if(binding.profile_id!=4||binding.program_id!=6||binding.magic!=VXL_HUNET_BINDING_MAGIC||memcmp(binding.profile_sha256,c->binding.profile_sha256,224))return -ESTALE;
 rc=vxl_hunet_client_reserve(c,VXL_CODE,VXL_HUNET_CODE,8388608,&c->handles[0]);if(rc)goto fail;
 rc=vxl_hunet_client_reserve(c,VXL_ARGS,0,128,&c->handles[3]);if(rc)goto fail;
 if(wmma){
  /* program7 identity is fixed by the driver; check locally against the compiled contract too. */
  extern const vxl_u8 *vxl_hunet_client_wmma_sha(void);
  rc=install(c,dir,"program7/code.bin",c->handles[0],30336,vxl_hunet_client_wmma_sha());if(rc)goto fail;
  rc=vxl_hunet_client_reserve(c,VXL_DATA,0x88000000,16384,&c->handles[2]);if(rc)goto fail;c->program_id=7;
 }else{
  rc=install(c,dir,"program6/code.bin",c->handles[0],41792,binding.program_sha256);if(rc)goto fail;
  rc=vxl_hunet_client_reserve(c,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864,&c->handles[1]);if(rc)goto fail;
  rc=vxl_hunet_client_reserve(c,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES,&c->handles[2]);if(rc)goto fail;
  rc=install(c,dir,"weights.bin",c->handles[1],23685056,binding.weights_sha256);if(rc)goto fail;
  s.magic=VXL_HUNET_SEAL_MAGIC;s.version=1;s.bytes=128;s.code=c->handles[0];s.weights=c->handles[1];s.workspace=c->handles[2];s.args=c->handles[3];vxl_hunet_hash(binding.profile_sha256,224,sha);memcpy(s.binding_sha256,sha,32);rc=payload(c,VXL_HUNET_SEAL,&s,128,0);if(rc)goto fail;
 }
 c->loaded=1;return 0;
fail:c->stopped=1;return rc;
}
#include "../generated/hunet/programs.h"
const vxl_u8 *vxl_hunet_client_wmma_sha(void){return vxl_hunet7_wire_sha256;}
int vxl_hunet_client_load(struct vxl_hunet_client *c,const char *d){return load(c,d,0);}
int vxl_hunet_client_load_wmma(struct vxl_hunet_client *c,const char *d){return load(c,d,1);}
int vxl_hunet_client_begin(struct vxl_hunet_client *c,const struct vxl_hunet_begin_v1 *b){if(!c||!c->loaded||c->stopped)return -EINVAL;return payload(c,VXL_HUNET_BEGIN_ROI,(void *)b,128,0);}
int vxl_hunet_client_submit(struct vxl_hunet_client *c,const struct vxl_hunet_job_v1 *j,vxl_u64 *id){
 struct vxl_request r=request();vxl_u64 previous;int rc;if(!c||!c->loaded||c->stopped||!id||!j)return -EINVAL;
 previous=c->last_job;c->last_job=0;*id=0;
 rc=vxl_hunet_client_copy(c,c->handles[3],0,(void *)j,128,1);if(rc)goto fail;
 r.handle=c->handles[0];r.handle2=c->handles[3];r.trust_id=c->program_id?c->program_id:6;r.deadline_ms=60000;r.length=128;rc=call(c,VXL_SUBMIT,&r);c->last_job=r.job_id;
 if(rc==-EFAULT&&!c->last_job){struct vxl_request status=request();if(!call(c,VXL_GET_STATUS,&status)&&status.job_id>previous&&(status.state==VXL_RUNNING||status.state==VXL_COMPLETED||status.state==VXL_QUARANTINED))c->last_job=status.job_id;}
 *id=c->last_job;if(rc)goto fail;return 0;
fail:c->stopped=1;return rc;
}
int vxl_hunet_client_wait(struct vxl_hunet_client *c,vxl_u64 id,vxl_u64 ms){struct vxl_request r=request();int rc;if(ms>60000)return -EINVAL;r.job_id=id;r.wait_ms=ms;rc=call(c,VXL_WAIT,&r);if(!rc&&(r.state!=VXL_COMPLETED||r.result))rc=r.result?r.result:-EIO;if(rc&&rc!=-ETIMEDOUT)c->stopped=1;return rc;}
int vxl_hunet_client_evidence(struct vxl_hunet_client *c,vxl_u64 id,struct vxl_hunet_job_evidence_v1 *e){return payload(c,VXL_HUNET_GET_EVIDENCE,e,256,id);}
int vxl_hunet_client_reap(struct vxl_hunet_client *c,vxl_u64 id){struct vxl_request r=request();r.job_id=id;return call(c,VXL_REAP,&r);}
int vxl_hunet_client_end(struct vxl_hunet_client *c,struct vxl_hunet_end_v1 *e){return payload(c,VXL_HUNET_END_ROI,e,128,0);}
int vxl_hunet_client_close(struct vxl_hunet_client *c){
 int fd;if(!c)return -EINVAL;fd=c->fd;
 if(c->borrowed_fd&&fd>=0){
  if(c->stopped)return -EIO;
  struct vxl_request r=request();int rc=call(c,VXL_GET_INFO,&r);if(rc)return rc;
  if(r.profile_id!=4||r.generation!=c->binding.generation)return -ESTALE;
  if(r.state!=VXL_READY||r.errors||r.pending_r||r.pending_w)return -EBUSY;
  for(unsigned i=4;i;--i)if(c->handles[i-1]){rc=vxl_hunet_client_free(c,c->handles[i-1]);if(rc){c->stopped=1;return rc;}c->handles[i-1]=0;}
  c->fd=-1;c->loaded=0;return 0;
 }
 c->fd=-1;c->loaded=0;return fd>=0&&close(fd)?-errno:0;
}

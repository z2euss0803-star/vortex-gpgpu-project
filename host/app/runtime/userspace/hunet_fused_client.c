#include <vxl/hunet_fused_client.h>
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include "../tools/hunet_opt_timing.h"
static enum vxl_opt_load_asset load_asset(const char *name){
 if(!strcmp(name,"cpu/gelu.elf"))return VXL_LOAD_CPU;
 if(!strcmp(name,"cpu-contract.json"))return VXL_LOAD_CPU_CONTRACT;
 if(!strcmp(name,"cpu-stages.json"))return VXL_LOAD_CPU_STAGES;
 if(!strcmp(name,"jobs.bin"))return VXL_LOAD_JOBS;
 if(!strcmp(name,"base/model.onnx"))return VXL_LOAD_MODEL;
 if(!strcmp(name,"base/matrix_descriptors.bin"))return VXL_LOAD_AUX;
 if(!strcmp(name,"fusion-contract.json"))return VXL_LOAD_FUSION;
 if(!strcmp(name,"descriptors.bin"))return VXL_LOAD_DESCRIPTORS;
 if(!strcmp(name,"layout.json"))return VXL_LOAD_LAYOUT;
 if(!strcmp(name,"base/weights.bin"))return VXL_LOAD_WEIGHTS;
 return VXL_LOAD_CODE;
}
static void load_mark(struct vxl_opt_load_timing *t,enum vxl_opt_load_field f,vxl_u64 begin,vxl_u64 bytes,int rc){vxl_opt_load_mark(t,f,begin,vxl_opt_load_now(t),bytes,rc);}
static struct vxl_request request(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=176;return r;}
static int call(struct vxl_fused_client *c,vxl_u32 cmd,void *p,vxl_u64 n,vxl_u64 id){
 struct vxl_request r=request();if(!c||c->gpu.fd<0)return -EBADF;r.user_ptr=(uintptr_t)p;r.length=n;r.job_id=id;return ioctl(c->gpu.fd,cmd,&r)<0?-errno:0;
}
int vxl_fused_client_attach_fd(struct vxl_fused_client *c,int fd){
 struct vxl_request r=request();int rc;if(!c||fd<0)return -EINVAL;
 memset(c,0,sizeof(*c));c->gpu.fd=fd;c->gpu.borrowed_fd=1;c->gpu.program_id=9;
 rc=ioctl(c->gpu.fd,VXL_GET_INFO,&r)<0?-errno:0;if(!rc&&(r.profile_id!=4||r.state!=VXL_READY))rc=-ENODEV;
 if(!rc)rc=call(c,VXL_HYBRID_GET_BINDING,&c->binding,512,0);
 if(!rc)rc=vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&c->binding,512);
 if(!rc&&(c->binding.v2.v1.profile_id!=4||c->binding.v2.v1.program_id!=9||c->binding.v2.v1.features!=15||c->binding.v2.v1.generation!=r.generation||c->binding.v2.v1.state!=VXL_READY))rc=-ESTALE;
 if(rc){c->gpu.fd=-1;return rc;}
 c->gpu.binding=c->binding.v2.v1;return 0;
}
int vxl_fused_client_open(struct vxl_fused_client *c,const char *path){
 if(!c||!path||strcmp(path,"/dev/vortex0"))return -EINVAL;
 memset(c,0,sizeof(*c));c->gpu.fd=-1;
 int fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
 int rc=vxl_fused_client_attach_fd(c,fd);if(rc){close(fd);return rc;}c->gpu.borrowed_fd=0;return 0;
}
static int self_hash(vxl_u8 sha[32],struct vxl_opt_load_timing *t){
 struct vxl_hunet_sha hash;struct stat s;vxl_u8 b[4096];ssize_t n;vxl_u64 stamp,total=0;int fd,rc=0;
 vxl_opt_load_asset(t,VXL_LOAD_SELF);stamp=vxl_opt_load_now(t);fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
 if(fd<0){rc=-errno;load_mark(t,VXL_LOAD_READ,stamp,0,rc);return rc;}
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size<64||s.st_size>67108864){close(fd);load_mark(t,VXL_LOAD_READ,stamp,0,-EINVAL);return -EINVAL;}
 load_mark(t,VXL_LOAD_READ,stamp,0,0);stamp=vxl_opt_load_now(t);vxl_hunet_sha_init(&hash);load_mark(t,VXL_LOAD_HASH,stamp,0,0);
 for(;;){stamp=vxl_opt_load_now(t);n=read(fd,b,sizeof(b));rc=n<0&&errno!=EINTR?-errno:0;load_mark(t,VXL_LOAD_READ,stamp,n>0?(vxl_u64)n:0,rc);
  if(!n)break;
  if(n<0){if(errno==EINTR){if(t)t->read_interrupts++;continue;}break;}
  total+=(vxl_u64)n;stamp=vxl_opt_load_now(t);vxl_hunet_sha_update(&hash,b,(vxl_u64)n);load_mark(t,VXL_LOAD_HASH,stamp,(vxl_u64)n,0);
 }
 if(total!=(vxl_u64)s.st_size)rc=-EIO;
 if(close(fd)&&!rc)rc=-errno;
 if(!rc){stamp=vxl_opt_load_now(t);vxl_hunet_sha_final(&hash,sha);load_mark(t,VXL_LOAD_HASH,stamp,0,0);}
 return rc;
}
static int asset_hash(const char *root,const char *name,const vxl_u8 want[32],struct vxl_opt_load_timing *t){
 char path[4096];struct stat s;vxl_u8 sha[32];void *p;int rc;vxl_u64 stamp;
 vxl_opt_load_asset(t,load_asset(name));stamp=vxl_opt_load_now(t);
 if(snprintf(path,sizeof(path),"%s/%s",root,name)>=(int)sizeof(path)||stat(path,&s)||s.st_size<1||s.st_size>67108864){load_mark(t,VXL_LOAD_READ,stamp,0,-EINVAL);return -EINVAL;}
 p=malloc((size_t)s.st_size);if(!p){load_mark(t,VXL_LOAD_READ,stamp,0,-ENOMEM);return -ENOMEM;}load_mark(t,VXL_LOAD_READ,stamp,0,0);
 stamp=vxl_opt_load_now(t);rc=vxl_hunet_read_asset(root,name,p,(vxl_u64)s.st_size);load_mark(t,VXL_LOAD_READ,stamp,(vxl_u64)s.st_size,rc);
 if(!rc){stamp=vxl_opt_load_now(t);vxl_hunet_hash(p,(vxl_u64)s.st_size,sha);if(memcmp(sha,want,32))rc=-ESTALE;load_mark(t,VXL_LOAD_HASH,stamp,(vxl_u64)s.st_size,rc);}free(p);return rc;
}
static int install(struct vxl_fused_client *c,const char *root,const char *name,vxl_u64 handle,vxl_u64 bytes,const vxl_u8 want[32],struct vxl_opt_load_timing *t){
 vxl_u8 sha[32];void *p;int rc;vxl_u64 stamp;vxl_opt_load_asset(t,load_asset(name));stamp=vxl_opt_load_now(t);p=malloc((size_t)bytes);if(!p){load_mark(t,VXL_LOAD_READ,stamp,0,-ENOMEM);return -ENOMEM;}load_mark(t,VXL_LOAD_READ,stamp,0,0);
 stamp=vxl_opt_load_now(t);rc=vxl_hunet_read_asset(root,name,p,bytes);load_mark(t,VXL_LOAD_READ,stamp,bytes,rc);
 if(!rc){stamp=vxl_opt_load_now(t);vxl_hunet_hash(p,bytes,sha);if(memcmp(sha,want,32))rc=-ESTALE;load_mark(t,VXL_LOAD_HASH,stamp,bytes,rc);}
 if(!rc){stamp=vxl_opt_load_now(t);rc=vxl_hunet_client_copy(&c->gpu,handle,0,p,bytes,1);load_mark(t,load_asset(name)==VXL_LOAD_WEIGHTS?VXL_LOAD_WEIGHTS_COPY:VXL_LOAD_CODE_COPY,stamp,bytes,rc);}
 free(p);return rc;
}
int vxl_fused_client_load_timed(struct vxl_fused_client *c,const char *root,struct vxl_opt_load_timing *t){
 struct vxl_fused_binding_v3 b,actual;struct vxl_hunet_seal_v1 s={0};vxl_u8 sha[32];int rc;vxl_u64 stamp;
 vxl_opt_load_begin(t);
 if(!c||!root||c->gpu.fd<0||c->gpu.loaded||c->gpu.stopped){vxl_opt_load_finish(t,-EINVAL);return -EINVAL;}
 vxl_opt_load_asset(t,VXL_LOAD_BINDING);stamp=vxl_opt_load_now(t);rc=vxl_hunet_read_asset(root,"binding-fused.bin",&b,512);load_mark(t,VXL_LOAD_READ,stamp,512,rc);if(rc)goto fail;
 rc=vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&b,512);if(rc)goto fail;
 actual=c->binding;actual.v2.v1.generation=actual.v2.v1.state=0;
 if(b.v2.v1.program_id!=9||b.v2.v1.features!=15||memcmp(&b,&actual,512)){rc=-ESTALE;goto fail;}
 rc=self_hash(sha,t);if(rc)goto fail;if(memcmp(sha,b.v2.cpu_binary_sha256,32)){rc=-ESTALE;goto fail;}
 rc=asset_hash(root,"cpu/gelu.elf",b.v2.cpu_binary_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"cpu-contract.json",b.v2.cpu_contract_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"cpu-stages.json",b.v2.cpu_stages_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"jobs.bin",b.v2.v1.jobs_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"base/model.onnx",b.v2.v1.model_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"base/matrix_descriptors.bin",b.v2.v1.aux_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"fusion-contract.json",b.fusion_contract_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"descriptors.bin",b.descriptors_sha256,t);if(rc)goto fail;
 rc=asset_hash(root,"layout.json",b.layout_sha256,t);if(rc)goto fail;
 vxl_opt_load_asset(t,VXL_LOAD_ADDRESS);stamp=vxl_opt_load_now(t);rc=vxl_hunet_client_reserve(&c->gpu,VXL_CODE,VXL_HUNET_CODE,8388608,&c->gpu.handles[0]);load_mark(t,VXL_LOAD_RESERVE,stamp,8388608,rc);if(rc)goto fail;
 stamp=vxl_opt_load_now(t);rc=vxl_hunet_client_reserve(&c->gpu,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864,&c->gpu.handles[1]);load_mark(t,VXL_LOAD_RESERVE,stamp,67108864,rc);if(rc)goto fail;
 stamp=vxl_opt_load_now(t);rc=vxl_hunet_client_reserve(&c->gpu,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES,&c->gpu.handles[2]);load_mark(t,VXL_LOAD_RESERVE,stamp,VXL_HUNET_WS_BYTES,rc);if(rc)goto fail;
 stamp=vxl_opt_load_now(t);rc=vxl_hunet_client_reserve(&c->gpu,VXL_ARGS,0,128,&c->gpu.handles[3]);load_mark(t,VXL_LOAD_RESERVE,stamp,128,rc);if(rc)goto fail;
 {char path[4096];struct stat st;if(snprintf(path,sizeof(path),"%s/program9/code.bin",root)>=(int)sizeof(path)||stat(path,&st)||st.st_size<1||st.st_size>8388608){rc=-EINVAL;goto fail;}
 rc=install(c,root,"program9/code.bin",c->gpu.handles[0],(vxl_u64)st.st_size,b.v2.v1.program_sha256,t);}if(rc)goto fail;
 rc=install(c,root,"base/weights.bin",c->gpu.handles[1],23685056,b.v2.v1.weights_sha256,t);if(rc)goto fail;
 s.magic=VXL_HYBRID_SEAL_MAGIC;s.version=1;s.bytes=128;s.code=c->gpu.handles[0];s.weights=c->gpu.handles[1];s.workspace=c->gpu.handles[2];s.args=c->gpu.handles[3];vxl_hunet_hash(&b,512,s.binding_sha256);
 vxl_opt_load_asset(t,VXL_LOAD_TRANSACTION);stamp=vxl_opt_load_now(t);rc=call(c,VXL_HYBRID_SEAL,&s,128,0);load_mark(t,VXL_LOAD_SEAL,stamp,0,rc);if(rc)goto fail;c->gpu.loaded=1;vxl_opt_load_finish(t,0);return 0;
fail:c->gpu.stopped=1;vxl_opt_load_finish(t,rc);return rc;
}
int vxl_fused_client_load(struct vxl_fused_client *c,const char *root){return vxl_fused_client_load_timed(c,root,NULL);}
int vxl_fused_client_begin_cpu(struct vxl_fused_client *c,struct vxl_hybrid_begin_v1 *p){return call(c,VXL_HYBRID_CPU_BEGIN,p,128,0);}
int vxl_fused_client_commit_cpu(struct vxl_fused_client *c,const struct vxl_hybrid_commit_v1 *p){return call(c,VXL_HYBRID_CPU_COMMIT,(void *)p,128,0);}
int vxl_fused_client_evidence(struct vxl_fused_client *c,vxl_u64 token,struct vxl_hybrid_cpu_evidence_v1 *p){return call(c,VXL_HYBRID_GET_CPU_EVIDENCE,p,384,token);}
int vxl_fused_client_end(struct vxl_fused_client *c,struct vxl_hybrid_end_v1 *p){return call(c,VXL_HYBRID_END,p,256,0);}
int vxl_fused_client_close(struct vxl_fused_client *c){return c?vxl_hunet_client_close(&c->gpu):-EINVAL;}

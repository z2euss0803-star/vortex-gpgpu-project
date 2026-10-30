#include <vxl/hunet_hybrid_client.h>
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
static struct vxl_request request(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=176;return r;}
static int call(struct vxl_hybrid_client *c,vxl_u32 cmd,void *p,vxl_u64 n,vxl_u64 id){
 struct vxl_request r=request();if(!c||c->gpu.fd<0)return -EBADF;r.user_ptr=(uintptr_t)p;r.length=n;r.job_id=id;return ioctl(c->gpu.fd,cmd,&r)<0?-errno:0;
}
int vxl_hybrid_client_open(struct vxl_hybrid_client *c,const char *path){
 struct vxl_request r=request();int rc;if(!c||!path||strcmp(path,"/dev/vortex0"))return -EINVAL;
 memset(c,0,sizeof(*c));c->gpu.fd=-1;c->gpu.program_id=8;c->gpu.fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(c->gpu.fd<0)return -errno;
 rc=ioctl(c->gpu.fd,VXL_GET_INFO,&r)<0?-errno:0;if(!rc&&(r.profile_id!=4||r.state!=VXL_READY))rc=-ENODEV;
 if(!rc)rc=call(c,VXL_HYBRID_GET_BINDING,&c->binding,384,0);
 if(!rc)rc=vxl_hybrid_payload_validate(VXL_HYBRID_GET_BINDING,&c->binding,384);
 if(!rc&&(c->binding.v1.profile_id!=4||c->binding.v1.program_id!=8||c->binding.v1.features!=7||c->binding.v1.generation!=r.generation||c->binding.v1.state!=VXL_READY))rc=-ESTALE;
 if(rc){vxl_hybrid_client_close(c);return rc;}
 c->gpu.binding=c->binding.v1;return 0;
}
static int self_hash(vxl_u8 sha[32]){
 struct vxl_hunet_sha hash;struct stat s;vxl_u8 b[4096];ssize_t n;vxl_u64 total=0;int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC),rc=0;
 if(fd<0)return -errno;
 if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size<64||s.st_size>67108864){close(fd);return -EINVAL;}
 vxl_hunet_sha_init(&hash);
 while((n=read(fd,b,sizeof(b)))!=0){if(n<0){if(errno==EINTR)continue;rc=-errno;break;}total+=(vxl_u64)n;vxl_hunet_sha_update(&hash,b,(vxl_u64)n);}
 if(total!=(vxl_u64)s.st_size)rc=-EIO;
 if(close(fd)&&!rc)rc=-errno;
 if(!rc)vxl_hunet_sha_final(&hash,sha);
 return rc;
}
static int asset_hash(const char *root,const char *name,const vxl_u8 want[32]){
 char path[4096];struct stat s;vxl_u8 sha[32];void *p;int rc;
 if(snprintf(path,sizeof(path),"%s/%s",root,name)>=(int)sizeof(path)||stat(path,&s)||s.st_size<1||s.st_size>67108864)return -EINVAL;
 p=malloc((size_t)s.st_size);if(!p)return -ENOMEM;
 rc=vxl_hunet_read_asset(root,name,p,(vxl_u64)s.st_size);if(!rc){vxl_hunet_hash(p,(vxl_u64)s.st_size,sha);if(memcmp(sha,want,32))rc=-ESTALE;}free(p);return rc;
}
static int install(struct vxl_hybrid_client *c,const char *root,const char *name,vxl_u64 handle,vxl_u64 bytes,const vxl_u8 want[32]){
 vxl_u8 sha[32];void *p=malloc((size_t)bytes);int rc;if(!p)return -ENOMEM;
 rc=vxl_hunet_read_asset(root,name,p,bytes);if(!rc){vxl_hunet_hash(p,bytes,sha);if(memcmp(sha,want,32))rc=-ESTALE;}
 if(!rc)rc=vxl_hunet_client_copy(&c->gpu,handle,0,p,bytes,1);
 free(p);return rc;
}
int vxl_hybrid_client_load(struct vxl_hybrid_client *c,const char *root){
 struct vxl_hybrid_binding_v2 b,actual;struct vxl_hunet_seal_v1 s={0};vxl_u8 sha[32];int rc;
 if(!c||!root||c->gpu.fd<0||c->gpu.loaded||c->gpu.stopped)return -EINVAL;
 rc=vxl_hunet_read_asset(root,"binding-hybrid.bin",&b,384);if(rc)goto fail;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_GET_BINDING,&b,384);if(rc)goto fail;
 actual=c->binding;actual.v1.generation=actual.v1.state=0;
 if(b.v1.program_id!=8||b.v1.features!=7||memcmp(&b,&actual,384)){rc=-ESTALE;goto fail;}
 rc=self_hash(sha);if(rc)goto fail;if(memcmp(sha,b.cpu_binary_sha256,32)){rc=-ESTALE;goto fail;}
 rc=asset_hash(root,"cpu/gelu.elf",b.cpu_binary_sha256);if(rc)goto fail;
 rc=asset_hash(root,"cpu-contract.json",b.cpu_contract_sha256);if(rc)goto fail;
 rc=asset_hash(root,"cpu-stages.json",b.cpu_stages_sha256);if(rc)goto fail;
 rc=asset_hash(root,"base/jobs.bin",b.v1.jobs_sha256);if(rc)goto fail;
 rc=asset_hash(root,"base/model.onnx",b.v1.model_sha256);if(rc)goto fail;
 rc=asset_hash(root,"base/matrix_descriptors.bin",b.v1.aux_sha256);if(rc)goto fail;
 rc=vxl_hunet_client_reserve(&c->gpu,VXL_CODE,VXL_HUNET_CODE,8388608,&c->gpu.handles[0]);if(rc)goto fail;
 rc=vxl_hunet_client_reserve(&c->gpu,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864,&c->gpu.handles[1]);if(rc)goto fail;
 rc=vxl_hunet_client_reserve(&c->gpu,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES,&c->gpu.handles[2]);if(rc)goto fail;
 rc=vxl_hunet_client_reserve(&c->gpu,VXL_ARGS,0,128,&c->gpu.handles[3]);if(rc)goto fail;
 rc=install(c,root,"program8/code.bin",c->gpu.handles[0],41792,b.v1.program_sha256);if(rc)goto fail;
 rc=install(c,root,"base/weights.bin",c->gpu.handles[1],23685056,b.v1.weights_sha256);if(rc)goto fail;
 s.magic=VXL_HYBRID_SEAL_MAGIC;s.version=1;s.bytes=128;s.code=c->gpu.handles[0];s.weights=c->gpu.handles[1];s.workspace=c->gpu.handles[2];s.args=c->gpu.handles[3];vxl_hunet_hash(&b,384,s.binding_sha256);
 rc=call(c,VXL_HYBRID_SEAL,&s,128,0);if(rc)goto fail;c->gpu.loaded=1;return 0;
fail:c->gpu.stopped=1;return rc;
}
int vxl_hybrid_client_begin_cpu(struct vxl_hybrid_client *c,struct vxl_hybrid_begin_v1 *p){return call(c,VXL_HYBRID_CPU_BEGIN,p,128,0);}
int vxl_hybrid_client_commit_cpu(struct vxl_hybrid_client *c,const struct vxl_hybrid_commit_v1 *p){return call(c,VXL_HYBRID_CPU_COMMIT,(void *)p,128,0);}
int vxl_hybrid_client_evidence(struct vxl_hybrid_client *c,vxl_u64 token,struct vxl_hybrid_cpu_evidence_v1 *p){return call(c,VXL_HYBRID_GET_CPU_EVIDENCE,p,384,token);}
int vxl_hybrid_client_end(struct vxl_hybrid_client *c,struct vxl_hybrid_end_v1 *p){return call(c,VXL_HYBRID_END,p,256,0);}
int vxl_hybrid_client_close(struct vxl_hybrid_client *c){return c?vxl_hunet_client_close(&c->gpu):-EINVAL;}

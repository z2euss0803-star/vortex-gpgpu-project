#include <vxl/s8_client.h>
#include <vxl/s8.h>
#include <stdio.h>
/* Explicit bind/unbind retains the requested fd API without hiding53 object
 * handles in files or changing the public ioctl. One session per calling thread. */
static _Thread_local struct vxl_s8_client *bound;
static struct vxl_request request(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=176;return r;}
int vxl_s8_object_contract(unsigned i,struct vxl_s8_client_object *o){
 if(!o||i>=53)return -EINVAL;
 memset(o,0,sizeof(*o));o->purpose=i==0?VXL_CODE:i==1?VXL_ARGS:VXL_DATA;
 if(i==0){o->gpu=0x80000000;o->bytes=0x100000;}else if(i==1){o->gpu=0x80100000;o->bytes=4096;}
 else if(i<18){o->gpu=0x81000000+(i-2)*0x20000;o->bytes=0x20000;}else if(i<34){o->gpu=0x81200000+(i-18)*0x20000;o->bytes=0x20000;}
 else if(i<50){o->gpu=0x81400000+(i-34)*4096;o->bytes=4096;}else if(i<52){o->gpu=i==50?0x81410000:0x81411000;o->bytes=4096;}else{o->gpu=0xfff70000;o->bytes=0x80040;}
 return 0;
}
int vxl_s8_client_bind(struct vxl_s8_client *c){if(bound||!c||c->fd<0||!c->io.call||!c->io.save||!c->io.trace||!c->io.seal||!c->io.now||!c->io.event)return -EINVAL;bound=c;return 0;}
void vxl_s8_client_unbind(struct vxl_s8_client *c){if(bound==c)bound=0;}
static int call(struct vxl_s8_client *c,const char *name,vxl_u32 cmd,struct vxl_request *r){vxl_u64 a=c->io.now(c->io.context);int rc=c->io.call(c->io.context,c->fd,cmd,r);vxl_u64 b=c->io.now(c->io.context);int saved=c->io.event(c->io.context,name,a,b,rc);if(b<a)return -ERANGE;return rc?rc:saved;}
static int copy(struct vxl_s8_client *c,unsigned index,vxl_u64 offset,void *data,vxl_u32 bytes,int to){struct vxl_request r=request();r.handle=c->objects[index].handle;r.offset=offset;r.length=bytes;r.user_ptr=(uintptr_t)data;int rc=call(c,to?"load_or_args":"capture",to?VXL_COPY_TO:VXL_COPY_FROM,&r);return rc?rc:r.used==bytes?0:-EIO;}
int vxl_s8_client_prepare(struct vxl_s8_client *c){
 struct vxl_request r=request();int rc;if(!c||c->fd<0||c->stopped||c->job_id)return -EINVAL;
 rc=call(c,"identity",VXL_GET_INFO,&r);if(rc)goto fail;
 if(r.profile_id!=5||r.state!=VXL_READY||!r.generation){rc=-ENODEV;goto fail;}c->generation=r.generation;if(!c->sequence)c->sequence=1;
 for(unsigned i=0;i<53;++i){struct vxl_s8_client_object want;vxl_s8_object_contract(i,&want);struct vxl_s8_client_object *o=&c->objects[i];if(o->gpu!=want.gpu||o->bytes!=want.bytes||o->purpose!=want.purpose||!o->initial){rc=-EINVAL;goto fail;}
  if(!c->loaded){r=request();r.domain=VXL_PL;r.purpose=o->purpose;r.address=o->gpu;r.length=o->bytes;rc=call(c,"reserve",VXL_RESERVE,&r);if(rc)goto fail;if(!r.handle||r.gpu_address!=o->gpu){rc=-ESTALE;goto fail;}for(unsigned k=0;k<i;++k)if(c->objects[k].handle==r.handle){rc=-ESTALE;goto fail;}o->handle=r.handle;}
  rc=copy(c,i,0,(void *)o->initial,o->bytes,1);if(rc)goto fail;
 }
 c->loaded=1;return 0;
fail:c->stopped=1;return rc;
}
static int capture(struct vxl_s8_client *c,vxl_u32 gpu,vxl_u32 bytes){
 vxl_u8 data[4096];char name[64];int rc;if(!bytes||bytes>sizeof(data))return -EINVAL;
 for(unsigned i=0;i<53;++i){struct vxl_s8_client_object *o=&c->objects[i];if(gpu>=o->gpu&&(vxl_u64)gpu+bytes<=(vxl_u64)o->gpu+o->bytes){rc=copy(c,i,gpu-o->gpu,data,bytes,0);if(rc)return rc;snprintf(name,sizeof(name),"capture-%03u-%08x.bin",c->captured,gpu);rc=c->io.save(c->io.context,name,data,bytes);if(!rc)c->captured++;return rc;}}
 return -ERANGE;
}
static int capture_all(struct vxl_s8_client *c,const struct vxl_s8_job_v1 *j){
 int rc;c->captured=0;
#define TAKE(a,n) do{rc=capture(c,(vxl_u32)(a),(n));if(rc)return rc;}while(0)
 for(unsigned slot=0;slot<16;++slot)TAKE(0x81400000u+slot*4096,4096);
 TAKE(0x81410000,4096);TAKE(0x81411000,4096);TAKE(0x800f0000,64);TAKE(0x800f0040,64);TAKE(0x800fffc0,64);
 for(unsigned hart=0;hart<64;++hart)TAKE(0xffff0000u-(hart+1)*8192u,64);
 TAKE(0xffff0000u,64);
 for(unsigned which=0;which<2;++which)for(unsigned slot=0;slot<16;++slot){vxl_u32 base=(which?0x81200000u:0x81000000u)+slot*0x20000u;TAKE(base,64);TAKE(base+64+vxl_s8_input_bytes(j,which),64);TAKE(base+0x1ffc0,64);}
#undef TAKE
 return c->captured==182?0:-EPROTO;
}
static int traces(struct vxl_s8_client *c,int post){static const char *names[]={"binding.bin","jobs.csv","operations.csv"};int first=0;for(unsigned i=0;i<3;++i){int rc=c->io.trace(c->io.context,names[i],post);if(rc&&!first)first=rc;}return first;}
int vxl_s8_run_case(int fd,const struct vxl_s8_job_v1 *input,const char *directory){
 struct vxl_s8_client *c=bound;struct vxl_s8_job_v1 j;struct vxl_request r;int rc,saved;char error[192];
 if(!c||c->fd!=fd||!input||!directory||directory[0]!='/'||c->stopped||!c->loaded||c->job_id)return -EINVAL;
 j=*input;j.generation=c->generation;j.sequence=c->sequence;j.args_crc32=vxl_s8_job_crc(&j);
 rc=c->io.save(c->io.context,"job.bin",&j,128);if(rc)goto fail;
 rc=copy(c,1,64,&j,128,1);if(rc)goto fail;
 r=request();r.handle=c->objects[0].handle;r.handle2=c->objects[1].handle;r.offset2=64;r.length=128;r.trust_id=11;r.deadline_ms=60000;rc=call(c,"submit",VXL_SUBMIT,&r);c->job_id=r.job_id;if(rc||!c->job_id){if(!rc)rc=-EPROTO;goto fail;}
 r=request();r.job_id=c->job_id;r.wait_ms=60000;rc=call(c,"wait",VXL_WAIT,&r);if(rc)goto fail;if(r.state!=VXL_COMPLETED||r.result){rc=r.result?r.result:-EIO;goto fail;}
 rc=capture_all(c,&j);if(rc)goto fail;
 rc=traces(c,0);if(rc)goto fail;
 rc=c->io.seal(c->io.context,0);if(rc)goto fail;
 r=request();r.job_id=c->job_id;rc=call(c,"reap",VXL_REAP,&r);if(rc)goto fail;
 rc=traces(c,1);if(rc)goto fail;rc=c->io.seal(c->io.context,1);if(rc)goto fail;
 c->job_id=0;c->sequence++;return 0;
fail:
 c->stopped=1;saved=rc;traces(c,0);snprintf(error,sizeof(error),"{\"state\":\"FAIL_STOP\",\"error\":%d,\"job_id\":%llu,\"captures\":%u,\"missing_reason\":\"first_error_no_retry_no_free\"}\n",saved,(unsigned long long)c->job_id,c->captured);c->io.save(c->io.context,"error.json",error,(vxl_u32)strlen(error));return saved;
}

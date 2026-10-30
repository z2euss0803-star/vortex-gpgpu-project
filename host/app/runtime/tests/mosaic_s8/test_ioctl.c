#define main runtime_component_main
#include "test_runtime.c"
#undef main
static unsigned frees;
static int alloc_cb(void *p,struct vxl_object *o){(void)p;(void)o;return 0;}
static void free_cb(void *p,struct vxl_object *o){(void)p;(void)o;++frees;}
static int read_user(void *p,void *to,vxl_u64 from,vxl_u64 n){(void)p;if(!from)return -EFAULT;memcpy(to,(const void *)(uintptr_t)from,n);return 0;}
static int write_user(void *p,vxl_u64 to,const void *from,vxl_u64 n){(void)p;if(!to)return -EFAULT;memcpy((void *)(uintptr_t)to,from,n);return 0;}
static const struct vxl_ops ops={.alloc=alloc_cb,.free=free_cb,.read_user=read_user,.write_user=write_user};
static struct vxl_request request(void){struct vxl_request r={.abi_major=1,.struct_size=176,.generation=9,.profile_id=6};return r;}
static int call(vxl_u32 cmd,struct vxl_request *r){return mosaic_engine_call(&e,77,cmd,r);}
static void allocate_all(void){
 memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;e.ops=&ops;frees=0;
 const vxl_u32 address[5]={0x80000000,0x80800000,0x82000000,0xfff00000,0};const vxl_u64 size[5]={0x800000,0x1000000,catalog.layout.end-0x82000000,0x100000,128};
 for(unsigned i=0;i<5;++i){struct vxl_request r=request();r.domain=i==4?VXL_PS:VXL_PL;r.purpose=i==0?VXL_CODE:i==4?VXL_ARGS:VXL_DATA;r.address=address[i];r.length=size[i];assert(!call(i==4?VXL_ALLOC:VXL_RESERVE,&r));assert(r.handle&&r.gpu_address==address[i]);}
}
int mosaic_engine_test_main(int argc,char **argv){
 assert(argc==2);setup(argv[1]);e.ops=&ops;struct vxl_request r=request();assert(!call(VXL_GET_INFO,&r)&&r.profile_id==6);allocate_all();
 r=request();r.domain=VXL_PL;r.purpose=VXL_DATA;r.address=0x83000000;r.length=4096;assert(call(VXL_RESERVE,&r)<0);
 r=request();r.trust_id=11;r.handle=e.objects[0].handle;r.handle2=e.objects[4].handle;r.deadline_ms=60000;assert(call(VXL_SUBMIT,&r)==-EACCES&&!f.starts);
 struct vxl_mosaic_seal_v1 s=seal();r=request();r.user_ptr=(uintptr_t)&s;r.length=128;assert(!call(VXL_MOSAIC_SEAL,&r));assert(h.loaded);
 r=request();r.handle=e.objects[0].handle;r.length=16;r.user_ptr=(uintptr_t)program_flat;assert(call(VXL_COPY_TO,&r)==-EACCES);
 vxl_u8 input[1152]={0};r=request();r.handle=e.objects[2].handle;r.offset=catalog.layout.region[MOSAIC_FP_A].address-0x82000000;r.user_ptr=(uintptr_t)input;r.length=sizeof(input);assert(!call(VXL_COPY_TO,&r)&&h.input_written==sizeof(input));
 struct vxl_mosaic_frame_v1 fr=frame();r=request();r.user_ptr=(uintptr_t)&fr;r.length=128;assert(!call(VXL_MOSAIC_BEGIN,&r));
 struct mosaic_job_v1 j;assert(!mosaic_next_job(&c,1,&j));r=request();r.handle=e.objects[4].handle;r.user_ptr=(uintptr_t)&j;r.length=128;assert(!call(VXL_COPY_TO,&r));
 r=request();r.handle=e.objects[0].handle;r.handle2=e.objects[4].handle;r.trust_id=12;r.deadline_ms=60000;h.submit_enter_ns=now(NULL);assert(!call(VXL_SUBMIT,&r)&&r.job_id&&f.starts==1);done();assert(mosaic_runtime_poll(&e)==1);
 vxl_u64 id=e.job.id;r=request();r.job_id=id;assert(call(VXL_REAP,&r)==-EBUSY);
 vxl_u8 wire[512];r=request();r.job_id=id;r.user_ptr=(uintptr_t)wire;r.length=512;assert(!call(VXL_MOSAIC_EVIDENCE,&r));struct mosaic_delivery ticket;mosaic_delivery_capture(&c,&ticket);assert(ticket.serial&&h.evidence_bytes==0);
 r=request();r.job_id=id;assert(call(VXL_REAP,&r)==-EBUSY);r=request();r.handle=e.objects[0].handle;assert(call(VXL_FREE,&r)==-EBUSY&&!frees);
 assert(!mosaic_delivery_finish(&c,&ticket,1));assert(h.evidence_bytes==512);r=request();r.job_id=id;assert(!call(VXL_REAP,&r)&&h.first==1);assert(mosaic_delivery_finish(&c,&ticket,1)==-ESTALE);
 /* Envelope failure after a successful payload copy cannot earn REAP. */
 assert(!mosaic_next_job(&c,1,&j));memcpy(h.args_shadow,&j,128);h.submit_enter_ns=now(NULL);r=request();r.handle=e.objects[0].handle;r.handle2=e.objects[4].handle;r.trust_id=12;r.deadline_ms=60000;assert(!call(VXL_SUBMIT,&r));done();assert(mosaic_runtime_poll(&e)==1);
 r=request();r.job_id=e.job.id;r.user_ptr=(uintptr_t)wire;r.length=512;assert(!call(VXL_MOSAIC_EVIDENCE,&r));mosaic_delivery_capture(&c,&ticket);assert(mosaic_delivery_finish(&c,&ticket,0)==-EFAULT);assert(e.quarantined&&e.job.pinned);r=request();r.job_id=e.job.id;assert(call(VXL_REAP,&r)<0);
 mosaic_engine_close(&e,77);assert(!frees&&e.owner==77&&!e.opened&&e.job.pinned);cleanup();
 setup(argv[1]);allocate_all();mosaic_engine_close(&e,77);assert(frees==4&&!e.owner&&!e.opened);cleanup();
 puts("MOSAIC_ENGINE_IOCTL_PAYLOAD_ENVELOPE_DELIVERY_OWNER_PIN_PASS_NOT_KERNEL_NOT_BOARD");return 0;
}
#ifndef MOSAIC_IOCTL_EMBED_TEST
int main(int argc,char **argv){return mosaic_engine_test_main(argc,argv);}
#endif

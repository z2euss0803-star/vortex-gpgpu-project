#include <vxl/mock.h>
#include <vxl/hunet.h>
#include <vxl/s8.h>
#include <uapi/vx_mosaic.h>
#include <assert.h>
#include <stdio.h>
static unsigned prepares;
static int prepare_spy(void *p,struct vxl_engine *e,struct vxl_object *a,vxl_u64 ao,struct vxl_object *b,vxl_u64 bo,vxl_u32 id,struct vxl_prepared *out) {
 (void)p;(void)e;(void)a;(void)ao;(void)b;(void)bo;(void)out;assert(id==11);prepares++;return -EAGAIN;
}
static struct vxl_request request(void) {struct vxl_request r={.abi_major=1,.struct_size=176};return r;}
int main(void) {
 struct vxl_mock m;struct vxl_s8_ctx c;vxl_u8 tag[16]={0x55};struct vxl_request r;vxl_u64 code,args;
 vxl_mock_init(&m);assert(vxl_engine_open(&m.engine,1)==0);r=request();assert(vxl_engine_call(&m.engine,1,VXL_GET_INFO,&r)==0 && r.profile_id==0);vxl_engine_close(&m.engine,1);
 struct vxl_hunet_ctx hc;struct vxl_hunet_release hr;
 vxl_hunet_release_defaults(&hr);assert(vxl_engine_init_profile(&m.engine,m.engine.ops,&m,1,4)==0);
 assert(vxl_hunet_layout_init(&hc,&m.engine,&hr)==0);assert(vxl_engine_open(&m.engine,1)==0);
 r=request();assert(vxl_engine_call(&m.engine,1,VXL_GET_INFO,&r)==0 && r.profile_id==4);
 vxl_engine_close(&m.engine,1);
 vxl_mock_init(&m);struct vxl_ops ops=*m.engine.ops;ops.prepare=prepare_spy;
 assert(vxl_engine_init_profile(&m.engine,&ops,&m,1,5)==0);
 assert(vxl_engine_open(&m.engine,1)==-ENODEV);
 assert(vxl_s8_context_init(&c,&m.engine,tag)==0);assert(!m.engine.hunet);
 assert(vxl_engine_open(&m.engine,1)==0);r=request();assert(vxl_engine_call(&m.engine,1,VXL_GET_INFO,&r)==0 && r.profile_id==5);
 r=request();assert(vxl_engine_call(&m.engine,1,VXL_HUNET_SEAL,&r)==-EOPNOTSUPP);
 r=request();assert(vxl_engine_call(&m.engine,1,VXL_MOSAIC_BINDING,&r)==-ENOTTY);
 r=request();r.domain=VXL_PL;r.purpose=VXL_CODE;r.address=0x80000000;r.length=0x100000;
 assert(vxl_engine_call(&m.engine,1,VXL_RESERVE,&r)==0);code=r.handle;
 r=request();r.domain=VXL_PL;r.purpose=VXL_ARGS;r.address=0x80100000;r.length=0x1000;
 assert(vxl_engine_call(&m.engine,1,VXL_RESERVE,&r)==0);args=r.handle;
 for(unsigned id=1;id<=10;++id) {r=request();r.handle=code;r.handle2=args;r.offset2=64;r.trust_id=id;r.deadline_ms=60000;assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EACCES);}
 r=request();r.handle=code;r.handle2=args;r.offset2=64;r.trust_id=12;r.deadline_ms=60000;assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EACCES);
 assert(!prepares);r=request();r.handle=code;r.handle2=args;r.offset2=64;r.trust_id=11;r.deadline_ms=59999;
 assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EINVAL && !prepares);
 r.deadline_ms=60000;
 assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EAGAIN && prepares==1);
 r=request();r.domain=VXL_PS;r.purpose=VXL_DATA;r.address=0x10002000;r.length=64;
 assert(vxl_engine_call(&m.engine,1,VXL_RESERVE,&r)<0);
 r=request();r.domain=VXL_PL;r.purpose=VXL_DATA;r.address=0x81000040;r.length=64;
 assert(vxl_engine_call(&m.engine,1,VXL_RESERVE,&r)<0);
 vxl_engine_close(&m.engine,1);assert(m.allocations==0);
 assert(!vxl_engine_init_profile(&m.engine,&ops,&m,1,6));
 assert(vxl_engine_open(&m.engine,1)==-ENODEV);
 assert(vxl_engine_init_profile(&m.engine,&ops,&m,1,7)<0);
 puts("S8_PROFILE_MATRIX_PASS legacy_report=0,4 new=5 only_program=11");return 0;
}

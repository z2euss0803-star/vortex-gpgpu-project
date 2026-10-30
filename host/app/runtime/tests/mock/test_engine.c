#include <vxl/mock.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct vxl_request req(void) {struct vxl_request r={0};r.abi_major=1;r.struct_size=sizeof(r);return r;}
static vxl_u64 alloc(struct vxl_mock *m,vxl_u32 domain,vxl_u64 n,vxl_u32 purpose) {
 struct vxl_request r=req();r.domain=domain;r.length=n;r.purpose=purpose;r.alignment=64;
 assert(!vxl_engine_call(&m->engine,1,VXL_ALLOC,&r));return r.handle;
}
static int copy(struct vxl_mock *m,vxl_u32 cmd,vxl_u64 h,vxl_u64 off,void *p,vxl_u64 n) {
 struct vxl_request r=req();r.handle=h;r.offset=off;r.user_ptr=(uintptr_t)p;r.length=n;
 return vxl_engine_call(&m->engine,1,cmd,&r);
}
int main(void) {
 struct vxl_mock m; struct vxl_request r; unsigned i,k,t; vxl_u64 h,code,args;
 unsigned sizes[]={1,15,16,17,63,64,65,255,256,4095,4096,4097};
 unsigned offsets[]={1,15,16,63}; unsigned char data[8192],out[8192],initial[8192];
 vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1)); assert(vxl_engine_open(&m.engine,2)==-EBUSY);
 for(i=0;i<8192;i++){data[i]=(unsigned char)(i*37+13);initial[i]=0xa5;}
 h=alloc(&m,VXL_PL,8192,VXL_DATA);
 {vxl_u32 addr[3];struct vxl_object *o=0;
  for(i=0;i<VXL_MAX_OBJECTS;i++)if(m.engine.objects[i].handle==h)o=&m.engine.objects[i];
  assert(o);addr[0]=addr[1]=addr[2]=o->gpu+8192-256;
  assert(!vxl_diagnostic_args(&m.engine,addr));addr[2]+=4;
  assert(vxl_diagnostic_args(&m.engine,addr)==-ERANGE);
 }
 for(i=0;i<12;i++)for(k=0;k<4;k++) {
  assert(!copy(&m,VXL_COPY_TO,h,0,initial,8192));
  assert(!copy(&m,VXL_COPY_TO,h,offsets[k],data,sizes[i]));
  assert(!copy(&m,VXL_COPY_FROM,h,0,out,8192));
  for(t=0;t<8192;t++)assert(out[t]==(t>=offsets[k]&&t<offsets[k]+sizes[i]?data[t-offsets[k]]:0xa5));
 }
 r=req();r.handle=h;r.handle2=h;r.offset=1;r.offset2=0;r.length=32;
 assert(vxl_engine_call(&m.engine,1,VXL_COPY_DEVICE,&r)==-EINVAL);
 assert(copy(&m,VXL_COPY_TO,h,8191,data,2)==-EINVAL);
 r=req();r.handle=h;assert(vxl_engine_call(&m.engine,2,VXL_FREE,&r)==-EBADF);
 m.fault=MOCK_READ_USER;assert(copy(&m,VXL_COPY_TO,h,0,data,16)==-EFAULT);m.fault=MOCK_WRITE_USER;
 assert(copy(&m,VXL_COPY_FROM,h,0,out,16)==-EFAULT);m.fault=MOCK_OK;
 code=alloc(&m,VXL_PS,64,VXL_CODE);args=alloc(&m,VXL_PS,64,VXL_ARGS);
 r=req();assert(!vxl_engine_call(&m.engine,1,VXL_GET_INFO,&r));assert(r.capacity==VXL_PL_SIZE&&r.used==8192);
 r=req();r.handle=code;r.handle2=args;r.deadline_ms=100;r.trust_id=0;
 assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EACCES);r.trust_id=1;
 assert(!vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r));
 assert(copy(&m,VXL_COPY_TO,h,0,data,16)==-EBUSY);
 r=req();r.handle=h;assert(vxl_engine_call(&m.engine,1,VXL_FREE,&r)==-EBUSY);
 r=req();r.job_id=1;r.wait_ms=0;assert(vxl_engine_call(&m.engine,1,VXL_WAIT,&r)==-ETIMEDOUT);
 vxl_mock_advance(&m,20);r=req();r.job_id=1;assert(!vxl_engine_call(&m.engine,1,VXL_WAIT,&r));
 r=req();r.job_id=2;assert(vxl_engine_call(&m.engine,1,VXL_WAIT,&r)==-ESTALE);
 r=req();r.job_id=1;assert(!vxl_engine_call(&m.engine,1,VXL_REAP,&r));
 /* 100 sequential jobs, explicit reap; second submit cannot overwrite history. */
 for(i=0;i<100;i++) {
  r=req();r.handle=code;r.handle2=args;r.deadline_ms=100;r.trust_id=1;
  assert(!vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r));vxl_mock_advance(&m,20);
  r=req();r.job_id=m.engine.job.id;assert(!vxl_engine_call(&m.engine,1,VXL_WAIT,&r));
  assert(!vxl_engine_call(&m.engine,1,VXL_WAIT,&r));assert(!vxl_engine_call(&m.engine,1,VXL_REAP,&r));
 }
 vxl_engine_close(&m.engine,1);assert(!m.allocations);
 assert(!vxl_engine_open(&m.engine,1));r=req();r.handle=h;assert(vxl_engine_call(&m.engine,1,VXL_FREE,&r)==-EBADF);
 vxl_engine_close(&m.engine,1);
 for(i=0;i<10;i++){assert(!vxl_engine_open(&m.engine,1));alloc(&m,VXL_PL,64,VXL_DATA);vxl_engine_close(&m.engine,1);assert(!m.allocations);}
 /* Hardware/model failure must pin buffers, close must not free them. */
 for(i=MOCK_NO_DONE;i<=MOCK_BAD_ACK;i++) {
  vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1));code=alloc(&m,VXL_PS,64,VXL_CODE);args=alloc(&m,VXL_PS,64,VXL_ARGS);
  m.fault=(int)i;r=req();r.handle=code;r.handle2=args;r.trust_id=1;r.deadline_ms=30;
  assert(!vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r));vxl_engine_close(&m.engine,1);
  assert(m.allocations==2);vxl_mock_advance(&m,40);assert(m.engine.quarantined&&m.engine.job.pinned&&m.allocations==2);
  assert(vxl_engine_open(&m.engine,2)==-EIO);
  /* Test-only dismantling of synthetic memory, never a hardware recovery. */
  for(k=0;k<VXL_MAX_OBJECTS;k++)if(m.engine.objects[k].storage)free(m.engine.objects[k].storage);
 }
 for(i=1;i<=3;i++) {vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1));m.fail_alloc=(int)i;
  for(k=1;k<=3;k++){r=req();r.domain=VXL_PL;r.purpose=VXL_DATA;r.length=64;r.alignment=64;
   assert(vxl_engine_call(&m.engine,1,VXL_ALLOC,&r)==(k==i?-ENOMEM:0));}
  vxl_engine_close(&m.engine,1);assert(!m.allocations);
 }
 /* Transfer fault/timeout must retain allocations even after release. */
 for(i=0;i<2;i++){
  vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1));h=alloc(&m,VXL_PL,64,VXL_DATA);
  m.fault=i?MOCK_COPY_TIMEOUT:MOCK_COPY_ERROR;
  assert(copy(&m,VXL_COPY_TO,h,1,data,17)==(i?-ETIMEDOUT:-EIO));
  assert(m.engine.quarantined&&m.engine.job.pinned);vxl_engine_close(&m.engine,1);
  assert(m.allocations==1&&vxl_engine_open(&m.engine,2)==-EIO);
  for(k=0;k<VXL_MAX_OBJECTS;k++)if(m.engine.objects[k].storage)free(m.engine.objects[k].storage);
 }
 vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1));h=alloc(&m,VXL_PL,64,VXL_DATA);
 assert(copy(&m,VXL_COPY_TO,h,0,0,16)==-EFAULT);vxl_engine_close(&m.engine,1);assert(!m.allocations);
 puts("MOCK_COPY_48_GUARD_CASES_100_JOBS_10_CLOSE_FAILURES_PASS");return 0;
}

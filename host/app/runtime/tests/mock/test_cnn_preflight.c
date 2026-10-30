#include "../support/strict_backend.h"
#include <vxl/cnn.h>
#include <vxl/mock.h>
#include <assert.h>
#include <stdio.h>
#include "../../generated/cnn/programs.h"
#include "../../vendor/h0/vecadd16.h"
static struct vxl_request req(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=sizeof(r);return r;}
static struct vxl_object *allocate(struct strict_backend *s,unsigned dom,unsigned purpose,unsigned n,unsigned address){
 struct vxl_request r=req();unsigned i;r.domain=dom;r.purpose=purpose;r.length=n;r.alignment=64;r.address=address;
 assert(!vxl_engine_call(&s->engine,1,address?VXL_RESERVE:VXL_ALLOC,&r));
 for(i=0;i<VXL_MAX_OBJECTS;i++)if(s->engine.objects[i].handle==r.handle)return &s->engine.objects[i];
 assert(0);return 0;
}
struct setup {struct strict_backend s;struct vxl_object *code,*args,*data[9];vxl_u32 words[16];};
static void setup(struct setup *t){
 unsigned i;static const unsigned count[]={64,18,2,72,72,18,36,2,2};
 strict_init(&t->s);assert(!vxl_engine_open(&t->s.engine,1));
 t->code=allocate(&t->s,VXL_PS,VXL_CODE,4096,0x10000000);t->args=allocate(&t->s,VXL_PS,VXL_ARGS,64,0x10010000);
 for(i=0;i<9;i++)t->data[i]=allocate(&t->s,VXL_PL,VXL_DATA,128+((count[i]*4+63)&~63U),0);
 memcpy(t->code->storage,vxl_cnn_program_2,4096);
 memset(t->words,0,64);t->words[0]=VXL_CNN_MAGIC;t->words[1]=64;t->words[2]=1;t->words[3]=2;
 for(i=0;i<4;i++){t->words[4+i]=t->data[i]->gpu+64;t->words[8+i]=count[i];}
 memcpy(t->args->storage,t->words,64);
}
static struct vxl_request submit(struct setup *t,unsigned id){struct vxl_request r=req();r.handle=t->code->handle;r.handle2=t->args->handle;r.trust_id=id;r.deadline_ms=10000;return r;}
static void rejected(struct setup *t,struct vxl_request *r,int expected){
 unsigned transfers=t->s.transfers;vxl_u64 next=t->s.engine.next_job;
 {int actual=vxl_engine_call(&t->s.engine,1,VXL_SUBMIT,r);if(actual!=expected)fprintf(stderr,"REJECT id=%u offset=%llu args_offset=%llu actual=%d expected=%d\n",r->trust_id,(unsigned long long)r->offset,(unsigned long long)r->offset2,actual,expected);assert(actual==expected);}
 assert(t->s.engine.job.state==VXL_READY&&!t->s.engine.quarantined&&!t->s.engine.job.pinned);
 assert(!t->s.starts&&t->s.engine.next_job==next&&t->s.transfers==transfers);
}
int main(void){
 struct setup t;struct vxl_request r;unsigned i,cmd;vxl_u64 id;
 setup(&t);
 /* Current engine starts/pins before validation: this assertion must fail RED. */
 t.words[0]=0;memcpy(t.args->storage,t.words,64);r=submit(&t,2);rejected(&t,&r,-EINVAL);
 t.words[0]=VXL_CNN_MAGIC;memcpy(t.args->storage,t.words,64);
 for(i=0;i<16;i++)if(i<4||i>=8){
  t.words[i]^=1;memcpy(t.args->storage,t.words,64);r=submit(&t,2);rejected(&t,&r,-EINVAL);
  t.words[i]^=1;memcpy(t.args->storage,t.words,64);
 }
 t.words[4]+=4;memcpy(t.args->storage,t.words,64);r=submit(&t,2);rejected(&t,&r,-EINVAL);
 t.words[4]-=4;memcpy(t.args->storage,t.words,64);
 t.data[0]->length-=64;r=submit(&t,2);rejected(&t,&r,-ERANGE);t.data[0]->length+=64;
 t.args->domain=VXL_PL;r=submit(&t,2);rejected(&t,&r,-EINVAL);t.args->domain=VXL_PS;
 t.args->length=48;r=submit(&t,2);rejected(&t,&r,-EINVAL);t.args->length=64;
 t.code->length=4095;r=submit(&t,2);rejected(&t,&r,-EACCES);t.code->length=4096;
 r=submit(&t,77);rejected(&t,&r,-EACCES);
 ((vxl_u8*)t.code->storage)[0]^=1;r=submit(&t,2);rejected(&t,&r,-EACCES);((vxl_u8*)t.code->storage)[0]^=1;
 r=submit(&t,2);r.offset=4;rejected(&t,&r,-EINVAL);
 r=submit(&t,2);r.offset2=4;rejected(&t,&r,-EINVAL);
 t.args->gpu+=64;r=submit(&t,2);rejected(&t,&r,-EINVAL);t.args->gpu-=64;
 t.code->domain=VXL_PL;r=submit(&t,2);rejected(&t,&r,-EACCES);t.code->domain=VXL_PS;
 t.words[7]=t.words[4];memcpy(t.args->storage,t.words,64);r=submit(&t,2);rejected(&t,&r,-ERANGE);
 t.words[7]=t.data[3]->gpu+64;memcpy(t.args->storage,t.words,64);
 r=submit(&t,2);r.handle2+=100;rejected(&t,&r,-EBADF);
 r=submit(&t,2);assert(!vxl_engine_call(&t.s.engine,1,VXL_SUBMIT,&r));id=r.job_id;assert(id==1&&t.s.starts==1);
 for(i=0;i<11;i++){
  vxl_u64 h=i==0?t.code->handle:i==1?t.args->handle:t.data[i-2]->handle;
  for(cmd=0;cmd<3;cmd++){r=req();r.handle=h;r.length=16;r.user_ptr=(uintptr_t)t.words;
   assert(vxl_engine_call(&t.s.engine,1,cmd==0?VXL_FREE:cmd==1?VXL_COPY_TO:VXL_COPY_FROM,&r)==-EBUSY);}
 }
 r=submit(&t,2);assert(vxl_engine_call(&t.s.engine,1,VXL_SUBMIT,&r)==-EBUSY);
 strict_complete(&t.s);r=req();assert(!vxl_engine_call(&t.s.engine,1,VXL_GET_STATUS,&r));assert(r.state==VXL_COMPLETED&&r.hwstatus==2&&r.jobs==1);
 r=req();r.job_id=id;assert(!vxl_engine_call(&t.s.engine,1,VXL_REAP,&r));assert(t.s.engine.job.state==VXL_READY);
 r=submit(&t,2);assert(!vxl_engine_call(&t.s.engine,1,VXL_SUBMIT,&r));vxl_engine_close(&t.s.engine,1);assert(t.s.allocations==11&&t.s.engine.job.pinned);strict_dispose(&t.s);
 /* ID1 production {a,b,c} and PL read fault; no START on transport error. */
 for(i=0;i<3;i++){
  vxl_u32 addr[3];setup(&t);memset(t.code->storage,0,4096);memcpy(t.code->storage,vecadd16_code,sizeof(vecadd16_code));
  for(cmd=0;cmd<3;cmd++){struct vxl_object *o=allocate(&t.s,VXL_PL,VXL_DATA,256,0);addr[cmd]=o->gpu;}
  t.args=allocate(&t.s,VXL_PL,VXL_ARGS,64,0);memcpy(t.args->storage,addr,12);t.s.transfer_error=i==1?-EIO:i==2?-ETIMEDOUT:0;
  r=submit(&t,1);assert(vxl_engine_call(&t.s.engine,1,VXL_SUBMIT,&r)==t.s.transfer_error);
  if(i){assert(!t.s.starts&&t.s.engine.quarantined&&t.s.engine.job.pinned&&t.s.engine.next_job==0);vxl_engine_close(&t.s.engine,1);assert(t.s.allocations==15);}
  else {assert(t.s.starts==1);strict_complete(&t.s);vxl_engine_close(&t.s.engine,1);assert(!t.s.allocations);}
  strict_dispose(&t.s);
 }
 setup(&t);memset(t.code->storage,0,4096);memcpy(t.code->storage,vecadd16_code,sizeof(vecadd16_code));
 {vxl_u32 synthetic[4]={64,t.data[0]->gpu+64,t.data[3]->gpu+64,t.data[4]->gpu+64};memcpy(t.args->storage,synthetic,16);r=submit(&t,1);rejected(&t,&r,-ERANGE);}
 strict_dispose(&t.s);
 {struct vxl_mock m;struct vxl_request a=req(),b=req();vxl_u32 production[3]={0x80000000,0x80001000,0x80002000};
  vxl_mock_init(&m);assert(!vxl_engine_open(&m.engine,1));a.domain=b.domain=VXL_PS;a.purpose=VXL_CODE;b.purpose=VXL_ARGS;a.length=b.length=64;a.alignment=b.alignment=64;
  assert(!vxl_engine_call(&m.engine,1,VXL_ALLOC,&a));assert(!vxl_engine_call(&m.engine,1,VXL_ALLOC,&b));
  r=req();r.handle=b.handle;r.user_ptr=(uintptr_t)production;r.length=12;assert(!vxl_engine_call(&m.engine,1,VXL_COPY_TO,&r));
  r=req();r.handle=a.handle;r.handle2=b.handle;r.trust_id=1;r.deadline_ms=100;
  assert(vxl_engine_call(&m.engine,1,VXL_SUBMIT,&r)==-EINVAL&&m.starts==0&&m.engine.next_job==0);
  vxl_engine_close(&m.engine,1);assert(!m.allocations);
 }
 puts("TEST_PROTOCOL_NOT_GPU_PREFLIGHT_LIFECYCLE_PASS");return 0;
}

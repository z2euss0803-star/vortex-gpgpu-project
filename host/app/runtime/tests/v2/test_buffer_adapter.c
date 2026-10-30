#include <vxl/v2/objects.h>
#include <vxl/engine.h>
#include <assert.h>
#include <stdio.h>
struct mock {int locked,allocs,copies,frees;unsigned char bytes[256];};
static int held(void *p){return ((struct mock *)p)->locked;}
static int alloc(void *p,struct vxl_object *o){struct mock *m=p;m->allocs++;o->storage=m->bytes;return 0;}
static void destroy(void *p,struct vxl_object *o){(void)o;((struct mock *)p)->frees++;}
static int transfer(void *p,struct vxl_object *o,vxl_u64 off,void *b,vxl_u64 n,int w){struct mock *m=p;(void)o;assert(!(off&15) && !(n&15) && off+n<=256);m->copies++;if(w)memcpy(m->bytes+off,b,n);else memcpy(b,m->bytes+off,n);return 0;}
int main(void){
 struct mock m={0};struct vxl_ops io={.alloc=alloc,.free=destroy,.transfer=transfer};struct vxl_engine old={0};
 struct vxl_v2_adapter a={&old,&io,&m,held,0};struct vxl_v2_ctx c;struct vxl_v2_ref r,part;unsigned char input[3]={7,8,9},out[3]={0};
 vxl_v2_init(&c,&vxl_v2_adapter_memory_ops,&a);old.owner=11;old.opened=1;old.job.state=VXL_READY;
 assert(vxl_v2_adapter_enter(&a,&c,11,1)==-EDEADLK);m.locked=1;
 old.objects[0].handle=1;assert(vxl_v2_adapter_enter(&a,&c,11,1)==-EBUSY);old.objects[0].handle=0;
 a.legacy_debt=1;assert(vxl_v2_adapter_enter(&a,&c,11,1)==-EBUSY);a.legacy_debt=0;
 old.owner=12;assert(vxl_v2_adapter_enter(&a,&c,11,1)==-EPERM);old.owner=11;
 assert(vxl_v2_adapter_enter(&a,&c,11,1)==0);assert(vxl_v2_buffer_create(&c,11,1,2,3,3,64,&r)==0);assert(m.allocs==1);
 part=r;part.offset=5;part.length=3;assert(vxl_v2_buffer_copy(&c,11,&part,input,1)==0);assert(m.bytes[4]==0 && m.bytes[5]==7 && m.bytes[7]==9 && m.bytes[8]==0);
 assert(vxl_v2_buffer_copy(&c,11,&part,out,0)==0);assert(!memcmp(input,out,3));
 assert(vxl_v2_buffer_seal(&c,11,&r)==0);assert(vxl_v2_buffer_copy(&c,11,&part,input,1)==-EACCES);
 assert(vxl_v2_owner_close(&c,11,1)==0 && m.frees==1);
 puts("V2_SHARED_LOCK_LEGACY_GATE_REAL_BYTE_ADAPTER_PASS_HOST_PROTOCOL_NOT_DMA");return 0;
}

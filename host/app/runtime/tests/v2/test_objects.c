#include <vxl/v2/objects.h>
#include <assert.h>
#include <stdio.h>
struct mock {unsigned allocs,frees,live,fail_at,copies;};
static int allocate(void *p,struct vxl_v2_object *o){struct mock *m=p;(void)o;m->allocs++;if(m->fail_at==m->allocs)return -ENOMEM;m->live++;return 0;}
static int destroy(void *p,struct vxl_v2_object *o){struct mock *m=p;(void)o;m->live--;m->frees++;return 0;}
static int copy(void *p,struct vxl_v2_object *o,vxl_u64 off,void *b,vxl_u64 n,int w){struct mock *m=p;(void)o;(void)off;(void)b;(void)n;(void)w;m->copies++;return 0;}
static const struct vxl_v2_memory_ops ops={allocate,destroy,copy};
int main(void){
 struct vxl_v2_ctx c;struct mock m={0};struct vxl_v2_ref a[64],bad;unsigned i;
 vxl_v2_init(&c,&ops,&m);assert(vxl_v2_owner_enter(&c,11,1)==0);
 assert(vxl_v2_owner_enter(&c,22,1)==-EBUSY);assert(vxl_v2_owner_check(&c,22,1)==-EPERM);assert(vxl_v2_owner_check(&c,11,2)==-ESTALE);
 for(i=0;i<64;i++)assert(vxl_v2_buffer_create(&c,11,1,2,3,3,64,&a[i])==0);
 assert(c.live==64 && m.live==64);assert(vxl_v2_buffer_create(&c,11,1,2,3,3,64,&bad)==-ENOSPC);
 for(i=0;i<5;i++){assert(vxl_v2_buffer_pin(&c,11,&a[0],i)==0);assert(vxl_v2_buffer_release(&c,11,&a[0])==-EBUSY);assert(vxl_v2_buffer_unpin(&c,11,&a[0],i)==0);assert(vxl_v2_buffer_unpin(&c,11,&a[0],i)==-EINVAL);}
 bad=a[0];bad.object_generation++;assert(vxl_v2_buffer_release(&c,11,&bad)==-ESTALE);
 assert(vxl_v2_buffer_release(&c,22,&a[0])==-EPERM);
 bad=a[0];bad.domain=0;assert(vxl_v2_buffer_copy(&c,11,&bad,&m,1)==-EINVAL);
 bad=a[0];bad.offset=~(vxl_u64)0;bad.length=2;assert(vxl_v2_buffer_copy(&c,11,&bad,&m,0)==-EOVERFLOW);
 assert(vxl_v2_buffer_seal(&c,11,&a[0])==0);assert(vxl_v2_buffer_copy(&c,11,&a[0],&m,1)==-EACCES);assert(m.copies==0);
 assert(vxl_v2_owner_close(&c,11,1)==0);assert(c.live==0 && m.live==0 && m.frees==64);
 assert(vxl_v2_owner_enter(&c,22,2)==0);assert(vxl_v2_buffer_release(&c,22,&a[0])==-ESTALE);assert(vxl_v2_owner_close(&c,22,1)==0);
 for(i=1;i<=35;i++){
  struct mock f={.fail_at=i};vxl_v2_init(&c,&ops,&f);assert(vxl_v2_owner_enter(&c,11,1)==0);
  memset(a,0,sizeof(a));assert(vxl_v2_buffer_batch(&c,11,1,2,4194304,35,a)==-ENOMEM);
  assert(c.live==0 && f.live==0 && f.frees==i-1);assert(a[0].handle==0);
 }
 m=(struct mock){0};vxl_v2_init(&c,&ops,&m);c.legacy_debt=1;assert(vxl_v2_owner_enter(&c,1,1)==-EBUSY);c.legacy_debt=0;
 c.epoch=~(vxl_u64)0;assert(vxl_v2_owner_enter(&c,1,0)==-EOVERFLOW);
 vxl_v2_init(&c,&ops,&m);assert(vxl_v2_owner_enter(&c,1,1)==0);assert(vxl_v2_buffer_create(&c,1,1,1,3,3,64,&a[0])==0);
 {struct vxl_v2_object *o=0;assert(vxl_v2_ref_resolve(&c,1,&a[0],1,&o)==0);assert(o && o->bus>=VXL_PS_BASE && o->gpu>=VXL_PS_BASE);}
 assert(vxl_v2_owner_close(&c,1,0)==-EIO);assert(c.quarantined && c.live==1 && m.live==1);assert(vxl_v2_buffer_release(&c,1,&a[0])==-EIO);
 puts("V2_OWNER_OBJECT_LIFETIME_AND_35_ATOMIC_ROLLBACKS_PASS_NOT_BOARD");return 0;
}

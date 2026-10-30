#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/submit_timing.h>
#include <assert.h>
#include <stdio.h>
struct fake {vxl_u64 now;unsigned calls,fail_at,corrupt_at;};
static int io_bytes(void *p,vxl_u64 a,void *b,vxl_u64 n,int write){
 struct fake *f=p;assert(n&&n<=64);f->calls++;f->now+=10;
 if(f->calls==f->fail_at)return -EIO;
 if(!write)memset(b,f->calls==f->corrupt_at?0:0xa5,(size_t)n);
 (void)a;return 0;
}
int vxl_hunet_io_bytes(struct vxl_hunet_ctx *c,vxl_u64 a,void *b,vxl_u64 n,int w){return c->io.rw(c->io.context,a,b,n,w);}
static vxl_u64 now_ns(void *p){return ((struct fake *)p)->now;}
static void reset(struct fake *f,struct vxl_hunet_ctx *c,struct vxl_engine *e,struct vxl_submit_time *t){
 memset(f,0,sizeof(*f));memset(c,0,sizeof(*c));memset(e,0,sizeof(*e));memset(t,0,sizeof(*t));f->now=1000000;
 c->engine=e;c->io=(struct vxl_hunet_io){f,io_bytes,now_ns};c->submit_time=t;e->generation=2;
}
int main(void){
 struct fake f;struct vxl_hunet_ctx c;struct vxl_engine e;struct vxl_submit_time t;
 struct vxl_hunet_hw_snapshot s;
 reset(&f,&c,&e,&t);assert(!vxl_hunet_before_start(&c,1,0));assert(f.calls==116);assert(t.guard_init_ns==1120&&!t.guard_depth&&!t.flags);
 memset(&s,0,sizeof(s));assert(!vxl_hunet_crt_snapshot(&c,&s));assert(f.calls==232);assert(t.guard_check_ns==1120&&s.crt_guard_flags==3&&!t.guard_depth);
 reset(&f,&c,&e,&t);f.fail_at=8;assert(vxl_hunet_before_start(&c,1,0)==-EIO);assert(f.calls==8&&t.guard_init_ns==80&&!t.guard_depth);
 reset(&f,&c,&e,&t);f.corrupt_at=5;memset(&s,0,sizeof(s));assert(vxl_hunet_crt_snapshot(&c,&s)==-EIO);assert(f.calls==5&&t.guard_check_ns==10&&!s.crt_guard_flags&&!t.guard_depth);
 reset(&f,&c,&e,&t);c.submit_time=NULL;assert(!vxl_hunet_before_start(&c,1,0));assert(f.calls==116&&t.guard_init_ns==0);
 t.guard_init_ns=~(vxl_u64)0-2;vxl_time_add(&t,&t.guard_init_ns,10,15);assert(t.guard_init_ns==~(vxl_u64)0&&(t.flags&VXL_TIME_OVERFLOW));
 t.flags=0;vxl_time_add(&t,&t.guard_check_ns,20,19);assert(t.flags&VXL_TIME_BACKWARDS);
 {struct vxl_submit_trace tr={0};struct vxl_submit_time rows[1];tr.rows=rows;tr.capacity=1;assert(vxl_time_begin(&tr,2,100));vxl_time_finish(&tr,200,0);assert(tr.used==1&&!tr.active);assert(!vxl_time_begin(&tr,3,300)&&(tr.flags&VXL_TIME_BUFFER_FULL));}
 puts("TIMING_REAL_GUARD_SUCCESS_FAILURE_AND_BOUNDS_PASS_NOT_BOARD");return 0;
}

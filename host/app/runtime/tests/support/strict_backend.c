/* Test protocol backend; never GPU execution evidence. */
#include "strict_backend.h"
#include <stdlib.h>
static int alloc(void *c,struct vxl_object *o){struct strict_backend *s=c;o->storage=calloc(1,(size_t)o->allocated);if(!o->storage)return -ENOMEM;s->allocations++;return 0;}
static void release(void *c,struct vxl_object *o){struct strict_backend *s=c;free(o->storage);s->allocations--;}
static int transfer(void *c,struct vxl_object *o,vxl_u64 off,void *p,vxl_u64 n,int write){
 struct strict_backend *s=c;s->transfers++;if(s->transfer_error)return s->transfer_error;
 if(vxl_range_check(o->allocated,off,n))return -ERANGE;
 if(write)memcpy((vxl_u8*)o->storage+off,p,(size_t)n);else memcpy(p,(vxl_u8*)o->storage+off,(size_t)n);return 0;
}
static int prepare(void *c,struct vxl_engine *e,struct vxl_object *code,vxl_u64 co,struct vxl_object *args,vxl_u64 ao,vxl_u32 id,struct vxl_prepared *p){
 (void)c;return vxl_controlled_prepare(e,code,co,args,ao,id,p);
}
static int start(void *c,const struct vxl_prepared *p,vxl_u32 *jobs){
 struct strict_backend *s=c;(void)p;*jobs=s->jobs;s->starts++;s->completed=0;return 0;
}
static int poll(void *c,struct vxl_event *ev){struct strict_backend *s=c;if(!s->completed)return 0;ev->status=2;ev->jobs=s->jobs+1;ev->idle=ev->ready=1;if(s->completion_error)ev->errors=1;return 1;}
static int read_user(void *c,void *dst,vxl_u64 src,vxl_u64 n){(void)c;if(!src)return -EFAULT;memcpy(dst,(void *)(uintptr_t)src,(size_t)n);return 0;}
static int write_user(void *c,vxl_u64 dst,const void *src,vxl_u64 n){(void)c;if(!dst)return -EFAULT;memcpy((void *)(uintptr_t)dst,src,(size_t)n);return 0;}
static vxl_u64 now(void *c){return ((struct strict_backend *)c)->clock;}
static const struct vxl_ops ops={alloc,release,transfer,prepare,start,poll,read_user,write_user,now};
void strict_init(struct strict_backend *s){memset(s,0,sizeof(*s));vxl_engine_init(&s->engine,&ops,s,1);}
void strict_complete(struct strict_backend *s){s->clock+=10;s->completed=1;vxl_engine_poll(&s->engine);if(s->engine.job.state==VXL_COMPLETED)s->jobs++;}
void strict_dispose(struct strict_backend *s){unsigned i;for(i=0;i<VXL_MAX_OBJECTS;i++){free(s->engine.objects[i].storage);s->engine.objects[i].storage=0;}s->allocations=0;}

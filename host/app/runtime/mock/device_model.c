/* Synthetic CPU/memory protocol model; does NOT execute RISC-V/GPU instructions. */
#include <vxl/mock.h>
#include <stdlib.h>
static int alloc_mem(void *c,struct vxl_object *o){struct vxl_mock *m=c;if(++m->alloc_calls==m->fail_alloc)return -ENOMEM;o->storage=calloc(1,(size_t)o->allocated);if(!o->storage)return -ENOMEM;m->allocations++;return 0;}
static void free_mem(void *c,struct vxl_object *o){struct vxl_mock *m=c;free(o->storage);m->allocations--;}
static int transfer(void *c,struct vxl_object *o,vxl_u64 off,void *buf,vxl_u64 n,int write){
 struct vxl_mock *m=c;int rc=vxl_cdma_addresses(o->bus+off,0x100f0000,n);if(rc)return rc;
 if(vxl_range_check(o->allocated,off,n))return -EINVAL;
 m->transfers++;if(m->fault==MOCK_COPY_ERROR)return -EIO;if(m->fault==MOCK_COPY_TIMEOUT)return -ETIMEDOUT;
 if(write)memcpy((vxl_u8 *)o->storage+off,buf,(size_t)n);else memcpy(buf,(vxl_u8 *)o->storage+off,(size_t)n);return 0;
}
static int read_user(void *c,void *dst,vxl_u64 src,vxl_u64 n){struct vxl_mock *m=c;if(!src||m->fault==MOCK_READ_USER)return -EFAULT;memcpy(dst,(void *)(uintptr_t)src,(size_t)n);return 0;}
static int write_user(void *c,vxl_u64 dst,const void *src,vxl_u64 n){struct vxl_mock *m=c;if(!dst||m->fault==MOCK_WRITE_USER)return -EFAULT;memcpy((void *)(uintptr_t)dst,src,(size_t)n);return 0;}
static struct vxl_object *by_addr(struct vxl_mock *m,vxl_u32 a,vxl_u64 n){unsigned i;for(i=0;i<VXL_MAX_OBJECTS;i++){struct vxl_object *o=&m->engine.objects[i];if(o->handle&&a>=o->gpu&&!vxl_range_check(o->length,a-o->gpu,n))return o;}return 0;}
static int prepare(void *c,struct vxl_engine *e,struct vxl_object *code,vxl_u64 co,struct vxl_object *arg,vxl_u64 ao,vxl_u32 id,struct vxl_prepared *p){
 struct vxl_mock *m=c;(void)e;(void)code;(void)co;
 if(id!=1)return -EACCES; /* Old synthetic model never executes production CNN. */
 if(m->fault==MOCK_BAD_ID)return -ENODEV;
 if(m->fault==MOCK_NOT_READY)return -EAGAIN;
 memset(p,0,sizeof(*p));p->program_id=1;p->args_word_count=4;
 if(vxl_range_check(arg->length,ao,16))return -EINVAL;
 memcpy(p->args_words,(vxl_u8 *)arg->storage+ao,16);
 if(p->args_words[0]>65536)return -EINVAL;
 if(p->args_words[0]&&(!by_addr(m,p->args_words[1],p->args_words[0]*4ULL)||!by_addr(m,p->args_words[2],p->args_words[0]*4ULL)||!by_addr(m,p->args_words[3],p->args_words[0]*4ULL)))return -ERANGE;
 return 0;
}
static int start(void *c,const struct vxl_prepared *p,vxl_u32 *jobs){
 struct vxl_mock *m=c;const vxl_u32 *words=p->args_words;struct vxl_object *a,*b,*out;unsigned i;
 *jobs=m->jobs;m->starts++;m->finish=m->clock+10;
 /* Explicit synthetic vecadd ABI = {count, a, b, c}. Small valid tests only. */
 if(!words[0]||words[0]>65536)return 0;
 a=by_addr(m,words[1],words[0]*4ULL);b=by_addr(m,words[2],words[0]*4ULL);out=by_addr(m,words[3],words[0]*4ULL);
 if(!a||!b||!out)return -EINVAL;
 for(i=0;i<words[0];i++){vxl_u32 av,bv,sum;memcpy(&av,(vxl_u8 *)a->storage+words[1]-a->gpu+i*4,4);memcpy(&bv,(vxl_u8 *)b->storage+words[2]-b->gpu+i*4,4);sum=av+bv;memcpy((vxl_u8 *)out->storage+words[3]-out->gpu+i*4,&sum,4);}
 return 0;
}
static int poll(void *c,struct vxl_event *e){
 struct vxl_mock *m=c;if(m->clock<m->finish||m->fault==MOCK_NO_DONE)return 0;
 e->status=2;e->ready=1;e->idle=1;e->jobs=m->jobs+1;
 if(m->fault==MOCK_ERROR){e->status|=4;e->errors=1;}
 if(m->fault==MOCK_PENDING)e->pending_w=1;
 if(m->fault==MOCK_STALE_JOBS)e->jobs=m->jobs;
 if(m->fault==MOCK_BAD_ACK)e->idle=0;
 if(m->fault==MOCK_OK)m->jobs++;
 return 1;
}
static vxl_u64 now(void *c){return ((struct vxl_mock *)c)->clock;}
static const struct vxl_ops ops={alloc_mem,free_mem,transfer,prepare,start,poll,read_user,write_user,now};
void vxl_mock_init(struct vxl_mock *m){memset(m,0,sizeof(*m));vxl_engine_init(&m->engine,&ops,m,1);}
void vxl_mock_advance(struct vxl_mock *m,vxl_u64 ms){m->clock+=ms;vxl_engine_poll(&m->engine);}

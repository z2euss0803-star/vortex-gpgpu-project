#include "program_fixture.h"
#include <vxl/v2/jobs.h>
struct hardware {vxl_u64 now;vxl_u32 starts,acks,jobs;};
static inline int prepare(void *p,const struct vxl_v2_program *program,const struct vxl_v2_job *j,const void *args,vxl_u32 n){(void)p;(void)program;(void)j;assert(n==4 && *(const vxl_u32 *)args==1);return 0;}
static inline int start(void *p,const struct vxl_v2_program *program,const struct vxl_v2_job *j,struct vxl_v2_start_proof *s){struct hardware *h=p;(void)program;(void)j;h->starts++;s->start_ns=h->now;s->jobs_before=h->jobs++;s->watchdog=1000;s->started=1;return 0;}
static inline int ack(void *p,vxl_u32 *idle,vxl_u32 *ready){struct hardware *h=p;h->acks++;*idle=1;*ready=1;return 0;}
static inline vxl_u64 now(void *p){return ((struct hardware *)p)->now;}
static const struct vxl_v2_execution_ops execution={prepare,start,ack,now};
struct fixture {struct vxl_v2_ctx c;struct vxl_v2_program_table table;struct vxl_v2_catalog cat;unsigned char raw[2048];struct program_mem memory;struct hardware hw;struct vxl_v2_queue *q;struct vxl_v2_program_ref program;struct vxl_v2_ref args;};
static inline void setup(struct fixture *f,const char *file){struct vxl_v2_catalog_entry e;struct vxl_v2_provider p=test_provider();unsigned char code[64]={0};vxl_u32 arg=1;size_t n;
 memset(f,0,sizeof(*f));n=read_fixture(file,f->raw,sizeof(f->raw));assert(vxl_v2_catalog_open(&f->cat,f->raw,(vxl_u32)n,&p)==0);assert(vxl_v2_catalog_lookup(&f->cat,f->raw+320,&e)==0);
 vxl_v2_init(&f->c,&program_ops,&f->memory);assert(vxl_v2_table_bind(&f->c,&f->table,&f->cat)==0);assert(vxl_v2_owner_enter(&f->c,1,1)==0);assert(vxl_v2_program_load(&f->c,1,&e,code,0,&f->program)==0);
 assert(vxl_v2_buffer_create(&f->c,1,1,2,2,3,4,&f->args)==0);assert(vxl_v2_buffer_copy(&f->c,1,&f->args,&arg,1)==0);f->q=calloc(1,sizeof(*f->q));assert(f->q);f->hw.now=100;f->hw.jobs=7;assert(vxl_v2_queue_bind(&f->c,f->q,&execution,&f->hw)==0);
}
static inline struct vxl_v2_job job(struct fixture *f,vxl_u64 cookie){struct vxl_v2_job j={.major=2,.size=512,.device_generation=1,.execution_budget_ns=1000,.args_bytes=4};j.cookie=cookie;j.program_handle=f->program.handle;j.program_generation=f->program.generation;j.args_handle=f->args.handle;j.args_generation=f->args.object_generation;return j;}
static inline struct vxl_v2_snapshot snapshot(struct fixture *f,vxl_u64 sequence){struct vxl_v2_snapshot s={.status=2,.core_mask=15,.elapsed_raw=37,.watchdog=1000,.cycles_valid=1,.done_seen=1,.cdma_idle=1,.guard_ok=1,.parameters_ok=1,.source_ok=1};s.sequence=sequence;f->hw.now+=10;s.now_ns=f->hw.now;s.jobs=f->hw.jobs;return s;}
static inline void teardown_host_storage(struct fixture *f){unsigned i;for(i=0;i<64;i++)if(f->c.objects[i].handle)free(f->c.objects[i].storage);free(f->q);}

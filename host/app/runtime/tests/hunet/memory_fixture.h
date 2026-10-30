#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct fixture {struct vxl_engine e;struct vxl_hunet_ctx c;struct vxl_hunet_release r;vxl_u8 *ps,*code,*weights,*ws,*stack;int reads,fault,mutate,starts,done,output_fault;vxl_u32 hw_jobs;vxl_u64 clock_ns;};
static vxl_u8 *map(struct fixture *f,vxl_u64 a,vxl_u64 n){
 if(a>=0x10000000&&!vxl_range_check(1048576,a-0x10000000,n))return f->ps+a-0x10000000;
 if(a>=VXL_HUNET_CODE&&!vxl_range_check(8388608,a-VXL_HUNET_CODE,n))return f->code+a-VXL_HUNET_CODE;
 if(a>=VXL_HUNET_WEIGHTS&&!vxl_range_check(67108864,a-VXL_HUNET_WEIGHTS,n))return f->weights+a-VXL_HUNET_WEIGHTS;
 if(a>=VXL_HUNET_WS&&!vxl_range_check(VXL_HUNET_WS_BYTES,a-VXL_HUNET_WS,n))return f->ws+a-VXL_HUNET_WS;
 if(a>=0xfff00000&&!vxl_range_check(1048576,a-0xfff00000,n))return f->stack+a-0xfff00000;
 return 0;
}
static int fio(void *p,vxl_u64 a,void *b,vxl_u64 n,int w){vxl_u8 *s=map(p,a,n);if(!s)return -ERANGE;if(w)memcpy(s,b,n);else memcpy(b,s,n);return 0;}
static int falloc(void *p,struct vxl_object *o){o->storage=map(p,o->gpu,o->allocated);return o->storage?0:-ENOMEM;}
static void ffree(void *p,struct vxl_object *o){(void)p;(void)o;}
static int ftransfer(void *p,struct vxl_object *o,vxl_u64 off,void *b,vxl_u64 n,int w){return fio(p,(vxl_u64)o->gpu+off,b,n,w);}
static int fprepare(void *p,struct vxl_engine *e,struct vxl_object *a,vxl_u64 ao,struct vxl_object *b,vxl_u64 bo,vxl_u32 id,struct vxl_prepared *o){(void)p;(void)a;(void)ao;(void)b;(void)bo;return vxl_hunet_prepare(e->hunet,o,id);}
static int fstart(void *p,const struct vxl_prepared *o,vxl_u32 *before){struct fixture *f=p;int rc;(void)o;*before=f->hw_jobs;rc=vxl_hunet_before_start(&f->c,f->e.job.id,*before);if(rc)return rc;f->starts++;f->done=0;return 0;}
/* Inject only completion registers/CRT writes. No tensor arithmetic is modeled. */
static int fpoll(void *p,struct vxl_event *e){struct fixture *f=p;struct vxl_hunet_hw_snapshot s={0};unsigned i;int rc;if(!f->done)return 0;s.event.id=f->e.job.id;s.event.status=2;s.event.ready=1;s.event.jobs=f->hw_jobs+1;s.watchdog=4000000000u;for(i=0;i<4;i++)memset(f->ps+0x2088+i*0x100,0,4);rc=vxl_hunet_crt_snapshot(&f->c,&s);if(rc)return rc;rc=vxl_hunet_accept_done(&f->c,&s,110000000,&f->c.evidence);if(rc)return rc;rc=vxl_hunet_after_ack(&f->c,1,1,110001000);if(rc)return rc;*e=s.event;e->kind=VXL_DONE;e->idle=1;f->hw_jobs++;return 1;}
static int fread_user(void *p,void *dst,vxl_u64 src,vxl_u64 n){struct fixture *f=p;if(n==196608){f->reads++;if(f->fault)return -EFAULT;}memcpy(dst,(void *)(uintptr_t)src,n);if(f->mutate&&n==196608)((vxl_u8 *)(uintptr_t)src)[0]^=1;return 0;}
static int fwrite_user(void *p,vxl_u64 dst,const void *src,vxl_u64 n){if(((struct fixture *)p)->output_fault)return -EFAULT;memcpy((void *)(uintptr_t)dst,src,n);return 0;}
static vxl_u64 fnow(void *p){(void)p;return 100;}
static vxl_u64 fns(void *p){struct fixture *f=p;return f->clock_ns?f->clock_ns:100000000;}
static const struct vxl_ops fops={falloc,ffree,ftransfer,fprepare,fstart,fpoll,fread_user,fwrite_user,fnow};
static void file_read(const char *p,void *dst,size_t n){FILE *f=fopen(p,"rb");assert(f);assert(fread(dst,1,n,f)==n);assert(fgetc(f)==EOF);fclose(f);}
static void init_fixture(struct fixture *f){
 memset(f,0,sizeof(*f));f->ps=calloc(1,1048576);f->code=calloc(1,8388608);f->weights=calloc(1,67108864);f->ws=calloc(1,VXL_HUNET_WS_BYTES);f->stack=calloc(1,1048576);assert(f->ps&&f->code&&f->weights&&f->ws&&f->stack);
 vxl_engine_init_profile(&f->e,&fops,f,1,4);vxl_hunet_release_defaults(&f->r);assert(!vxl_hunet_layout_init(&f->c,&f->e,&f->r));f->c.io=(struct vxl_hunet_io){f,fio,fns};f->c.input_snapshot=malloc(196608);assert(f->c.input_snapshot);assert(!vxl_engine_open(&f->e,123));
 file_read("build/hunet-rv32/code.bin",f->code,f->r.program[0].wire_bytes);file_read("../hunet_original_tcu_20261008_v1/runs/t4_pack_a2/weights.bin",f->weights,23685056);
}
static void destroy_fixture(struct fixture *f){free(f->ps);free(f->code);free(f->weights);free(f->ws);free(f->stack);free(f->c.input_snapshot);}
static struct vxl_request req(void){struct vxl_request r={0};r.abi_major=1;r.struct_size=176;return r;}
static vxl_u64 reserve_fixture(struct fixture *f,vxl_u32 purpose,vxl_u64 a,vxl_u64 n){struct vxl_request r=req();r.domain=purpose==VXL_ARGS?VXL_PS:VXL_PL;r.purpose=purpose;r.address=a;r.length=n;assert(!vxl_engine_call(&f->e,123,purpose==VXL_ARGS?VXL_ALLOC:VXL_RESERVE,&r));return r.handle;}
static void seal_fixture(struct fixture *f){struct vxl_hunet_seal_v1 s={0};s.magic=VXL_HUNET_SEAL_MAGIC;s.version=1;s.bytes=128;s.code=reserve_fixture(f,VXL_CODE,VXL_HUNET_CODE,8388608);s.weights=reserve_fixture(f,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864);s.workspace=reserve_fixture(f,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES);s.args=reserve_fixture(f,VXL_ARGS,0,128);memcpy(s.binding_sha256,f->r.binding_sha,32);assert(!vxl_hunet_seal(&f->c,&s));}

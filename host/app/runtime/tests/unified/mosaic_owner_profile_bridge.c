#define _POSIX_C_SOURCE 200809L
#include "mosaic_owner_bridge.h"
#include "mosaic_owner_profile_hooks.h"
#include "../v2/port_fixture.h"
#include <vxl/v2/control.h>
#include <time.h>
#include <vxl/hunet.h>
/* HOST protocol fixture only. Native loader/relocation/pinning/ACK execute,
 * but GPU output is an explicit ZERO MARKER, never GPU math/reference data. */
struct native_bridge {struct os os;struct vxl_v2_controls controls;struct vxl_v2_file_ops files;unsigned char catalog[4096],code[65537],rodata[6307841];int corrupt;unsigned input_expected,input_checked;unsigned char input_sha[32];};
static int native_file_read(void*p,const char*path,vxl_u32 max,struct vxl_v2_file_blob*b){struct native_bridge*n=p;char expected[128],hex[]="0123456789abcdef";struct vxl_v2_catalog_entry e;unsigned i;assert(max==8388608);memcpy(&e,n->catalog+64,512);strcpy(expected,"/lib/firmware/vxl/programs/");for(i=0;i<32;i++){expected[27+2*i]=hex[e.entry_id[i]>>4];expected[28+2*i]=hex[e.entry_id[i]&15];}memcpy(expected+27+64,".bin",5);assert(!strcmp(path,expected));b->bytes=malloc(6373376);assert(b->bytes);memcpy(b->bytes,n->code,65536);memcpy(b->bytes+65536,n->rodata,6307840);b->length=6373376;b->mode=0444;b->regular=b->no_follow=1;return 0;}
static void native_file_release(void*p,struct vxl_v2_file_blob*b){(void)p;free(b->bytes);}
extern int mosaic_owner_validate_args(const void*);
static int native_prepare(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,const void*args,vxl_u32 bytes){
 struct os*o=p;struct native_bridge*n=(struct native_bridge*)o;const vxl_u32*a=args;vxl_u32 roles[2],count=1,i;(void)program;assert(bytes==128&&j->num_refs==8&&!mosaic_owner_validate_args(args));
 if(n->input_expected&&!n->input_checked){struct vxl_v2_object*input;unsigned char actual[32];assert(!vxl_v2_ref_resolve(&o->f.c,1,&j->refs[0],1,&input));vxl_hunet_hash((unsigned char*)input->storage+j->refs[0].offset,j->refs[0].length,actual);assert(!memcmp(actual,n->input_sha,32));n->input_checked=1;}
 roles[0]=(a[2]==1||a[2]>=7)?2:a[2]==3?5:1;if((a[2]==1||a[2]>=7)&&(a[3]&1)){roles[1]=a[2]==1?6:1;count=2;}
 for(i=0;i<count;i++){struct vxl_v2_object*y;const struct vxl_v2_ref*r=&j->refs[roles[i]];assert(!vxl_v2_ref_resolve(&o->f.c,1,r,2,&y));assert(r->offset+r->length<y->length);memset((unsigned char*)y->storage+r->offset,0,(size_t)r->length);if(n->corrupt){((unsigned char*)y->storage)[r->offset+r->length]^=255;n->corrupt=0;}}
 {struct vxl_v2_object*work;const struct vxl_v2_ref*r=&j->refs[7];assert(!vxl_v2_ref_resolve(&o->f.c,1,r,2,&work));owner512_inject(args,(unsigned char*)work->storage+r->offset,r->length,r->handle,r->object_generation,r->offset);}
 return 0;
}
static int native_start(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,struct vxl_v2_start_proof*s){struct os*o=p;return start(&o->f.hw,program,j,s);}
static int native_ack(void*p,vxl_u32*idle,vxl_u32*ready){struct os*o=p;int rc=ack(&o->f.hw,idle,ready);owner512_ack(o->f.hw.acks);return rc;}
static vxl_u64 native_now(void*p){return ((struct os*)p)->f.hw.now;}
static const struct vxl_v2_execution_ops native_execution={native_prepare,native_start,native_ack,native_now};
void*mosaic_owner_bridge_create(const char*catalog_path,const char*code_path,const char*ro_path,const unsigned char compatible[32]){
 struct native_bridge*n=calloc(1,sizeof(*n));struct fixture*f;struct vxl_v2_provider provider;struct vxl_v2_catalog_entry e;struct vxl_v2_caps*c;struct timespec ts;size_t size;assert(n);f=&n->os.f;size=read_fixture(catalog_path,n->catalog,sizeof(n->catalog));memcpy(&e,n->catalog+64,512);provider.flags=4;memcpy(provider.hardware_contract,e.hardware_contract,32);memcpy(provider.bit,e.bit,32);memcpy(provider.xsa,e.xsa,32);assert(!vxl_v2_catalog_open(&f->cat,n->catalog,(vxl_u32)size,&provider));size=read_fixture(code_path,n->code,sizeof(n->code));assert(size==65536);size=read_fixture(ro_path,n->rodata,sizeof(n->rodata));assert(size==6307840);
 vxl_v2_init(&f->c,&program_ops,&f->memory);assert(!vxl_v2_table_bind(&f->c,&f->table,&f->cat));assert(!vxl_v2_owner_enter(&f->c,1,1));f->q=calloc(1,sizeof(*f->q));assert(f->q);clock_gettime(CLOCK_MONOTONIC,&ts);f->hw.now=(vxl_u64)ts.tv_sec*1000000000ULL+ts.tv_nsec;f->hw.jobs=7;assert(!vxl_v2_queue_bind(&f->c,f->q,&native_execution,&n->os));assert(!pthread_mutex_init(&n->os.mutex,0));n->os.finish=1;n->os.port=(struct vxl_v2_port){.ctx=&f->c,.ops=&port_ops,.os=&n->os,.control=vxl_v2_control,.control_context=&n->controls};n->files=(struct vxl_v2_file_ops){n,native_file_read,native_file_release};n->controls.files=&n->files;c=&n->controls.caps;c->version=2;c->bytes=256;c->max_objects=64;c->max_pending=32;c->max_completions=64;c->max_refs=8;c->max_args=4096;c->max_programs=16;c->cycle_bits=32;c->contexts=1;c->feature_mask=4;c->identity_source=1;c->ps_base=VXL_PS_BASE;c->ps_bytes=VXL_PS_SIZE;c->pl_base=VXL_PL_BASE;c->pl_bytes=VXL_PL_SIZE;memcpy(c->hardware_contract,e.hardware_contract,32);memcpy(c->bit,e.bit,32);memcpy(c->xsa,e.xsa,32);memcpy(c->compatible,compatible,32);return n;
}
int mosaic_owner_bridge_call(void*p,unsigned long cmd,void*opaque){
 struct native_bridge*n=p;struct vxl_compute_v2_request*r=opaque;struct vxl_v2_copy_desc copied;int is_copy=cmd==VXL_V2_BUFFER_COPY,rc;
 if(is_copy)memcpy(&copied,(const void*)(uintptr_t)r->user_ptr,sizeof(copied));
 rc=dispatch(&n->os,(unsigned)cmd,r);
 if(n->os.f.q->pending&&!n->os.f.c.active){lock_os(&n->os);assert(!vxl_v2_worker_locked(&n->os.port));unlock_os(&n->os);}
 if(is_copy){const struct vxl_v2_ref*ref=copied.direction==1?&copied.destination:&copied.source;owner512_copy(copied.direction,ref->handle,ref->object_generation,ref->offset,copied.bytes,copied.user_ptr,n->os.f.hw.acks,rc);}
 if(!rc&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&r->flags==VXL_V2_READ))){struct vxl_v2_completion*c=(void*)(uintptr_t)r->user_ptr;c->cycles_valid=0;c->elapsed_raw=0;owner512_snapshot(c);}
 return rc;
}
unsigned mosaic_owner_bridge_starts(void*p){return ((struct native_bridge*)p)->os.f.hw.starts;}
unsigned mosaic_owner_bridge_acks(void*p){return ((struct native_bridge*)p)->os.f.hw.acks;}
unsigned mosaic_owner_bridge_live(void*p){return ((struct native_bridge*)p)->os.f.c.live;}
void mosaic_owner_bridge_corrupt_guard(void*p){((struct native_bridge*)p)->corrupt=1;}
void mosaic_owner_bridge_destroy(void*p){struct native_bridge*n=p;lock_os(&n->os);assert(!vxl_v2_close_locked(&n->os.port,1));unlock_os(&n->os);teardown_host_storage(&n->os.f);pthread_mutex_destroy(&n->os.mutex);free(n);}

void mosaic_owner_bridge_expect_input(void*p,const unsigned char hash[32]){struct native_bridge*n=p;memcpy(n->input_sha,hash,32);n->input_expected=1;n->input_checked=0;}
unsigned mosaic_owner_bridge_checked_input(void*p){return ((struct native_bridge*)p)->input_checked;}

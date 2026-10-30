#define _POSIX_C_SOURCE 200809L
#include "image_owner_bridge.h"
#include "../v2/port_fixture.h"
#include <vxl/v2/control.h>
#include <time.h>
#include "image/pipeline_linux.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/tests/rv32_image_model.h"
#pragma GCC diagnostic pop
/* HOST protocol fixture only. Native loader/relocation/pinning/ACK execute,
 * but GPU output is an explicit ZERO RGB/opaque MARKER, never GPU math/reference data. */
struct native_bridge {struct os os;struct vxl_v2_controls controls;struct vxl_v2_file_ops files;unsigned char catalog[20000];char program_dir[1024];int corrupt,arithmetic;};
static int native_file_read(void*p,const char*path,vxl_u32 max,struct vxl_v2_file_blob*b){
 struct native_bridge*n=p;char file[1200];assert(max==8388608);assert(strlen(path)==95&&!strncmp(path,"/lib/firmware/vxl/programs/",27));
 assert(snprintf(file,sizeof(file),"%s/%s",n->program_dir,path+27)>0);b->bytes=malloc(2049);assert(b->bytes);b->length=read_fixture(file,b->bytes,2049);assert(b->length==2048);b->mode=0444;b->regular=b->no_follow=1;return 0;
}
static void native_file_release(void*p,struct vxl_v2_file_blob*b){(void)p;free(b->bytes);}
/* Optional HOST frozen instruction model. Actual C loader/relocator/object pins
 * still own all addresses. The model never represents physical GPU timing. */
static int native_prepare(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,const void*args,vxl_u32 bytes){
 struct os*o=p;struct native_bridge*n=(struct native_bridge*)o;struct vxl_v2_object*y;const struct vxl_v2_ref*r=&j->refs[3];const vxl_u32*a=args;vxl_u64 i;
 assert(bytes==64&&j->num_refs==5);assert(!vxl_v2_ref_resolve(&o->f.c,1,r,2,&y));assert(r->offset+r->length<y->length);
 if(n->arithmetic){
  struct vxl_v2_object*code;uint32_t patched[16];const unsigned address_words[]={0,1,2,3,10};image_job logical;unsigned count;
  assert((a[4]==256&&a[5]==144)||(a[4]==1280&&a[5]==720));assert(!im_memory&&y->length==VX_IMAGE_RESERVED_BYTES+192ULL);
  for(unsigned k=0;k<5;k++)assert(j->refs[k].handle==r->handle&&j->refs[k].object_generation==r->object_generation);
  memcpy(patched,args,64);
  for(unsigned k=0;k<5;k++){unsigned at=address_words[k];if(patched[at]){assert((vxl_u64)patched[at]>=y->gpu+64ULL&&(vxl_u64)patched[at]<y->gpu+64ULL+VX_IMAGE_RESERVED_BYTES);patched[at]=IMAGE_SLOT_BASE+(patched[at]-y->gpu-64u);}}
  memcpy(&logical,patched,64);assert(!vxl_v2_ref_resolve(&o->f.c,1,&program->owned[0],1,&code));assert(code->sealed&&program->owned[0].length==2048);
  count=logical.width*logical.height;if(program->entry.logical_program==520){assert(logical.param0==2||logical.param0==4||logical.param0==8);count/=logical.param0*logical.param0;}
  im_memory=(uint32_t*)((unsigned char*)y->storage+64);int rc=im_run((const uint32_t*)((unsigned char*)code->storage+program->owned[0].offset),2048/4,&logical,count);im_memory=0;assert(!rc);
 }else{
  assert((a[4]==256&&a[5]==144)||(a[4]==1280&&a[5]==720));memset((unsigned char*)y->storage+r->offset,0,(size_t)r->length);for(i=3;i<r->length;i+=4)((unsigned char*)y->storage)[r->offset+i]=255;
 }
 if(n->corrupt){((unsigned char*)y->storage)[r->offset+r->length]^=255;n->corrupt=0;}return 0;
}
static int native_start(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,struct vxl_v2_start_proof*s){struct os*o=p;return start(&o->f.hw,program,j,s);}
static int native_ack(void*p,vxl_u32*idle,vxl_u32*ready){struct os*o=p;return ack(&o->f.hw,idle,ready);}
static vxl_u64 native_now(void*p){return ((struct os*)p)->f.hw.now;}
static const struct vxl_v2_execution_ops native_execution={native_prepare,native_start,native_ack,native_now};
void*image_owner_bridge_create(const char*catalog_path,const char*program_dir,const unsigned char compatible[32]){
 struct native_bridge*n=calloc(1,sizeof(*n));struct fixture*f;struct vxl_v2_provider provider;struct vxl_v2_catalog_entry e;struct vxl_v2_caps*c;struct timespec ts;size_t size;assert(n);f=&n->os.f;size=read_fixture(catalog_path,n->catalog,sizeof(n->catalog));memcpy(&e,n->catalog+64,512);provider.flags=1;memcpy(provider.hardware_contract,e.hardware_contract,32);memcpy(provider.bit,e.bit,32);memcpy(provider.xsa,e.xsa,32);assert(!vxl_v2_catalog_open(&f->cat,n->catalog,(vxl_u32)size,&provider));assert(strlen(program_dir)<sizeof(n->program_dir));strcpy(n->program_dir,program_dir);
 vxl_v2_init(&f->c,&program_ops,&f->memory);assert(!vxl_v2_table_bind(&f->c,&f->table,&f->cat));assert(!vxl_v2_owner_enter(&f->c,1,1));f->q=calloc(1,sizeof(*f->q));assert(f->q);clock_gettime(CLOCK_MONOTONIC,&ts);f->hw.now=(vxl_u64)ts.tv_sec*1000000000ULL+ts.tv_nsec;f->hw.jobs=7;assert(!vxl_v2_queue_bind(&f->c,f->q,&native_execution,&n->os));assert(!pthread_mutex_init(&n->os.mutex,0));n->os.finish=1;n->os.port=(struct vxl_v2_port){.ctx=&f->c,.ops=&port_ops,.os=&n->os,.control=vxl_v2_control,.control_context=&n->controls};n->files=(struct vxl_v2_file_ops){n,native_file_read,native_file_release};n->controls.files=&n->files;c=&n->controls.caps;c->version=2;c->bytes=256;c->max_objects=64;c->max_pending=32;c->max_completions=64;c->max_refs=8;c->max_args=4096;c->max_programs=16;c->cycle_bits=32;c->contexts=1;c->feature_mask=1;c->identity_source=1;c->ps_base=VXL_PS_BASE;c->ps_bytes=VXL_PS_SIZE;c->pl_base=VXL_PL_BASE;c->pl_bytes=VXL_PL_SIZE;memcpy(c->hardware_contract,e.hardware_contract,32);memcpy(c->bit,e.bit,32);memcpy(c->xsa,e.xsa,32);memcpy(c->compatible,compatible,32);return n;
}
int image_owner_bridge_call(void*p,unsigned long cmd,void*r){struct native_bridge*n=p;int rc=dispatch(&n->os,(unsigned)cmd,r);if(n->os.f.q->pending&&!n->os.f.c.active){lock_os(&n->os);assert(!vxl_v2_worker_locked(&n->os.port));unlock_os(&n->os);}if(!rc&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&((struct vxl_compute_v2_request*)r)->flags==VXL_V2_READ))){struct vxl_v2_completion*c=(void*)(uintptr_t)((struct vxl_compute_v2_request*)r)->user_ptr;c->cycles_valid=0;c->elapsed_raw=0;}return rc;}
unsigned image_owner_bridge_starts(void*p){return ((struct native_bridge*)p)->os.f.hw.starts;}
unsigned image_owner_bridge_acks(void*p){return ((struct native_bridge*)p)->os.f.hw.acks;}
unsigned image_owner_bridge_live(void*p){return ((struct native_bridge*)p)->os.f.c.live;}
void image_owner_bridge_corrupt_guard(void*p){((struct native_bridge*)p)->corrupt=1;}
void image_owner_bridge_destroy(void*p){struct native_bridge*n=p;lock_os(&n->os);assert(!vxl_v2_close_locked(&n->os.port,1));unlock_os(&n->os);teardown_host_storage(&n->os.f);pthread_mutex_destroy(&n->os.mutex);free(n);}

/* Explicit opt-in per host process; sole owner serializes all model calls. */
void image_owner_bridge_arithmetic(void*p){((struct native_bridge*)p)->arithmetic=1;}

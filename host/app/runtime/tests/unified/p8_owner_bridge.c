#define _POSIX_C_SOURCE 200809L
#include "p8_owner_bridge.h"
#include "../v2/port_fixture.h"
#include <vxl/v2/control.h>
#include <time.h>
/* Protocol-only CPU oracle injection. No RV32/GPU execution, native hardware
 * retirement policy, DP or performance acceptance. Common real C relocation,
 * queue, buffer pins and original ACK execute in host memory. */
struct native_bridge {struct os os;struct vxl_v2_controls controls;struct vxl_v2_file_ops files;unsigned char catalog[65536];char program_dir[1024];int corrupt;unsigned char *projected,*color,*depth;size_t projected_bytes,pixel_bytes;void(*before_prepare)(void*,unsigned);void*oracle_context;unsigned char*expected[10];size_t expected_bytes[10];unsigned expected_width;};
static int native_file_read(void*p,const char*path,vxl_u32 max,struct vxl_v2_file_blob*b){
 struct native_bridge*n=p;char file[1200];assert(max==8388608);assert(strlen(path)==95&&!strncmp(path,"/lib/firmware/vxl/programs/",27));
 assert(snprintf(file,sizeof(file),"%s/%s",n->program_dir,path+27)>0);/* Actual frozen joint NN and P8 payloads; production catalog verifies every byte/SHA. */
 b->bytes=malloc((size_t)max+1);assert(b->bytes);b->length=read_fixture(file,b->bytes,(size_t)max+1);assert(b->length&&b->length<=max);b->mode=0444;b->regular=b->no_follow=1;return 0;
}
static void native_file_release(void*p,struct vxl_v2_file_blob*b){(void)p;free(b->bytes);}
static int native_prepare(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,const void*args,vxl_u32 bytes){
 struct native_bridge*n=p;struct vxl_v2_object*o;const vxl_u32*a=args;assert(bytes==64);unsigned id=program->entry.logical_program;
 if(n->before_prepare)n->before_prepare(n->oracle_context,id);
 if(n->expected_width){
  const unsigned transform=id==9||id==11;const unsigned roles[]={8,6,2,7,9};
  if(transform){const struct vxl_v2_ref*r=&j->refs[0];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,r,1,&o));assert(r->length==n->expected_bytes[3]&&!memcmp((unsigned char*)o->storage+r->offset,n->expected[3],r->length));
   const size_t pixels=(size_t)n->expected_width*n->expected_width,color=pixels*4,depth_at=((color+191)&~(size_t)63)+64;const vxl_u32*rgba=(void*)((unsigned char*)o->storage+64),*depth=(void*)((unsigned char*)o->storage+depth_at);for(size_t i=0;i<pixels;++i)assert(rgba[i]==0xdeadbeef&&depth[i]==0xdeadbeef);
   r=&j->refs[1];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,r,2,&o));const vxl_u32*projected=(void*)((unsigned char*)o->storage+r->offset);const size_t active=(size_t)a[2]*8;for(size_t i=0;i<r->length/4;++i)assert(projected[i]==(i<active?0xdeadbeef:0));
  }else for(unsigned i=0;i<5;++i){const unsigned slot=roles[i];const struct vxl_v2_ref*r=&j->refs[i+2];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,r,1,&o));assert(r->length==n->expected_bytes[slot]);size_t consumed=r->length;if(n->controls.caps.flags&VXL_V2_CAP_FAST_RUNTIME){if(slot==8)consumed=(size_t)a[8]*64*4;if(slot==7){struct vxl_v2_object*dir;assert(!vxl_v2_ref_resolve(&n->os.f.c,1,&j->refs[3],1,&dir));const vxl_u32*d=(const void*)((unsigned char*)dir->storage+j->refs[3].offset);const size_t tiles=(size_t)n->expected_width*n->expected_width/16;vxl_u32 next=0;for(size_t k=0;k<tiles;k++){assert(d[k*2]==next&&d[k*2+1]<=262144-next);next+=d[k*2+1];}consumed=(size_t)next*4;}}assert(consumed<=r->length&&!memcmp((unsigned char*)o->storage+r->offset,n->expected[slot],consumed));}
 }

 if(id==9||id==11){assert(j->num_refs==2&&n->projected);const struct vxl_v2_ref*r=&j->refs[1];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,r,2,&o)&&r->length==n->projected_bytes);memcpy((unsigned char*)o->storage+r->offset,n->projected,r->length);if(n->corrupt==2)((unsigned char*)o->storage)[r->offset]^=1;}
 else{assert((id==10||id==12)&&j->num_refs==7&&n->color&&n->depth);unsigned tiles=id==10?4096:16384;struct vxl_v2_object*d,*r;assert(!vxl_v2_ref_resolve(&n->os.f.c,1,&j->refs[3],1,&d));assert(!vxl_v2_ref_resolve(&n->os.f.c,1,&j->refs[5],1,&r));const vxl_u32*directory=(void*)((unsigned char*)d->storage+j->refs[3].offset);const vxl_u32*refs=(void*)((unsigned char*)r->storage+j->refs[5].offset);vxl_u32 next=0;for(unsigned i=0;i<tiles;i++){assert(directory[2*i]==next&&directory[2*i+1]<=262144-next);next+=directory[2*i+1];}for(unsigned i=0;i<next;i++)assert(refs[i]<a[8]);for(unsigned i=0;i<2;i++){const struct vxl_v2_ref*ref=&j->refs[i];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,ref,2,&o)&&ref->length==n->pixel_bytes);memcpy((unsigned char*)o->storage+ref->offset,i?n->depth:n->color,ref->length);}}
 if((id==10||id==12)&&(n->corrupt==3||n->corrupt==4)){const struct vxl_v2_ref*r=&j->refs[n->corrupt==4?1:0];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,r,2,&o));*(vxl_u32*)((unsigned char*)o->storage+r->offset)=0xdeadbeef;}
 if(n->corrupt==1){const struct vxl_v2_ref*ref=&j->refs[id==9||id==11?1:0];assert(!vxl_v2_ref_resolve(&n->os.f.c,1,ref,2,&o));((unsigned char*)o->storage)[ref->offset-1]^=1;}return 0;
}
static int native_start(void*p,const struct vxl_v2_program*program,const struct vxl_v2_job*j,struct vxl_v2_start_proof*s){struct os*o=p;return start(&o->f.hw,program,j,s);}
static int native_ack(void*p,vxl_u32*idle,vxl_u32*ready){struct os*o=p;return ack(&o->f.hw,idle,ready);}
static vxl_u64 native_now(void*p){return ((struct os*)p)->f.hw.now;}
static const struct vxl_v2_execution_ops native_execution={native_prepare,native_start,native_ack,native_now};
void*p8_owner_bridge_create(const char*catalog_path,const char*program_dir,const unsigned char compatible[32]){
 struct native_bridge*n=calloc(1,sizeof(*n));struct fixture*f;struct vxl_v2_provider provider;struct vxl_v2_catalog_entry e;struct vxl_v2_caps*c;struct timespec ts;size_t size;assert(n);f=&n->os.f;size=read_fixture(catalog_path,n->catalog,sizeof(n->catalog));memcpy(&e,n->catalog+64,512);assert(e.provider_flags==2||e.provider_flags==4);provider.flags=e.provider_flags;memcpy(provider.hardware_contract,e.hardware_contract,32);memcpy(provider.bit,e.bit,32);memcpy(provider.xsa,e.xsa,32);assert(!vxl_v2_catalog_open(&f->cat,n->catalog,(vxl_u32)size,&provider));assert(strlen(program_dir)<sizeof(n->program_dir));strcpy(n->program_dir,program_dir);
 vxl_v2_init(&f->c,&program_ops,&f->memory);assert(!vxl_v2_table_bind(&f->c,&f->table,&f->cat));assert(!vxl_v2_owner_enter(&f->c,1,1));f->q=calloc(1,sizeof(*f->q));assert(f->q);clock_gettime(CLOCK_MONOTONIC,&ts);f->hw.now=(vxl_u64)ts.tv_sec*1000000000ULL+ts.tv_nsec;f->hw.jobs=7;assert(!vxl_v2_queue_bind(&f->c,f->q,&native_execution,&n->os));assert(!pthread_mutex_init(&n->os.mutex,0));n->os.finish=1;n->os.port=(struct vxl_v2_port){.ctx=&f->c,.ops=&port_ops,.os=&n->os,.control=vxl_v2_control,.control_context=&n->controls};n->files=(struct vxl_v2_file_ops){n,native_file_read,native_file_release};n->controls.files=&n->files;c=&n->controls.caps;c->version=2;c->bytes=256;c->max_objects=64;c->max_pending=32;c->max_completions=64;c->max_refs=8;c->max_args=4096;c->max_programs=16;c->cycle_bits=32;c->contexts=1;c->feature_mask=provider.flags;c->identity_source=1;c->ps_base=VXL_PS_BASE;c->ps_bytes=VXL_PS_SIZE;c->pl_base=VXL_PL_BASE;c->pl_bytes=VXL_PL_SIZE;memcpy(c->hardware_contract,e.hardware_contract,32);memcpy(c->bit,e.bit,32);memcpy(c->xsa,e.xsa,32);memcpy(c->compatible,compatible,32);return n;
}
int p8_owner_bridge_call(void*p,unsigned long cmd,void*r){struct native_bridge*n=p;int rc=dispatch(&n->os,(unsigned)cmd,r);if(n->os.f.q->pending&&!n->os.f.c.active){lock_os(&n->os);assert(!vxl_v2_worker_locked(&n->os.port));unlock_os(&n->os);}if(!rc&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&((struct vxl_compute_v2_request*)r)->flags==VXL_V2_READ))){struct vxl_v2_completion*c=(void*)(uintptr_t)((struct vxl_compute_v2_request*)r)->user_ptr;c->cycles_valid=0;c->elapsed_raw=0;}return rc;}
unsigned p8_owner_bridge_starts(void*p){return ((struct native_bridge*)p)->os.f.hw.starts;}
unsigned p8_owner_bridge_acks(void*p){return ((struct native_bridge*)p)->os.f.hw.acks;}
unsigned p8_owner_bridge_live(void*p){return ((struct native_bridge*)p)->os.f.c.live;}
void p8_owner_bridge_corrupt_guard(void*p){((struct native_bridge*)p)->corrupt=1;}
void p8_owner_bridge_destroy(void*p){struct native_bridge*n=p;lock_os(&n->os);assert(!vxl_v2_close_locked(&n->os.port,1));unlock_os(&n->os);teardown_host_storage(&n->os.f);for(unsigned i=0;i<10;++i)free(n->expected[i]);free(n->projected);free(n->color);free(n->depth);pthread_mutex_destroy(&n->os.mutex);free(n);}

void p8_owner_bridge_oracle(void*p,const void*projected,size_t n,const void*color,const void*depth,size_t bytes){struct native_bridge*b=p;assert(!b->projected&&!b->color&&!b->depth);b->projected=malloc(n);b->color=malloc(bytes);b->depth=malloc(bytes);assert(b->projected&&b->color&&b->depth);memcpy(b->projected,projected,n);memcpy(b->color,color,bytes);memcpy(b->depth,depth,bytes);b->projected_bytes=n;b->pixel_bytes=bytes;}
void p8_owner_bridge_before_prepare(void*p,void(*callback)(void*,unsigned),void*context){struct native_bridge*b=p;assert(!b->os.f.hw.starts&&!b->before_prepare);b->before_prepare=callback;b->oracle_context=context;}
void p8_owner_bridge_replace_oracle(void*p,const void*projected,size_t n,const void*color,const void*depth,size_t bytes){struct native_bridge*b=p;assert(b->before_prepare);free(b->projected);free(b->color);free(b->depth);b->projected=b->color=b->depth=0;p8_owner_bridge_oracle(p,projected,n,color,depth,bytes);}
void p8_owner_bridge_bad_projection(void*p){((struct native_bridge*)p)->corrupt=2;}

void p8_owner_bridge_bad_pixel(void*p,int depth){((struct native_bridge*)p)->corrupt=depth?4:3;}

void p8_owner_bridge_enable_fast(void*p){((struct native_bridge*)p)->controls.caps.flags|=VXL_V2_CAP_FAST_RUNTIME;((struct native_bridge*)p)->os.f.q->fast_supported=1;}

void p8_owner_bridge_expected_frame(void*p,const void*const buffers[10],const size_t bytes[10],unsigned width){struct native_bridge*n=p;assert(width==256||width==512);n->expected_width=width;for(unsigned i=0;i<10;++i)if(i==2||i==3||i==6||i==7||i==8||i==9){free(n->expected[i]);n->expected[i]=malloc(bytes[i]);assert(n->expected[i]);memcpy(n->expected[i],buffers[i],bytes[i]);n->expected_bytes[i]=bytes[i];}}

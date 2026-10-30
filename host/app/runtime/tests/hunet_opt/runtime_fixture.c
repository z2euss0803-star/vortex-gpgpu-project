#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../hunet_hybrid/fixture.h"
#include <vxl/hunet_cpu_gelu.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../../generated/hunet/layout.h"
#include "../../generated/hunet_fused/identity.h"
#include "../../generated/hunet_fused/layout.h"
static struct vxl_opt_release opt;
static struct hfixture h;static int mode;static unsigned consumed;static vxl_u32 last_cpu_y;static vxl_u64 last_cpu_bytes;static vxl_u8 last_cpu_sha[32];
#define TEST_FD 1000001
static vxl_u64 clock_ns(void *p){struct timespec t;(void)p;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (vxl_u64)t.tv_sec*1000000000ULL+t.tv_nsec;}
static int runtime_poll(void *p,struct vxl_event *e){
 struct fixture *f=p;struct vxl_hunet_hw_snapshot s={0};unsigned i;int rc;if(!f->done)return 0;
 s.event.id=f->e.job.id;s.event.status=2;s.event.ready=1;s.event.jobs=f->hw_jobs+1;s.watchdog=4000000000u;s.cdma_status=0x1002;
 for(i=0;i<4;i++)memset(f->ps+0x2088+i*0x100,0,4);
 rc=vxl_hunet_crt_snapshot(&f->c,&s);if(rc)return rc;rc=vxl_hunet_accept_done(&f->c,&s,clock_ns(p),&f->c.evidence);if(rc)return rc;rc=vxl_hunet_after_ack(&f->c,1,1,clock_ns(p));if(rc)return rc;*e=s.event;e->kind=VXL_DONE;e->idle=1;f->hw_jobs++;return 1;
}
void runtime_fixture_init(int fault){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 hinit(&h);h.f.c.active=0;h.f.c.io.now_ns=clock_ns;h.ops.poll=runtime_poll;mode=fault;consumed=0;last_cpu_y=0;last_cpu_bytes=0;
 memset(&opt,0,sizeof(opt));opt.catalog=vxl_opt_catalog_default();opt.digest_backend=(opt.catalog->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE;opt.wire_bytes=h.f.r.program[0].wire_bytes;opt.ro_bytes=h.f.r.program[0].ro_bytes;
 memcpy(opt.wire_sha,h.f.r.program[0].wire_sha,32);memcpy(opt.ro_sha,h.f.r.program[0].ro_sha,32);
 opt.binding.v3.v2=h.release.binding;opt.binding.v3.v2.v1.version=4;opt.binding.v3.v2.v1.bytes=512;opt.binding.v3.v2.v1.program_id=10;opt.binding.v3.v2.v1.features=31;
 memset(opt.binding.v3.v2.v1.source_sha256,0x81,32);memcpy(opt.binding.v3.v2.v1.jobs_sha256,opt.catalog->jobs_sha,32);
 memset(opt.binding.v3.fusion_contract_sha256,0x34,32);memcpy(opt.binding.v3.descriptors_sha256,opt.catalog->descriptors_sha,32);memcpy(opt.binding.v3.layout_sha256,opt.catalog->layout_sha,32);
 vxl_hunet_hash(&opt.binding,512,opt.binding_sha);h.f.c.sealed=0;h.f.c.program_sealed=0;assert(!vxl_opt_init(&h.f.c,&opt));
 struct vxl_hunet_seal_v1 seal={.magic=VXL_HYBRID_SEAL_MAGIC,.version=1,.bytes=128};seal.code=h.f.c.handles[0];seal.weights=h.f.c.handles[1];seal.workspace=h.f.c.handles[2];seal.args=h.f.c.handles[3];memcpy(seal.binding_sha256,opt.binding_sha,32);assert(!vxl_opt_seal(&h.f.c,&seal));
 h.f.c.digest_enabled=1;h.f.c.digest_backend=opt.digest_backend;
}
void runtime_fixture_free(void){hfree(&h);}
unsigned runtime_gpu_count(void){return h.f.starts;}
unsigned runtime_cpu_count(void){return h.f.c.hybrid.cpu_jobs;}
unsigned runtime_consumed_count(void){return consumed;}
int __real_open(const char *,int,...);
int __wrap_open(const char *p,int flags,...){
 if(!strcmp(p,"/dev/vortex0"))return TEST_FD;
 if(flags&O_CREAT){va_list args;va_start(args,flags);int permissions=va_arg(args,int);va_end(args);return __real_open(p,flags,permissions);}
 return __real_open(p,flags);
}
int __real_close(int);
int __wrap_close(int fd){if(fd==TEST_FD){vxl_engine_close(&h.f.e,123);return 0;}return __real_close(fd);}
int __wrap_ioctl(int fd,unsigned long cmd,...){
 va_list args;va_start(args,cmd);struct vxl_request *r=va_arg(args,struct vxl_request *);va_end(args);int rc;
 if(fd!=TEST_FD){errno=EBADF;return -1;}
 if(mode==2&&cmd==VXL_HYBRID_GET_BINDING){errno=EOPNOTSUPP;return -1;}
 if(cmd==VXL_WAIT)h.f.done=1;
 if(cmd==VXL_HYBRID_CPU_BEGIN){const struct vxl_hybrid_begin_v1 *p=(const void *)(uintptr_t)r->user_ptr;struct vxl_hybrid_begin_v1 expected=hbegin_request(&h,(void *)(uintptr_t)p->input_ptr);assert(!memcmp(p,&expected,128));const vxl_u32 *j=vxl_hunet_catalog_job(h.f.c.next_sequence);float *x=(float *)map(&h.f,j[8],(vxl_u64)j[7]*4);for(unsigned i=0;i<j[7];i++)x[i]=((int)(i%97)-48)/16.f;}
 if(cmd==VXL_SUBMIT&&last_cpu_y){
  struct vxl_hunet_job_v1 j;memcpy(&j,h.f.c.args_shadow,128);assert(j.x==last_cpu_y);vxl_u8 sha[32];vxl_hunet_hash(map(&h.f,j.x,last_cpu_bytes),last_cpu_bytes,sha);assert(!memcmp(sha,last_cpu_sha,32));consumed++;last_cpu_y=0;
 }
 rc=vxl_engine_call(&h.f.e,123,(vxl_u32)cmd,r);
 if(!rc&&cmd==VXL_HUNET_BEGIN_ROI){
  /* Finite test bytes in slots. This is not GPU tensor arithmetic. */
  for(unsigned i=0;i<106;i++)if(vxl_hunet_slots[i].writable)memset(map(&h.f,vxl_hunet_slots[i].start,vxl_hunet_slots[i].bytes),0,(size_t)vxl_hunet_slots[i].bytes);
  memcpy(map(&h.f,VXL_HUNET_INPUT,196608),h.f.c.input_snapshot,196608);
  assert(!vxl_opt_restore_descriptors(&h.f.c));assert(!vxl_hunet_guards(&h.f.c,1));
 }
 if(!rc&&cmd==VXL_SUBMIT){struct vxl_opt_meta m;assert(!vxl_opt_validate_job(&h.f.c,h.f.c.args_shadow,h.f.c.next_sequence,&m));if(m.owner_tiles){vxl_u32 *owners=(vxl_u32 *)map(&h.f,0x84040040,(vxl_u64)m.owner_tiles*4);for(unsigned tile=0;tile<m.owner_tiles;tile++)assert(!vxl_opt_owner_expected(opt.catalog,&m,tile,&owners[tile]));if(mode==4)owners[0]^=1;}}

 if(!rc&&cmd==VXL_HYBRID_CPU_COMMIT){const struct vxl_hybrid_commit_v1 *p=(const void *)(uintptr_t)r->user_ptr;const vxl_u32 *j=vxl_hunet_catalog_job(p->sequence);last_cpu_y=j[13];last_cpu_bytes=p->output_bytes;memcpy(last_cpu_sha,p->output_sha256,32);}
 if(!rc&&mode==3&&cmd==VXL_HYBRID_GET_BINDING)((struct vxl_opt_binding_v4 *)(uintptr_t)r->user_ptr)->v3.v2.v1.version=3;
 if(!rc){struct vxl_hunet_delivery d;struct vxl_hybrid_delivery hd;vxl_hunet_delivery_capture(&h.f.c,(vxl_u32)cmd,&d);vxl_hybrid_delivery_capture(&h.f.c,(vxl_u32)cmd,&hd);vxl_hunet_delivery_finish(&h.f.c,&d);vxl_hybrid_delivery_finish(&h.f.c,&hd);}
 if(rc){errno=-rc;return -1;}return 0;
}
int __real_hunet_cpu_gelu4(const float *,float *,size_t,struct hunet_cpu_timing *);
int __wrap_hunet_cpu_gelu4(const float *x,float *y,size_t n,struct hunet_cpu_timing *t){if(mode==1)return -EDOM;return __real_hunet_cpu_gelu4(x,y,n,t);}

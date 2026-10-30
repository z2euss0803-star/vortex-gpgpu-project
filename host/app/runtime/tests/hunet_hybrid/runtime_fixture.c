#define _GNU_SOURCE
#include "fixture.h"
#include <vxl/hunet_cpu_gelu.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../../generated/hunet/layout.h"
static struct hfixture h;static int mode;static unsigned consumed;static vxl_u32 last_cpu_y;static vxl_u64 last_cpu_bytes;static vxl_u8 last_cpu_sha[32];
#define TEST_FD 1000001
static vxl_u64 clock_ns(void *p){struct timespec t;(void)p;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (vxl_u64)t.tv_sec*1000000000ULL+t.tv_nsec;}
static int runtime_poll(void *p,struct vxl_event *e){
 struct fixture *f=p;struct vxl_hunet_hw_snapshot s={0};unsigned i;int rc;if(!f->done)return 0;
 s.event.id=f->e.job.id;s.event.status=2;s.event.ready=1;s.event.jobs=f->hw_jobs+1;s.watchdog=4000000000u;
 for(i=0;i<4;i++)memset(f->ps+0x2088+i*0x100,0,4);
 rc=vxl_hunet_crt_snapshot(&f->c,&s);if(rc)return rc;rc=vxl_hunet_accept_done(&f->c,&s,clock_ns(p),&f->c.evidence);if(rc)return rc;rc=vxl_hunet_after_ack(&f->c,1,1,clock_ns(p));if(rc)return rc;*e=s.event;e->kind=VXL_DONE;e->idle=1;f->hw_jobs++;return 1;
}
void runtime_fixture_init(int fault){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 hinit(&h);h.f.c.active=0;h.f.c.io.now_ns=clock_ns;h.ops.poll=runtime_poll;mode=fault;consumed=0;last_cpu_y=0;last_cpu_bytes=0;
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
 }
 if(!rc&&cmd==VXL_SUBMIT){struct vxl_hunet_job_v1 j;memcpy(&j,h.f.c.args_shadow,128);if(j.op==2){unsigned active=(j.tasks+3)/4;if(active>4)active=4;vxl_u32 *owners=(vxl_u32 *)map(&h.f,j.scratch,(vxl_u64)j.tasks*4);unsigned index=0;for(unsigned core=0;core<active;core++)for(unsigned n=0;n<j.tasks/active+(core<j.tasks%active);n++)owners[index++]=core+1;}}
 if(!rc&&cmd==VXL_HYBRID_CPU_COMMIT){const struct vxl_hybrid_commit_v1 *p=(const void *)(uintptr_t)r->user_ptr;const vxl_u32 *j=vxl_hunet_catalog_job(p->sequence);last_cpu_y=j[13];last_cpu_bytes=p->output_bytes;memcpy(last_cpu_sha,p->output_sha256,32);}
 if(!rc){struct vxl_hunet_delivery d;struct vxl_hybrid_delivery hd;vxl_hunet_delivery_capture(&h.f.c,(vxl_u32)cmd,&d);vxl_hybrid_delivery_capture(&h.f.c,(vxl_u32)cmd,&hd);vxl_hunet_delivery_finish(&h.f.c,&d);vxl_hybrid_delivery_finish(&h.f.c,&hd);}
 if(rc){errno=-rc;return -1;}return 0;
}
int __real_hunet_cpu_gelu4(const float *,float *,size_t,struct hunet_cpu_timing *);
int __wrap_hunet_cpu_gelu4(const float *x,float *y,size_t n,struct hunet_cpu_timing *t){if(mode==1)return -EDOM;return __real_hunet_cpu_gelu4(x,y,n,t);}

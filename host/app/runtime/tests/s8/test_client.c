/* Actual client flow with ioctl/trace/persistence/clock boundaries only. */
#include <vxl/s8_client.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct fixture {unsigned calls,submit,reap,captures,traces,sealed,fail;vxl_u64 clock;};
static int call(void *p,int fd,vxl_u32 cmd,struct vxl_request *r){struct fixture *f=p;assert(fd==7);f->calls++;if(f->fail==1&&cmd==VXL_SUBMIT)return -EIO;if(cmd==VXL_GET_INFO){r->profile_id=5;r->state=1;r->generation=2;}if(cmd==VXL_RESERVE){r->handle=f->calls;r->gpu_address=(vxl_u32)r->address;}if(cmd==VXL_SUBMIT){f->submit++;r->job_id=1;}if(cmd==VXL_WAIT){r->state=3;r->result=0;}if(cmd==VXL_COPY_TO)r->used=r->length;if(cmd==VXL_COPY_FROM){if(f->fail==2)return -EFAULT;memset((void *)(uintptr_t)r->user_ptr,0,r->length);r->used=r->length;}if(cmd==VXL_REAP){assert(f->captures==182&&f->traces==3&&f->sealed);f->reap++;}return 0;}
static int save(void *p,const char *name,const void *data,vxl_u32 bytes){struct fixture *f=p;(void)data;assert(name&&bytes);if(f->fail==3)return -ENOSPC;if(!strncmp(name,"capture-",8))f->captures++;return 0;}
static int trace(void *p,const char *name,int post){struct fixture *f=p;assert(name);if(f->fail==4)return -EIO;if(!post)f->traces++;return 0;}
static int seal(void *p,int post){struct fixture *f=p;(void)post;if(f->fail==5)return -EDOM;f->sealed=1;return 0;}
static vxl_u64 now(void *p){return ++((struct fixture *)p)->clock;}
static int event(void *p,const char *n,vxl_u64 a,vxl_u64 b,int rc){(void)p;(void)n;(void)rc;assert(a<=b);return 0;}
int main(void){
 for(unsigned fault=0;fault<=5;++fault){struct fixture f={.fail=fault};struct vxl_s8_client c={.fd=7,.io={.context=&f,.call=call,.save=save,.trace=trace,.seal=seal,.now=now,.event=event}};
  for(unsigned i=0;i<53;++i){assert(!vxl_s8_object_contract(i,&c.objects[i]));c.objects[i].initial=calloc(1,c.objects[i].bytes);assert(c.objects[i].initial);}
  assert(!vxl_s8_client_bind(&c));assert(!vxl_s8_client_prepare(&c));struct vxl_s8_job_v1 j={.magic=VXL_S8_JOB_MAGIC,.version=1,.bytes=128,.case_id=1,.core_mask=1};
  int rc=vxl_s8_run_case(7,&j,"/tmp/s8-explicit-test");if(!fault){assert(!rc&&f.reap==1&&f.submit==1);}else{assert(rc<0&&!f.reap&&c.stopped);unsigned previous=f.submit;assert(vxl_s8_run_case(7,&j,"/tmp/s8-explicit-test")<0&&f.submit==previous);}
  vxl_s8_client_unbind(&c);for(unsigned i=0;i<53;++i)free((void *)c.objects[i].initial);
 }
 puts("S8_CLIENT_CAPTURE182_TRACE_SEAL_BEFORE_REAP_FAIL_STOP_PASS SOFTWARE_ONLY");
}

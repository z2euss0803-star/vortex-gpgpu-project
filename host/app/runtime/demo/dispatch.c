#define _GNU_SOURCE
#include <vxl/demo_dispatch.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
static int valid_info(const struct vxl_request *r){return r->generation&&(r->profile_id==4||r->profile_id==5||r->profile_id==6)&&r->state==VXL_READY&&!r->errors&&!r->pending_w&&!r->pending_r;}
static int open_owner(void *p){struct demo_dispatch *d=p;int fd=d->control.open(d->control.context);if(fd<0)return fd;struct vxl_request r={0};int rc=d->control.info(d->control.context,fd,&r);if(!rc&&!valid_info(&r))rc=-ENODEV;if(rc){(void)d->control.close(d->control.context,fd);return rc;}d->profile=r.profile_id;d->session->generation=r.generation;return fd;}
static int quiet(void *p,int fd){struct demo_dispatch *d=p;if(d->active)return d->apps[d->active-1].quiescent(d->apps[d->active-1].context,fd);struct vxl_request r={0};int rc=d->control.info(d->control.context,fd,&r);return rc?rc:valid_info(&r)&&!r.used?1:0;}
static int close_owner(void *p,int fd){struct demo_dispatch *d=p;if(d->active){int rc=d->detach[d->active-1](d->apps[d->active-1].context,fd);if(rc)return rc;d->active=0;}struct vxl_request r={0};int rc=d->control.info(d->control.context,fd,&r);if(rc)return rc;if(!valid_info(&r)||r.used)return -EBUSY;return d->control.close(d->control.context,fd);}
static int start(void *p,int fd,const struct demo_run *r,uint64_t *id){struct demo_dispatch *d=p;if(!d->active||r->app!=d->active||r->generation!=d->session->generation)return -ESTALE;struct demo_gpu_ops *ops=&d->apps[d->active-1];return ops->start(ops->context,fd,r,id);}
static int poll(void *p,int fd,uint64_t id){struct demo_dispatch *d=p;if(!d->active)return -ENODEV;struct demo_gpu_ops *ops=&d->apps[d->active-1];return ops->poll(ops->context,fd,id);}
static int collect(void *p,int fd,uint64_t id,struct demo_result *result){struct demo_dispatch *d=p;if(!d->active)return -ENODEV;struct demo_gpu_ops *ops=&d->apps[d->active-1];return ops->collect(ops->context,fd,id,result);}
int demo_dispatch_init(struct demo_session *s,struct demo_store *store,vxl_u32 generation,struct demo_dispatch *d){
 if(!s||!d||!generation||!d->control.open||!d->control.close||!d->control.info||!d->control.select)return -EINVAL;
 for(unsigned i=0;i<2;++i)if(!d->apps[i].start||!d->apps[i].poll||!d->apps[i].quiescent||!d->apps[i].collect||!d->detach[i])return -EINVAL;
 struct demo_gpu_ops ops={open_owner,close_owner,start,poll,quiet,collect,d};int rc=demo_session_init(s,store,generation,&ops);if(rc)return rc;d->session=s;d->active=0;d->profile=0;s->dispatcher=d;return 0;
}
int demo_dispatch_posix_info(void *p,int fd,struct vxl_request *r){(void)p;if(fd<0||!r)return -EINVAL;memset(r,0,sizeof(*r));r->abi_major=1;r->struct_size=176;return ioctl(fd,VXL_GET_INFO,r)<0?-errno:0;}
int demo_dispatch_posix_select(void *p,int fd,vxl_u32 profile,vxl_u32 generation,vxl_u32 *out){(void)p;if(fd<0||!out)return -EINVAL;struct vxl_request r={.abi_major=1,.struct_size=176,.profile_id=profile,.generation=generation};int rc=ioctl(fd,VXL_SELECT_PROFILE,&r)<0?-errno:0;if(!rc){if(r.profile_id!=profile||r.state!=VXL_READY)return -EPROTO;*out=r.generation;}return rc;}
int demo_request_display(struct demo_session *s,const char *id,enum demo_layout layout){
 if(!s||!id||!s->store||!s->dispatcher||!s->dispatcher->control.display||layout<DEMO_LAYOUT_RESULT||layout>DEMO_LAYOUT_COMPARE)return -EINVAL;
 char *text=malloc(DEMO_MAX_JSON+1);struct demo_result *r=calloc(1,sizeof(*r));if(!text||!r){free(text);free(r);return -ENOMEM;}
 size_t bytes=0;int rc=demo_store_read_result(s->store,id,text,DEMO_MAX_JSON+1,&bytes);if(!rc)rc=demo_parse_run(text,bytes,&r->run);if(!rc&&(r->run.state!=DEMO_RESULT_VALIDATED||strcmp(id,r->run.run_id)))rc=-ESTALE;
 if(!rc){demo_sha_hex(text,bytes,r->source_result_sha);rc=s->dispatcher->control.display(s->dispatcher->control.context,r,layout);}
 free(text);free(r);return rc; /* Display failures never start GPU work or alter compute state. */
}

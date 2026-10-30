#define _GNU_SOURCE
#include "kms.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct native_buffer {uint32_t fb,handle;void *map;size_t bytes;};
enum {NATIVE_MAX_ROUTES=32};
struct native {int fd,changed,event,restore_errno;char path[64];uint32_t connector,crtc;drmModeModeInfo mode;drmModeCrtc *old;uint32_t old_routes[NATIVE_MAX_ROUTES];int old_route_count;struct native_buffer buffers[2];uint64_t cookie,event_cookie,event_ns;};
static void native_close(void *p){struct native *n=p;if(n->old){drmModeFreeCrtc(n->old);n->old=NULL;}if(n->fd>=0){drmDropMaster(n->fd);close(n->fd);n->fd=-1;}}
static int native_errno(void){return errno?-errno:-EIO;}
static int native_crtc_index(const drmModeRes *r,uint32_t id){
 for(int i=0;i<r->count_crtcs;i++)if(r->crtcs[i]==id)return i;
 return -1;
}
static int native_select_crtc(struct native *n,const drmModeRes *r,const drmModeConnector *c){
 /* encoder_id describes the current route. encoders[] describes possibilities
    and its first entry can belong to a different, already active CRTC. */
 if(c->encoder_id){
  drmModeEncoder *e=drmModeGetEncoder(n->fd,c->encoder_id);if(!e)return native_errno();
  uint32_t id=e->crtc_id;drmModeFreeEncoder(e);
  if(id){if(native_crtc_index(r,id)<0)return -EPROTO;n->crtc=id;return 0;}
 }
 if(c->count_encoders<0||c->count_encoders>NATIVE_MAX_ROUTES)return -EOVERFLOW;
 for(int j=0;j<c->count_encoders;j++){
  drmModeEncoder *e=drmModeGetEncoder(n->fd,c->encoders[j]);if(!e)return native_errno();
  uint32_t possible=e->possible_crtcs,active=e->crtc_id;drmModeFreeEncoder(e);
  if(active)continue; /* Never move an encoder away from another output. */
  for(int k=0;k<r->count_crtcs;k++)if(possible&(1u<<k)){
   drmModeCrtc *candidate=drmModeGetCrtc(n->fd,r->crtcs[k]);if(!candidate)return native_errno();
   int idle=!candidate->mode_valid&&!candidate->buffer_id;drmModeFreeCrtc(candidate);
   if(idle){n->crtc=r->crtcs[k];return 0;}
  }
 }
 return -EBUSY;
}
static int native_snapshot_routes(struct native *n,const drmModeRes *r){
 n->old_route_count=0;
 for(int i=0;i<r->count_connectors;i++){
  drmModeConnector *c=drmModeGetConnector(n->fd,r->connectors[i]);if(!c)return native_errno();
  uint32_t connector=c->connector_id,encoder=c->encoder_id;drmModeFreeConnector(c);
  if(encoder){
   drmModeEncoder *e=drmModeGetEncoder(n->fd,encoder);if(!e)return native_errno();
   uint32_t crtc=e->crtc_id;drmModeFreeEncoder(e);
   if(crtc==n->crtc){
    /* A clone is another output owner. Refuse before making any modeset. */
    if(connector!=n->connector)return -EBUSY;
    n->old_routes[n->old_route_count++]=connector;
   }
  }
 }
 if(n->old->mode_valid){if(!n->old->buffer_id||n->old_route_count!=1)return -EPROTO;}
 else if(n->old->buffer_id||n->old_route_count)return -EPROTO;
 return 0;
}
static int native_open(void *p,const struct dp_config *cfg,struct dp_caps *caps){
 struct native *n=p;drmModeRes *resources=NULL;drmModeConnector *connector=NULL;uint64_t value=0;int rc=-ENODEV;
 n->fd=open(n->path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(n->fd<0)return -errno;
 if(drmGetCap(n->fd,DRM_CAP_DUMB_BUFFER,&value)||!value){rc=-ENOTSUP;goto fail;}caps->dumb=1;
 if(!drmIsMaster(n->fd)&&drmSetMaster(n->fd)){rc=-errno;goto fail;}caps->master=1;
 value=0;if(!drmGetCap(n->fd,DRM_CAP_TIMESTAMP_MONOTONIC,&value))caps->event_monotonic=value==1;
 resources=drmModeGetResources(n->fd);if(!resources){rc=native_errno();goto fail;}
 if(resources->count_connectors<0||resources->count_connectors>NATIVE_MAX_ROUTES||resources->count_crtcs<0||resources->count_crtcs>NATIVE_MAX_ROUTES){rc=-EOVERFLOW;goto fail;}
 for(int i=0;i<resources->count_connectors;i++){
  connector=drmModeGetConnector(n->fd,resources->connectors[i]);if(!connector)continue;
  if(connector->connector_type!=DRM_MODE_CONNECTOR_DisplayPort||connector->connection!=DRM_MODE_CONNECTED||!connector->count_modes){drmModeFreeConnector(connector);connector=NULL;continue;}
  int mode=-1;
  for(int j=0;j<connector->count_modes;j++){
   const drmModeModeInfo *m=&connector->modes[j];if(!m->hdisplay||!m->vdisplay||m->hdisplay>1920||m->vdisplay>1080)continue;
   /* Bounded local display: progressive <=60Hz, independent of EDID ordering. */
   if(!m->clock||!m->htotal||!m->vtotal||(m->flags&(DRM_MODE_FLAG_INTERLACE|DRM_MODE_FLAG_DBLSCAN))||m->vscan>1)continue;
   if((uint64_t)m->clock*1000>(uint64_t)60*m->htotal*m->vtotal)continue;
   if(mode<0||(m->type&DRM_MODE_TYPE_PREFERRED))mode=j;
   if(m->hdisplay==cfg->width&&m->vdisplay==cfg->height){mode=j;break;}
  }
  if(mode<0){drmModeFreeConnector(connector);connector=NULL;continue;}
  n->mode=connector->modes[mode];n->connector=connector->connector_id;
  n->crtc=0;rc=native_select_crtc(n,resources,connector);if(rc)goto fail;
  if(n->crtc)break;
  drmModeFreeConnector(connector);connector=NULL;
 }
 if(!connector||!n->crtc){rc=-ENODEV;goto fail;}
 n->old=drmModeGetCrtc(n->fd,n->crtc);if(!n->old){rc=native_errno();goto fail;}
 rc=native_snapshot_routes(n,resources);if(rc)goto fail;
 caps->connected=1;caps->width=n->mode.hdisplay;caps->height=n->mode.vdisplay;
 drmModeFreeConnector(connector);drmModeFreeResources(resources);return 0;
fail:
 if(connector)drmModeFreeConnector(connector);
 if(resources)drmModeFreeResources(resources);
 native_close(n);return rc;
}
static int native_create(void *p,uint32_t w,uint32_t h,struct dp_buffer *b){
 struct native *n=p;struct drm_mode_create_dumb create={0};struct drm_mode_map_dumb map={0};struct drm_mode_destroy_dumb destroy={0};uint32_t handles[4]={0},pitches[4]={0},offsets[4]={0};void *memory;uint32_t fb=0;int slot=-1,rc;
 for(int i=0;i<2;i++)if(!n->buffers[i].fb){slot=i;break;}
 if(slot<0)return -ENOSPC;
 create.width=w;create.height=h;create.bpp=32;
 if(drmIoctl(n->fd,DRM_IOCTL_MODE_CREATE_DUMB,&create))return -errno;
 if(create.pitch<(uint64_t)w*4||create.size<(uint64_t)create.pitch*h||create.size>DEMO_MAX_ASSET){rc=-ERANGE;goto fail;}
 handles[0]=create.handle;pitches[0]=create.pitch;
 if(drmModeAddFB2(n->fd,w,h,DRM_FORMAT_ARGB8888,handles,pitches,offsets,&fb,0)){rc=-errno;goto fail;}
 map.handle=create.handle;if(drmIoctl(n->fd,DRM_IOCTL_MODE_MAP_DUMB,&map)){rc=-errno;goto fail;}
 memory=mmap(NULL,(size_t)create.size,PROT_READ|PROT_WRITE,MAP_SHARED,n->fd,(off_t)map.offset);if(memory==MAP_FAILED){rc=-errno;goto fail;}
 n->buffers[slot]=(struct native_buffer){fb,create.handle,memory,(size_t)create.size};*b=(struct dp_buffer){fb,w,h,create.pitch,DP_ARGB8888,(size_t)create.size,memory};return 0;
fail:
 if(fb)drmModeRmFB(n->fd,fb);
 destroy.handle=create.handle;drmIoctl(n->fd,DRM_IOCTL_MODE_DESTROY_DUMB,&destroy);return rc;
}
static int native_initial(void *p,const struct dp_buffer *b){struct native *n=p;n->changed=1;return drmModeSetCrtc(n->fd,n->crtc,b->id,0,0,&n->connector,1,&n->mode)?-errno:0;}
static int native_flip(void *p,const struct dp_buffer *b,uint64_t cookie){struct native *n=p;n->cookie=cookie;n->event=0;return drmModePageFlip(n->fd,n->crtc,b->id,DRM_MODE_PAGE_FLIP_EVENT,n)?-errno:0;}
static void flipped(int fd,unsigned seq,unsigned sec,unsigned usec,void *p){struct native *n=p;(void)fd;(void)seq;n->event_cookie=n->cookie;n->event_ns=(uint64_t)sec*1000000000u+(uint64_t)usec*1000u;n->event++;}
static int native_receive(void *p,int ms,uint64_t *cookie,uint64_t *stamp){
 struct native *n=p;drmModeConnector *connector=drmModeGetConnector(n->fd,n->connector);struct pollfd fd={n->fd,POLLIN,0};int rc;
 if(!connector)return -ENODEV;
 int connected=connector->connection==DRM_MODE_CONNECTED;drmModeFreeConnector(connector);if(!connected)return -ENODEV;
 rc=poll(&fd,1,ms);if(rc<0)return -errno;if(!rc)return -ETIMEDOUT;if(fd.revents&(POLLERR|POLLHUP|POLLNVAL))return -ENODEV;if(!(fd.revents&POLLIN))return -EAGAIN;
 drmEventContext events={0};events.version=DRM_EVENT_CONTEXT_VERSION;events.page_flip_handler=flipped;
 if(drmHandleEvent(n->fd,&events))return -errno;
 if(n->event!=1)return -EPROTO;
 *cookie=n->event_cookie;*stamp=n->event_ns;n->event=0;return 0;
}
static int native_disable(void *p){struct native *n=p;int rc;
 if(!n->changed)return 0;
 /* dp_open may already have tried restoration after an initial modeset error.
    Later process shutdown must return that first error without retrying it. */
 if(n->restore_errno)return n->restore_errno;
 /* Only a successful synchronous restore proves our scanout buffers retired.
    On error keep the snapshot, fd and all mappings pinned for explicit recovery. */
 if(!n->old)return n->restore_errno=-EPROTO;
 if(n->old->mode_valid)
  rc=drmModeSetCrtc(n->fd,n->crtc,n->old->buffer_id,n->old->x,n->old->y,n->old_routes,n->old_route_count,&n->old->mode);
 else rc=drmModeSetCrtc(n->fd,n->crtc,0,0,0,NULL,0,NULL);
 if(rc)return n->restore_errno=native_errno();
 n->changed=0;return 0;
}
static void native_destroy(void *p,struct dp_buffer *b){struct native *n=p;for(int i=0;i<2;i++)if(n->buffers[i].fb==b->id){struct native_buffer *s=&n->buffers[i];struct drm_mode_destroy_dumb d={0};drmModeRmFB(n->fd,s->fb);munmap(s->map,s->bytes);d.handle=s->handle;drmIoctl(n->fd,DRM_IOCTL_MODE_DESTROY_DUMB,&d);memset(s,0,sizeof(*s));break;}}
int dp_native_ops(struct dp_ops *ops,const char *path){
 struct native *n;size_t len;
 if(!ops||!path)return -EINVAL;
 len=strlen(path);if(len!=14||strncmp(path,"/dev/dri/card",13)||path[13]<'0'||path[13]>'9')return -EINVAL;
 n=calloc(1,sizeof(*n));if(!n)return -ENOMEM;n->fd=-1;memcpy(n->path,path,len+1);
 *ops=(struct dp_ops){native_open,native_create,native_initial,native_flip,native_receive,native_disable,native_destroy,native_close,n};return 0;
}
void dp_native_free(struct dp_ops *ops){if(ops&&ops->context){struct native *n=ops->context;if(n->fd>=0)return;free(n);memset(ops,0,sizeof(*ops));}}

/* Exercise actual native KMS routing/restoration with injected libdrm calls.
 * No real /dev/dri open, ioctl, modeset or physical DP operation is performed. */
#define _GNU_SOURCE
#include "../../viewer/kms.h"
#define open restore_test_open
#define close restore_test_close
#define mmap restore_test_mmap
#define munmap restore_test_munmap
#include "../../viewer/kms.c"
#undef open
#undef close
#undef mmap
#undef munmap
#include <assert.h>
#include <stdio.h>
#include "../../viewer/viewer.c"
/* Submission is outside these open/close tests; reaching it is a test error. */
int demo_frame_check(const struct demo_frame *f,int canvas){(void)f;(void)canvas;abort();}
void demo_sha_hex(const void *p,size_t size,char out[65]){(void)p;(void)size;(void)out;abort();}

static struct {
 int high_refresh_first;
 int disabled,missing_connector,restore_error,initial_error,cloned,wrong_encoder_first,inactive_dp;
 int closed,freed_crtc,modesets,created,destroyed;
 uint32_t last_crtc,last_fb,last_x,last_y,routes[32];
 int route_count,has_mode;
 drmModeModeInfo last_mode;
} fixture;

int restore_test_open(const char *path,int flags,...){
 assert(!strcmp(path,"/dev/dri/card0"));assert(flags&O_NOFOLLOW);return 19;
}
int restore_test_close(int fd){assert(fd==19);fixture.closed++;return 0;}
int drmGetCap(int fd,uint64_t cap,uint64_t *value){
 assert(fd==19);assert(cap==DRM_CAP_DUMB_BUFFER||cap==DRM_CAP_TIMESTAMP_MONOTONIC);*value=1;return 0;
}
int drmIsMaster(int fd){assert(fd==19);return 1;}
int drmSetMaster(int fd){assert(fd==19);return 0;}
int drmDropMaster(int fd){assert(fd==19);return 0;}
drmModeResPtr drmModeGetResources(int fd){
 assert(fd==19);drmModeResPtr r=calloc(1,sizeof(*r));assert(r);
 r->count_crtcs=2;r->crtcs=calloc(2,sizeof(uint32_t));assert(r->crtcs);r->crtcs[0]=33;r->crtcs[1]=44;
 r->count_connectors=2;r->connectors=calloc(2,sizeof(uint32_t));assert(r->connectors);r->connectors[0]=11;r->connectors[1]=12;return r;
}
void drmModeFreeResources(drmModeResPtr r){free(r->crtcs);free(r->connectors);free(r);}
drmModeConnectorPtr drmModeGetConnector(int fd,uint32_t id){
 assert(fd==19);assert(id==11||id==12);
 if(fixture.missing_connector&&id==12){errno=EIO;return NULL;}
 drmModeConnectorPtr c=calloc(1,sizeof(*c));assert(c);c->connector_id=id;
 c->connector_type=id==11?DRM_MODE_CONNECTOR_DisplayPort:DRM_MODE_CONNECTOR_HDMIA;
 c->connection=DRM_MODE_CONNECTED;c->encoder_id=fixture.disabled?0:(id==11?21:22);
 if(id==11&&fixture.inactive_dp)c->encoder_id=0;
 if(id==12&&!fixture.cloned&&!fixture.inactive_dp)c->encoder_id=0;
 c->count_modes=fixture.high_refresh_first?2:1;c->modes=calloc((size_t)c->count_modes,sizeof(*c->modes));assert(c->modes);
 c->modes[0].hdisplay=1280;c->modes[0].vdisplay=720;c->modes[0].type=DRM_MODE_TYPE_PREFERRED;c->modes[0].clock=74250;c->modes[0].htotal=1650;c->modes[0].vtotal=750;c->modes[0].vrefresh=60;
 if(fixture.high_refresh_first){c->modes[0].hdisplay=1920;c->modes[0].vdisplay=1080;c->modes[0].clock=358000;c->modes[0].vrefresh=144;c->modes[0].htotal=2200;c->modes[0].vtotal=1130;c->modes[1]=c->modes[0];c->modes[1].clock=148500;c->modes[1].vrefresh=60;c->modes[1].vtotal=1125;}
 c->count_encoders=fixture.wrong_encoder_first&&id==11?2:1;
 c->encoders=calloc((size_t)c->count_encoders,sizeof(uint32_t));assert(c->encoders);
 c->encoders[0]=id==11?21:22;
 if(c->count_encoders==2){c->encoders[0]=23;c->encoders[1]=21;}
 return c;
}
void drmModeFreeConnector(drmModeConnectorPtr c){free(c->modes);free(c->encoders);free(c);}
drmModeEncoderPtr drmModeGetEncoder(int fd,uint32_t id){
 assert(fd==19);assert(id==21||id==22||id==23);drmModeEncoderPtr e=calloc(1,sizeof(*e));assert(e);
 e->encoder_id=id;e->possible_crtcs=3;e->crtc_id=fixture.disabled?0:(id==23?44:33);return e;
}
void drmModeFreeEncoder(drmModeEncoderPtr e){free(e);}
drmModeCrtcPtr drmModeGetCrtc(int fd,uint32_t id){
 assert(fd==19);assert(id==33||id==44);drmModeCrtcPtr c=calloc(1,sizeof(*c));assert(c);c->crtc_id=id;
 if(!fixture.disabled&&!(fixture.inactive_dp&&id==44)){c->buffer_id=77;c->x=7;c->y=9;c->mode_valid=1;c->mode.hdisplay=800;c->mode.vdisplay=600;memcpy(c->mode.name,"original-mode",14);}
 return c;
}
void drmModeFreeCrtc(drmModeCrtcPtr c){fixture.freed_crtc++;free(c);}
int drmModeSetCrtc(int fd,uint32_t crtc,uint32_t fb,uint32_t x,uint32_t y,uint32_t *routes,int count,drmModeModeInfoPtr mode){
 assert(fd==19);assert(count>=0&&count<=32);fixture.modesets++;fixture.last_crtc=crtc;fixture.last_fb=fb;fixture.last_x=x;fixture.last_y=y;
 fixture.route_count=count;fixture.has_mode=mode!=NULL;if(count)memcpy(fixture.routes,routes,(size_t)count*sizeof(*routes));if(mode)fixture.last_mode=*mode;
 if((fixture.restore_error&&fb!=99)||(fixture.initial_error&&fb==99)){errno=EIO;return -1;}return 0;
}
static int fake_create(void *p,uint32_t w,uint32_t h,struct dp_buffer *b){
 (void)p;*b=(struct dp_buffer){99+(uint32_t)fixture.created++,w,h,4*w,DP_XRGB8888,(size_t)4*w*h,NULL};
 b->map=calloc(1,b->bytes);assert(b->map);return 0;
}
static void fake_destroy(void *p,struct dp_buffer *b){(void)p;fixture.destroyed++;free(b->map);}
static void setup(struct native *n){memset(n,0,sizeof(*n));n->fd=-1;memcpy(n->path,"/dev/dri/card0",15);}
static void opened(struct native *n){struct dp_config cfg={1280,720};struct dp_caps caps={0};assert(!native_open(n,&cfg,&caps));assert(caps.connected&&caps.master&&caps.dumb);}
static void changed(struct native *n){struct dp_buffer b={0};b.id=99;assert(!native_initial(n,&b));assert(n->changed&&fixture.last_fb==99);}

int drmIoctl(int fd,unsigned long request,void *arg){
 assert(fd==19);
 if(request==DRM_IOCTL_MODE_CREATE_DUMB){struct drm_mode_create_dumb *d=arg;d->handle=5;d->pitch=d->width*4;d->size=(uint64_t)d->pitch*d->height;return 0;}
 if(request==DRM_IOCTL_MODE_MAP_DUMB){((struct drm_mode_map_dumb *)arg)->offset=0;return 0;}
 assert(request==DRM_IOCTL_MODE_DESTROY_DUMB);return 0;
}
int drmModeAddFB2(int fd,uint32_t w,uint32_t h,uint32_t format,const uint32_t handles[4],const uint32_t pitches[4],const uint32_t offsets[4],uint32_t *id,uint32_t flags){
 (void)w;(void)h;(void)handles;(void)pitches;(void)offsets;(void)flags;assert(fd==19);
 if(format!=DRM_FORMAT_ARGB8888){errno=EINVAL;return -1;}*id=88;return 0;
}
int drmModeRmFB(int fd,uint32_t id){assert(fd==19&&id==88);return 0;}
void *restore_test_mmap(void *a,size_t bytes,int prot,int flags,int fd,off_t offset){(void)a;(void)prot;(void)flags;(void)offset;assert(fd==19);return calloc(1,bytes);}
int restore_test_munmap(void *a,size_t bytes){(void)bytes;free(a);return 0;}

int main(void){
 struct native format_native;memset(&fixture,0,sizeof(fixture));setup(&format_native);opened(&format_native);struct dp_buffer real_buffer={0};assert(!native_create(&format_native,1280,720,&real_buffer));assert(real_buffer.format==DRM_FORMAT_ARGB8888);native_destroy(&format_native,&real_buffer);native_close(&format_native);
 memset(&fixture,0,sizeof(fixture));fixture.high_refresh_first=1;setup(&format_native);struct dp_config safe_cfg={1920,1080};struct dp_caps safe_caps={0};assert(!native_open(&format_native,&safe_cfg,&safe_caps));assert(format_native.mode.clock==148500&&format_native.mode.vrefresh==60);native_close(&format_native);
 struct native n;
 /* RED: the previous code disabled the CRTC instead of restoring old fb. */
 memset(&fixture,0,sizeof(fixture));setup(&n);opened(&n);changed(&n);
 assert(!native_disable(&n));assert(fixture.last_fb==77&&fixture.last_x==7&&fixture.last_y==9);
 assert(fixture.last_crtc==33&&fixture.route_count==1&&fixture.routes[0]==11);
 assert(fixture.has_mode&&fixture.last_mode.hdisplay==800&&fixture.last_mode.vdisplay==600&&!strcmp(fixture.last_mode.name,"original-mode"));
 assert(!n.changed);native_close(&n);assert(fixture.closed==1&&fixture.freed_crtc==1);

 memset(&fixture,0,sizeof(fixture));fixture.disabled=1;setup(&n);opened(&n);changed(&n);
 assert(!native_disable(&n));assert(fixture.last_fb==0&&fixture.route_count==0&&!fixture.has_mode);native_close(&n);

 memset(&fixture,0,sizeof(fixture));fixture.wrong_encoder_first=1;setup(&n);opened(&n);assert(n.crtc==33);changed(&n);assert(!native_disable(&n));native_close(&n);

 /* Incomplete topology cannot be guessed or modified. */
 memset(&fixture,0,sizeof(fixture));fixture.missing_connector=1;setup(&n);
 struct dp_config cfg={1280,720};struct dp_caps caps={0};assert(native_open(&n,&cfg,&caps)==-EIO);assert(!fixture.modesets&&n.fd==-1);

 /* Do not take another output's clone route; exclusivity is required. */
 memset(&fixture,0,sizeof(fixture));fixture.cloned=1;setup(&n);assert(native_open(&n,&cfg,&caps)==-EBUSY);assert(!fixture.modesets&&n.fd==-1);

 /* An inactive DP must not reuse an encoder already routing another output,
    even when that encoder also advertises a second, free CRTC. */
 memset(&fixture,0,sizeof(fixture));fixture.inactive_dp=1;setup(&n);
 assert(native_open(&n,&cfg,&caps)==-EBUSY);assert(!fixture.modesets&&n.fd==-1);

 memset(&fixture,0,sizeof(fixture));setup(&n);opened(&n);changed(&n);fixture.restore_error=1;
 assert(native_disable(&n)==-EIO);assert(n.changed&&n.fd==19&&n.old&&!fixture.closed&&!fixture.freed_crtc);
 int previous_modesets=fixture.modesets;
 /* Test-fixture teardown only: no real scanout exists, no recovery API implied. */
 native_close(&n);

 /* Real dp_open calls dp_close after initial failure. Shutdown must not cause
    a second restore after that first restore has also failed. */
 memset(&fixture,0,sizeof(fixture));fixture.initial_error=fixture.restore_error=1;setup(&n);
 struct dp_ops o={native_open,fake_create,native_initial,native_flip,native_receive,native_disable,fake_destroy,native_close,&n};
 struct dp_context context={0};assert(dp_open(&context,&cfg,&o)==-EIO);
 assert(context.opened&&context.quarantined&&n.changed&&fixture.modesets==2&&!fixture.destroyed&&!fixture.closed);
 previous_modesets=fixture.modesets;fixture.initial_error=fixture.restore_error=0;
 assert(dp_close(&context)==-EIO);assert(fixture.modesets==previous_modesets);
 assert(context.opened&&context.quarantined&&context.buffers[0].map&&context.buffers[1].map&&!fixture.destroyed&&!fixture.closed);
 /* Free only the virtual fixture after proving production leaves it pinned. */
 for(int i=0;i<context.created;i++)free(context.buffers[i].map);
 native_close(&n);
 puts("NATIVE_KMS_ORIGINAL_STATE_RESTORED_FAKE_ONLY_NOT_PHYSICAL_DP");return 0;
}

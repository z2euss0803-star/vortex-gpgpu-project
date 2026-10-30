#define _POSIX_C_SOURCE 200809L
#include "kms.h"
#include <errno.h>
#include <string.h>
#include <time.h>
uint64_t dp_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
int dp_close(struct dp_context *c){
 int rc;
 if(!c)return -EINVAL;
 if(!c->opened)return 0;
 if(c->active>=0){rc=c->ops.disable(c->ops.context);if(rc){c->quarantined=1;return rc;}}
 for(int i=0;i<c->created;i++)c->ops.destroy(c->ops.context,&c->buffers[i]);
 c->ops.close(c->ops.context);memset(c,0,sizeof(*c));c->active=-1;return 0;
}
int dp_open(struct dp_context *c,const struct dp_config *cfg,const struct dp_ops *o){
 int rc;
 if(!c||!cfg||!o||!o->open||!o->create||!o->initial||!o->flip||!o->receive||!o->disable||!o->destroy||!o->close||c->opened)return -EINVAL;
 memset(c,0,sizeof(*c));c->ops=*o;c->active=-1;
 rc=o->open(o->context,cfg,&c->caps);if(rc)return rc;
 c->opened=1;
 if(!c->caps.dumb||!c->caps.master||!c->caps.connected||!c->caps.width||!c->caps.height||c->caps.width>1920||c->caps.height>1080){dp_close(c);return -ENOTSUP;}
 for(int i=0;i<2;i++){
  struct dp_buffer *b=&c->buffers[i];
  rc=o->create(o->context,c->caps.width,c->caps.height,b);if(rc){dp_close(c);return rc;}c->created++;
  if(!b->map||b->width!=c->caps.width||b->height!=c->caps.height||(b->format!=DP_XRGB8888&&b->format!=DP_ARGB8888)||b->pitch<(uint64_t)b->width*4||b->bytes<(uint64_t)b->pitch*b->height||b->bytes>DEMO_MAX_ASSET){dp_close(c);return -ERANGE;}
  memset(b->map,0,b->bytes);
 }
 c->active=0;rc=o->initial(o->context,&c->buffers[0]);if(rc){c->quarantined=1;dp_close(c);return rc;}
 return 0;
}
static void rgb_pixel(const uint8_t*s,uint8_t*d){d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=255;}
int dp_copy_rgb24(const struct demo_frame*f,struct dp_buffer*b,struct dp_copy_stats*stats){
 uint32_t fitw,fith,left,top,xmap[1920];struct dp_copy_stats local={0};
 if(stats)memset(stats,0,sizeof(*stats));
 if(demo_frame_check(f,1)||!b||!b->map||!b->width||!b->height||b->width>1920||b->height>1080||(b->format!=DP_XRGB8888&&b->format!=DP_ARGB8888)||b->pitch<(uint64_t)b->width*4||b->bytes<(uint64_t)b->pitch*b->height||b->bytes>DEMO_MAX_ASSET)return -EINVAL;
 {uintptr_t a=(uintptr_t)f->pixels,d=(uintptr_t)b->map;if(a<=d?d-a<f->bytes:a-d<b->bytes)return -EINVAL;}
 local.identity=f->width==b->width&&f->height==b->height;
 if(local.identity){
  for(uint32_t y=0;y<b->height;++y){const uint8_t*src=f->pixels+(size_t)y*f->stride;uint8_t*dst=b->map+(size_t)y*b->pitch;uint32_t x=0;
   for(;x+4<=b->width;x+=4){rgb_pixel(src+3*x,dst+4*x);rgb_pixel(src+3*x+3,dst+4*x+4);rgb_pixel(src+3*x+6,dst+4*x+8);rgb_pixel(src+3*x+9,dst+4*x+12);}
   for(;x<b->width;++x)rgb_pixel(src+3*x,dst+4*x);
   memset(dst+(size_t)b->width*4,0,b->pitch-(size_t)b->width*4);
  }
  memset(b->map+(size_t)b->pitch*b->height,0,b->bytes-(size_t)b->pitch*b->height);
  local.source_bytes=(uint64_t)b->width*b->height*3;local.pixel_bytes=(uint64_t)b->width*b->height*4;local.clear_bytes=b->bytes-local.pixel_bytes;
 }else{
  fitw=b->width;fith=(uint32_t)((uint64_t)f->height*fitw/f->width);if(fith>b->height){fith=b->height;fitw=(uint32_t)((uint64_t)f->width*fith/f->height);}if(!fitw)fitw=1;if(!fith)fith=1;
  left=(b->width-fitw)/2;top=(b->height-fith)/2;memset(b->map,0,b->bytes);
  for(uint32_t x=0;x<fitw;++x)xmap[x]=(uint32_t)((uint64_t)x*f->width/fitw)*3;
  for(uint32_t y=0;y<fith;++y){const uint8_t*row=f->pixels+(size_t)((uint64_t)y*f->height/fith)*f->stride;uint8_t*dest=b->map+(size_t)(y+top)*b->pitch+4*left;for(uint32_t x=0;x<fitw;++x)rgb_pixel(row+xmap[x],dest+x*4);}
  local.source_bytes=(uint64_t)fitw*fith*3;local.pixel_bytes=(uint64_t)fitw*fith*4;local.clear_bytes=b->bytes;
 }
 if(stats)*stats=local;
 return 0;
}
static int submit_frame(struct dp_context *c,const struct demo_frame *f,int immutable_rgb){
 int target,rc;char sha[65];struct dp_buffer *b;struct dp_event *e;struct dp_copy_stats conversion;uint64_t convert_begin,convert_end;
 if(!c||!c->opened||c->quarantined)return -EIO;
 if(c->pending||c->retired_event)return -EBUSY;
 if(demo_frame_check(f,1)||strlen(f->run_id)!=32||strlen(f->source_result_sha)!=64||strlen(f->sha256)!=64)return -EINVAL;
 if(!immutable_rgb){demo_sha_hex(f->pixels,f->bytes,sha);if(strcmp(sha,f->sha256))return -EBADMSG;}
 if(c->next_cookie==UINT64_MAX)return -EOVERFLOW;
 target=1-c->active;b=&c->buffers[target];
 convert_begin=dp_now();rc=dp_copy_rgb24(f,b,&conversion);convert_end=dp_now();if(rc)return rc;
 e=&c->frames[target];memset(e,0,sizeof(*e));e->cookie=++c->next_cookie;e->kind=DP_SUBMITTED;e->received_ns=dp_now();e->conversion=conversion;e->conversion_ns=convert_begin&&convert_end>=convert_begin?convert_end-convert_begin:0;
 memcpy(e->run_id,f->run_id,sizeof(e->run_id));memcpy(e->frame_sha,f->sha256,sizeof(e->frame_sha));
 demo_sha_hex(b->map,b->bytes,e->scanout_sha);e->width=b->width;e->height=b->height;e->pitch=b->pitch;
 rc=c->ops.flip(c->ops.context,b,e->cookie);if(rc){c->quarantined=1;return rc;}c->pending=target+1;return 0;
}
int dp_submit(struct dp_context*c,const struct demo_frame*f){return submit_frame(c,f,0);}
int dp_submit_prevalidated_immutable(struct dp_context*c,const struct demo_frame*f){return submit_frame(c,f,1);}
int dp_poll(struct dp_context *c,int ms,struct dp_event *event){
 uint64_t cookie=0,stamp=0;int target,rc;struct dp_event *e;
 if(!c||!event||!c->opened||ms<0)return -EINVAL;
 memset(event,0,sizeof(*event));
 if(c->quarantined)return -EIO;
 if(c->retired_event){*event=c->retired;c->retired_event=0;return 0;}
 if(!c->pending)return -EAGAIN;
 target=c->pending-1;e=&c->frames[target];
 if(e->kind==DP_SUBMITTED){*event=*e;e->kind=0;return 0;}
 rc=c->ops.receive(c->ops.context,ms,&cookie,&stamp);
 if(rc==-EINTR||rc==-EAGAIN)return rc;
 if(!rc&&cookie!=e->cookie)rc=-EPROTO;
 if(rc){c->quarantined=1;*event=*e;event->kind=DP_FAILED;event->received_ns=dp_now();return rc;}
 c->retired=c->frames[c->active];c->retired.kind=DP_RETIRED;c->retired.received_ns=dp_now();c->retired_event=1;
 c->active=target;c->pending=0;*event=*e;event->kind=DP_PRESENTED;event->received_ns=dp_now();event->kernel_ns=stamp;event->kernel_clock_known=c->caps.event_monotonic;
 return 0;
}

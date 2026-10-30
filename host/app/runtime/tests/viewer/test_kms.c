#include "../../viewer/kms.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct fake {int cap,master,connected,created,destroyed,closed,scan,flip,receive_rc,disable_rc;uint64_t cookie,event_cookie;};
static int op_open(void *p,const struct dp_config *q,struct dp_caps *caps){struct fake *f=p;(void)q;*caps=(struct dp_caps){3,2,f->cap,f->master,f->connected,0};return 0;}
static int create(void *p,uint32_t w,uint32_t h,struct dp_buffer *b){struct fake *f=p;*b=(struct dp_buffer){++f->created,w,h,16,DP_XRGB8888,32,calloc(1,32)};return 0;}
static int initial(void *p,const struct dp_buffer *b){((struct fake *)p)->scan=b->id;return 0;}
static int flip(void *p,const struct dp_buffer *b,uint64_t cookie){struct fake *f=p;f->flip=b->id;f->cookie=cookie;return 0;}
static int receive(void *p,int ms,uint64_t *cookie,uint64_t *stamp){struct fake *f=p;(void)ms;if(f->receive_rc)return f->receive_rc;*cookie=f->event_cookie?f->event_cookie:f->cookie;*stamp=1234;f->scan=f->flip;return 0;}
static int disable(void *p){struct fake *f=p;if(f->disable_rc)return f->disable_rc;f->scan=f->flip=0;return 0;}
static void destroy(void *p,struct dp_buffer *b){struct fake *f=p;assert(f->scan!=(int)b->id&&f->flip!=(int)b->id);f->destroyed++;free(b->map);}
static void close_op(void *p){((struct fake *)p)->closed++;}
static struct dp_ops ops(struct fake *f){return (struct dp_ops){op_open,create,initial,flip,receive,disable,destroy,close_op,f};}
int main(void){
 struct fake f={.cap=0,.master=1,.connected=1};struct dp_ops o=ops(&f);struct dp_context c={0};struct dp_config cfg={1280,720};struct demo_frame frame={0};struct dp_event e;
 assert(dp_open(&c,&cfg,&o)==-ENOTSUP);assert(!f.created&&f.closed==1);
 f.cap=1;f.master=0;assert(dp_open(&c,&cfg,&o)==-ENOTSUP);assert(!f.created);
 f.master=1;assert(!dp_open(&c,&cfg,&o));assert(f.created==2&&f.scan==1);
 assert(!demo_frame_alloc(3,2,0,&frame));for(unsigned i=0;i<6;i++){frame.pixels[3*i]=255;frame.pixels[3*i+1]=17;frame.pixels[3*i+2]=4;}
 memset(frame.run_id,'1',32);memset(frame.source_result_sha,'a',64);assert(!demo_frame_hash(&frame));
 assert(!dp_submit(&c,&frame));assert(dp_submit(&c,&frame)==-EBUSY);assert(c.buffers[0].map[0]==0);
 assert(c.buffers[1].map[0]==4&&c.buffers[1].map[1]==17&&c.buffers[1].map[2]==255&&c.buffers[1].map[3]==255);assert(c.buffers[1].map[12]==0&&c.buffers[1].map[16]==4);
 assert(!dp_poll(&c,1,&e)&&e.kind==DP_SUBMITTED);f.receive_rc=-EINTR;assert(dp_poll(&c,1,&e)==-EINTR&&!c.quarantined);f.receive_rc=0;
 assert(!dp_poll(&c,1,&e)&&e.kind==DP_PRESENTED&&!e.kernel_clock_known&&e.kernel_ns==1234);assert(!strcmp(e.run_id,frame.run_id));
 assert(!dp_poll(&c,1,&e)&&e.kind==DP_RETIRED);assert(!dp_submit(&c,&frame));assert(!dp_poll(&c,1,&e)&&e.kind==DP_SUBMITTED);
 f.receive_rc=-ETIMEDOUT;assert(dp_poll(&c,1,&e)==-ETIMEDOUT&&e.kind==DP_FAILED&&c.quarantined);assert(dp_submit(&c,&frame)<0);
 f.disable_rc=-EIO;assert(dp_close(&c)==-EIO&&f.destroyed==0&&f.closed==2);f.disable_rc=0;assert(!dp_close(&c)&&f.destroyed==2&&f.closed==3);
 memset(&f,0,sizeof(f));f.cap=f.master=f.connected=1;o=ops(&f);assert(!dp_open(&c,&cfg,&o));assert(!dp_submit(&c,&frame));assert(!dp_poll(&c,1,&e));f.event_cookie=99;assert(dp_poll(&c,1,&e)==-EPROTO&&c.quarantined);assert(!dp_close(&c));
 demo_frame_free(&frame);assert(!demo_frame_alloc(1,1,0,&frame));frame.pixels[0]=255;memset(frame.run_id,'1',32);memset(frame.source_result_sha,'a',64);assert(!demo_frame_hash(&frame));
 memset(&f,0,sizeof(f));f.cap=f.master=f.connected=1;o=ops(&f);assert(!dp_open(&c,&cfg,&o));assert(!dp_submit(&c,&frame));assert(c.buffers[1].map[2]==255&&c.buffers[1].map[6]==255&&c.buffers[1].map[10]==0);assert(!dp_close(&c));
 demo_frame_free(&frame);puts("DRM_FAKE_LIFECYCLE_PASS_NOT_PHYSICAL_DP");return 0;
}

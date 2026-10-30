#define _GNU_SOURCE
#include "../../viewer/kms.h"
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct fake {int open_rc,flip_rc,cap,master,connected,created,destroyed,closed,scan,flip,receive_rc,disable_rc;uint64_t cookie,event_cookie;};
static int op_open(void *p,const struct dp_config *q,struct dp_caps *caps){struct fake *f=p;(void)q;if(f->open_rc)return f->open_rc;*caps=(struct dp_caps){3,2,f->cap,f->master,f->connected,0};return 0;}
static int create(void *p,uint32_t w,uint32_t h,struct dp_buffer *b){struct fake *f=p;*b=(struct dp_buffer){++f->created,w,h,16,DP_XRGB8888,32,calloc(1,32)};return 0;}
static int initial(void *p,const struct dp_buffer *b){((struct fake *)p)->scan=b->id;return 0;}
static int flip(void *p,const struct dp_buffer *b,uint64_t cookie){struct fake *f=p;f->flip=b->id;f->cookie=cookie;return f->flip_rc;}
static int receive(void *p,int ms,uint64_t *cookie,uint64_t *stamp){struct fake *f=p;(void)ms;if(f->receive_rc)return f->receive_rc;*cookie=f->event_cookie?f->event_cookie:f->cookie;*stamp=1234;f->scan=f->flip;return 0;}
static int disable(void *p){struct fake *f=p;if(f->disable_rc)return f->disable_rc;f->scan=f->flip=0;return 0;}
static void destroy(void *p,struct dp_buffer *b){struct fake *f=p;assert(f->scan!=(int)b->id&&f->flip!=(int)b->id);f->destroyed++;free(b->map);}
static void close_op(void *p){((struct fake *)p)->closed++;}
static struct dp_ops ops(struct fake *f){return (struct dp_ops){op_open,create,initial,flip,receive,disable,destroy,close_op,f};}
static int disconnected(void *p,const struct dp_event *e){(void)p;(void)e;return -EPIPE;}
int main(void){
 char path[]="/tmp/demo-present-XXXXXX";assert(mkdtemp(path));struct demo_store store;
 assert(!demo_store_open(&store,path,NULL));struct demo_frame frame={0};
 assert(!demo_frame_alloc(3,2,0,&frame));memset(frame.run_id,'1',32);memset(frame.source_result_sha,'a',64);assert(!demo_frame_hash(&frame));
 int expected[]={-EBUSY,-ENOTSUP,-EIO,-EPIPE};
 for(unsigned i=0;i<4;i++){
  struct fake f={.cap=1,.master=1,.connected=1};struct dp_ops o=ops(&f);struct dp_context c={0};
  if(i==0)f.open_rc=-EBUSY;
  if(i==1)f.cap=0;
  if(i==2)f.flip_rc=-EIO;
  uint64_t previous=store.sequence;
  assert(dp_present_frame(&c,&o,&store,&frame,i==3?disconnected:NULL,NULL)==expected[i]);
  assert(store.sequence>previous);
  assert(!dp_close(&c));
 }
 char run[256];snprintf(run,sizeof(run),"%s/%s",path,frame.run_id);DIR *dir=opendir(run);assert(dir);unsigned errors=0;struct dirent *e;
 while((e=readdir(dir)))if(!strncmp(e->d_name,"error-display-",14))errors++;
 closedir(dir);assert(errors==4);
 demo_frame_free(&frame);demo_store_close(&store);puts("DISPLAY_ERROR_RECEIPTS_HOST_PASS_NOT_DP");return 0;
}

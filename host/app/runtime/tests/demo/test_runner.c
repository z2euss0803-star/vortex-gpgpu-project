#include <vxl/demo_runner.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>
struct gpu {int opens,starts,closes,held,poll_result,drained;struct demo_result *provided;};
static int owner(void *p){struct gpu *g=p;if(g->held)return -EBUSY;g->held=1;g->opens++;return 101;}
static int close_owner(void *p,int fd){struct gpu *g=p;assert(fd==101);g->held=0;g->closes++;return 0;}
static int start(void *p,int fd,const struct demo_run *r,uint64_t *job){struct gpu *g=p;assert(fd==101&&r->app==DEMO_MOSAIC);g->starts++;*job=7;return 0;}
static int poll(void *p,int fd,uint64_t job){assert(fd==101&&job==7);return ((struct gpu *)p)->poll_result;}
static int drained(void *p,int fd){assert(fd==101);return ((struct gpu *)p)->drained;}
static int collect(void *p,int fd,uint64_t j,struct demo_result *r){struct gpu *g=p;assert(fd==101&&j==7);if(!g->provided)return -ENOSYS;*r=*g->provided;return 0;}
int main(int argc,char **argv){
 struct demo_session *s=calloc(1,sizeof(*s)),*second=calloc(1,sizeof(*second));
 struct demo_result *status=calloc(1,sizeof(*status));struct demo_run *r=calloc(1,sizeof(*r));
 struct gpu g={0};struct demo_gpu_ops ops={owner,close_owner,start,poll,drained,collect,&g};
 struct demo_result *provided=calloc(1,sizeof(*provided));
 FILE *f;char *text=calloc(1,DEMO_MAX_JSON+1);size_t n;
 assert(argc==2&&s&&second&&status&&r&&text&&provided);f=fopen(argv[1],"rb");assert(f);n=fread(text,1,DEMO_MAX_JSON,f);fclose(f);assert(!demo_parse_run(text,n,r));provided->run=*r;
 {struct json_object *o=json_tokener_parse(text);const char *v;
  json_object_object_add(o,"state",json_object_new_string("STAGED"));v=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(v,strlen(v),r));json_object_put(o);}
 assert(!demo_session_init(s,NULL,1,&ops));assert(s->owner_fd==-1&&g.opens==0);
 assert(!demo_session_bind_identity(s,"CURRENT-BOOT","CURRENT-BUILD"));
 assert(demo_start(s,r)==-ESTALE&&g.opens==0&&g.starts==0&&s->state==0&&!s->request.run_id[0]);
 {struct json_object *o=json_tokener_parse(r->json);const char *v;
  json_object_object_add(o,"boot_id",json_object_new_string("CURRENT-BOOT"));
  v=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(v,strlen(v),r));
  assert(demo_start(s,r)==-ESTALE&&!g.opens&&!g.starts&&!s->request.run_id[0]);
  json_object_object_add(o,"build_id",json_object_new_string("CURRENT-BUILD"));
  v=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(v,strlen(v),r));json_object_put(o);}
 provided->run=*r;provided->run.state=DEMO_RESULT_VALIDATED;
 {struct json_object *o=json_tokener_parse(r->json);json_object_object_add(o,"state",json_object_new_string("RESULT_VALIDATED"));const char *v=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(v,strlen(v),&provided->run));json_object_put(o);}
 assert(!demo_enqueue(s,r)&&s->state==DEMO_STAGED&&!g.opens&&!g.starts);
 assert(!demo_enqueue(s,r)&&!g.starts);
 assert(!demo_start(s,r));assert(g.starts==1&&g.opens==1&&s->state==DEMO_RUNNING);
 /* A reconnect to the identical run cannot launch it again. */
 assert(!demo_start(s,r));assert(g.starts==1&&g.opens==1);
 assert(!demo_session_init(second,NULL,1,&ops));assert(demo_owner_acquire(second)==-EBUSY);
 r->generation=2;assert(demo_start(s,r)==-ESTALE);r->generation=1;
 g.poll_result=-ETIMEDOUT;
 assert(demo_status(s,r->run_id,status)==-ETIMEDOUT);
 assert(s->quarantined&&s->owner_fd==101&&g.closes==0);
 assert(demo_owner_release(s)==-EBUSY);assert(g.closes==0);
 assert(demo_start(s,r)==-EBUSY&&g.starts==1);
 g.drained=1;assert(!demo_owner_release(s));assert(g.closes==1);
 assert(!demo_session_init(s,NULL,1,&ops));g.poll_result=1;g.provided=provided;
 assert(!demo_start(s,r));assert(g.opens==2&&g.starts==2);
 assert(!demo_status(s,r->run_id,status));assert(s->state==DEMO_RESULT_VALIDATED);
 assert(!strcmp(status->run.run_id,r->run_id));assert(s->owner_fd==101&&g.closes==1);
 /* A later request borrows the same owner. */
 {struct json_object *o=json_tokener_parse(r->json);const char *v;
  json_object_object_add(o,"run_id",json_object_new_string("22222222222222222222222222222222"));v=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(v,strlen(v),r));json_object_put(o);}
 assert(!demo_start(s,r));assert(g.opens==2&&g.starts==3);
 /* Stale collected output cannot publish as a new run. */
 assert(demo_status(s,r->run_id,status)==-ESTALE);assert(s->quarantined);
 assert(!demo_owner_release(s));
 /* An explicit retry after a pre-start owner refusal must actually start,
    and subsequent identical retries must not duplicate submission. */
 assert(!demo_session_init(second,NULL,1,&ops));g.held=1;
 assert(demo_start(second,r)==-EBUSY&&second->state==DEMO_STAGED);
 {int previous=g.starts;g.held=0;assert(!demo_start(second,r));
  assert(second->state==DEMO_RUNNING&&g.starts==previous+1);
  assert(!demo_start(second,r)&&g.starts==previous+1);}
 assert(!demo_owner_release(second));
 free(s);free(second);free(status);free(r);free(text);free(provided);
 puts("DEMO_RUNNER_OWNER_AND_TIMEOUT_HOST_PASS_NOT_BOARD");return 0;
}

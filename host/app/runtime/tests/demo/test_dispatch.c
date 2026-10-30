#define _GNU_SOURCE
#include <vxl/demo_dispatch.h>
#include <json-c/json.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct gpu{int app,starts,unloads,ready,objects;struct demo_result *result;};
struct control{int opens,closes,selects,displays,fail_select;vxl_u32 profile,gen;struct gpu *apps;};
static int open_owner(void *p){struct control *c=p;++c->opens;++c->gen;return 55;}
static int close_owner(void *p,int fd){assert(fd==55);++((struct control*)p)->closes;return 0;}
static int info(void *p,int fd,struct vxl_request *r){struct control *c=p;assert(fd==55);r->profile_id=c->profile;r->generation=c->gen;r->state=VXL_READY;r->used=c->apps[0].objects+c->apps[1].objects;return 0;}
static int select_profile(void *p,int fd,vxl_u32 target,vxl_u32 gen,vxl_u32 *out){struct control *c=p;assert(fd==55&&gen==c->gen&&!c->apps[0].objects&&!c->apps[1].objects);if(c->fail_select)return -ENOMEM;if(c->profile!=target){++c->gen;++c->selects;c->profile=target;}*out=c->gen;return 0;}
static int start(void *p,int fd,const struct demo_run *run,uint64_t *job){struct gpu *g=p;assert(fd==55&&g->app==(int)run->app);++g->starts;g->objects=4;*job=g->starts;return 0;}
static int poll(void *p,int fd,uint64_t job){struct gpu *g=p;assert(fd==55&&job==(uint64_t)g->starts);return g->ready;}
static int quiet(void *p,int fd){assert(fd==55);return ((struct gpu*)p)->ready;}
static int collect(void *p,int fd,uint64_t job,struct demo_result *result){struct gpu *g=p;assert(fd==55&&job==(uint64_t)g->starts);*result=*g->result;return 0;}
static int detach(void *p,int fd){struct gpu *g=p;assert(fd==55);if(!g->ready)return -EBUSY;++g->unloads;g->objects=0;return 0;}
static int display(void *p,const struct demo_result *result,enum demo_layout layout){struct control *c=p;assert(result->run.state==DEMO_RESULT_VALIDATED&&layout==DEMO_LAYOUT_RESULT);++c->displays;return -EIO;}
static void fixture(const char *path,struct demo_run *run){FILE *f=fopen(path,"rb");assert(f);char *b=calloc(1,DEMO_MAX_JSON+1);size_t n=fread(b,1,DEMO_MAX_JSON,f);fclose(f);assert(!demo_parse_run(b,n,run));free(b);}
static void request(struct demo_run *r,unsigned gen,int app,const char *state){json_object *o=json_tokener_parse(r->json);json_object_object_add(o,"generation",json_object_new_int(gen));json_object_object_add(o,"app",json_object_new_string(app==1?"mosaic":"hunet"));json_object_object_add(o,"state",json_object_new_string(state));const char *text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(text,strlen(text),r));json_object_put(o);}
int main(int argc,char **argv){assert(argc>=2);struct demo_session *s=calloc(1,sizeof(*s));struct demo_dispatch d={0};struct gpu gpu[2]={{.app=1,.ready=1},{.app=2,.ready=1}};struct control c={.profile=5,.gen=1,.apps=gpu};d.control=(struct demo_dispatch_control){open_owner,close_owner,info,select_profile,display,&c};for(unsigned i=0;i<2;++i){d.apps[i]=(struct demo_gpu_ops){NULL,NULL,start,poll,quiet,collect,&gpu[i]};d.detach[i]=detach;gpu[i].result=calloc(1,sizeof(struct demo_result));}
 assert(!demo_dispatch_init(s,NULL,1,&d));assert(!demo_select_app(s,DEMO_MOSAIC));assert(s->owner_fd==55&&s->generation==3&&d.active==DEMO_MOSAIC&&c.opens==1&&c.selects==1);assert(!demo_select_app(s,DEMO_MOSAIC)&&c.selects==1&&!gpu[0].unloads);
 struct demo_run *run=calloc(1,sizeof(*run));struct demo_result *out=calloc(1,sizeof(*out));fixture(argv[1],run);request(run,s->generation,1,"STAGED");gpu[0].result->run=*run;request(&gpu[0].result->run,s->generation,1,"RESULT_VALIDATED");assert(!demo_start(s,run));assert(demo_select_app(s,DEMO_HUNET)==-EBUSY&&!gpu[0].unloads);assert(!demo_status(s,run->run_id,out)&&s->state==DEMO_RESULT_VALIDATED&&gpu[0].starts==1);
 gpu[0].ready=0;assert(demo_select_app(s,DEMO_HUNET)==-EBUSY&&!gpu[0].unloads);gpu[0].ready=1;c.fail_select=1;assert(demo_select_app(s,DEMO_HUNET)==-ENOMEM&&s->owner_fd==55&&!s->quarantined&&d.active==0&&c.gen==3);c.fail_select=0;
 assert(!demo_select_app(s,DEMO_HUNET)&&s->generation==4&&c.opens==1&&d.active==DEMO_HUNET);assert(!demo_select_app(s,DEMO_MOSAIC)&&s->generation==5&&c.opens==1&&gpu[1].unloads==1);assert(!demo_owner_release(s)&&c.closes==1&&s->owner_fd==-1);
 char directory[]="/tmp/demo-dispatch-test-XXXXXX";assert(mkdtemp(directory));struct demo_store store;assert(!demo_store_open(&store,directory,NULL));s->store=&store;fixture(argv[1],&out->run);char *text=calloc(1,DEMO_MAX_JSON+1);size_t n;assert(!demo_write_result(out,text,DEMO_MAX_JSON+1,&n));assert(!demo_parse_run(text,n,&out->run));demo_sha_hex(text,n,out->source_result_sha);assert(!demo_publish(&store,out));int starts=gpu[0].starts+gpu[1].starts;enum demo_state state=s->state;assert(demo_request_display(s,out->run.run_id,DEMO_LAYOUT_RESULT)==-EIO&&c.displays==1&&s->state==state&&!s->quarantined&&gpu[0].starts+gpu[1].starts==starts);assert(demo_request_display(s,"22222222222222222222222222222222",DEMO_LAYOUT_RESULT)==-ENOENT&&c.displays==1);demo_store_close(&store);free(text);free(run);free(out);free(gpu[0].result);free(gpu[1].result);free(s);puts("DUAL_DISPATCH_SAME_OWNER_RESIDENT_GENERATION_BUSY_FAILURE_AND_DISPLAY_INDEPENDENCE_HOST_PASS_NOT_GPU_NOT_DP");return 0;}

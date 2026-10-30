/* TEST ONLY: delayed software callbacks, never a device or numerical model. */
#include <vxl/demo_release.h>
#include <json-c/json.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
struct Model {demo_dispatch d{};demo_result result{};unsigned gen=1,profile=5,starts=0;bool held=false;};
static Model *model;
static int open_owner(void *p){auto &m=*static_cast<Model*>(p);if(m.held)return -EBUSY;m.held=true;++m.gen;return 1000001;}
static int close_owner(void *p,int fd){if(fd!=1000001)return -EBADF;static_cast<Model*>(p)->held=false;return 0;}
static int info(void *p,int fd,vxl_request *r){auto &m=*static_cast<Model*>(p);if(fd!=1000001)return -EBADF;r->generation=m.gen;r->profile_id=m.profile;r->state=VXL_READY;return 0;}
static int select(void *p,int fd,unsigned profile,unsigned gen,unsigned *next){auto &m=*static_cast<Model*>(p);if(fd!=1000001||gen!=m.gen)return -ESTALE;if(m.profile!=profile){m.profile=profile;++m.gen;}*next=m.gen;return 0;}
static int start(void *p,int fd,const demo_run *r,uint64_t *id){auto &m=*static_cast<Model*>(p);if(fd!=1000001)return -EBADF;auto *o=json_tokener_parse(r->json);++m.starts;if(json_object_get_boolean(json_object_object_get(o,"fail_start"))){json_object_put(o);return -EIO;}sleep(7);json_object_object_add(o,"state",json_object_new_string("RESULT_VALIDATED"));json_object_object_add(o,"model_call_count",json_object_new_int(m.starts));json_object_object_add(o,"timing_valid",json_object_new_boolean(0));const char *s=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);int rc=demo_parse_run(s,strlen(s),&m.result.run);if(!rc)demo_sha_hex(s,strlen(s),m.result.source_result_sha);json_object_put(o);*id=m.starts;return rc;}
static int poll(void *,int,uint64_t){return 1;}
static int quiet(void *,int){return 1;}
static int collect(void *p,int,uint64_t,demo_result *r){*r=static_cast<Model*>(p)->result;return 0;}
static int detach(void *,int){return 0;}
static int display(void *,const demo_result *,demo_layout){return -EIO;}
extern "C" int demo_release_install(demo_session *s,demo_store *store,const char *root){if(strcmp(root,"TEST_ONLY_ASYNC_CALLBACKS"))return -EPERM;model=new Model;auto &d=model->d;d.control={open_owner,close_owner,info,select,display,model};for(unsigned i=0;i<2;++i){d.apps[i]={nullptr,nullptr,start,poll,quiet,collect,model};d.detach[i]=detach;}return demo_dispatch_init(s,store,1,&d);}

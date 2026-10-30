#include <vxl/demo_runner.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
static int event(struct demo_session *s,const char *name){return s->store?demo_event(s->store,s->request.run_id,name):0;}
static int quarantine(struct demo_session *s,int rc){s->quarantined=1;s->state=DEMO_QUARANTINED;(void)event(s,"QUARANTINED");return rc;}
static int same_identity(const struct demo_run *a,const struct demo_run *b,int all){
 struct json_object *x=json_tokener_parse(a->json),*y=json_tokener_parse(b->json);int same=0;
 if(x&&y){
  if(all)same=json_object_equal(x,y);
  else same=json_object_equal(json_object_object_get(x,"identities"),json_object_object_get(y,"identities"));
 }
 json_object_put(x);json_object_put(y);return same;
}
int demo_session_bind_identity(struct demo_session *s,const char *boot,const char *build){
 if(!s||!boot||!build||!boot[0]||!build[0]||strlen(boot)>128||strlen(build)>128||s->identity_bound||s->owner_fd>=0||(s->request.run_id[0]&&!s->quarantined))return -EINVAL;
 const char *ids[]={boot,build};for(unsigned k=0;k<2;k++)for(size_t i=0;ids[k][i];i++)if((unsigned char)ids[k][i]<33||(unsigned char)ids[k][i]>126)return -EINVAL;
 strcpy(s->boot_id,boot);strcpy(s->build_id,build);s->identity_bound=1;return 0;
}
static int current_identity(const struct demo_session *s,const struct demo_run *r){
 if(!s->identity_bound)return 1;
 struct json_object *o=json_tokener_parse(r->json);int same=0;
 if(o){const char *boot=json_object_get_string(json_object_object_get(o,"boot_id")),*build=json_object_get_string(json_object_object_get(o,"build_id"));same=boot&&build&&!strcmp(boot,s->boot_id)&&!strcmp(build,s->build_id);}
 json_object_put(o);return same;
}
int demo_session_init(struct demo_session *s,struct demo_store *d,uint32_t generation,const struct demo_gpu_ops *ops){
 int rc;
 if(!s||!generation||!ops||!ops->open_owner||!ops->start||!ops->poll)return -EINVAL;
 memset(s,0,sizeof(*s));s->owner_fd=-1;s->generation=generation;s->gpu=*ops;s->store=d;
 if(d){
  rc=demo_restore_pending(d,&s->request);
  if(rc<0)return rc;
  if(rc){s->quarantined=1;s->state=DEMO_QUARANTINED;}
 }
 return 0;
}
int demo_enqueue(struct demo_session *s,const struct demo_run *run){
 struct demo_run *checked;int rc;
 if(!s||!run)return -EINVAL;
 if(run->generation!=s->generation)return -ESTALE;
 if(s->quarantined)return -EBUSY;
 checked=calloc(1,sizeof(*checked));if(!checked)return -ENOMEM;
 rc=demo_parse_run(run->json,run->json_bytes,checked);
 if(!rc&&(checked->generation!=run->generation||checked->app!=run->app||checked->state!=run->state||strcmp(checked->run_id,run->run_id)))rc=-EINVAL;
 if(rc){free(checked);return rc;}
 if(!current_identity(s,checked)){free(checked);return -ESTALE;}
 if(s->request.run_id[0]&&!strcmp(s->request.run_id,run->run_id)){
  rc=same_identity(&s->request,checked,1)?0:-EEXIST;free(checked);
  if(rc||s->state!=DEMO_STAGED)return rc;
  return 0; /* A queued or active identical command never creates a second intent. */
 }
 if(s->state==DEMO_RUNNING||s->state==DEMO_COMPUTE_DONE){free(checked);return -EBUSY;}
 if(checked->state!=DEMO_STAGED){free(checked);return -EINVAL;}
 if(s->owner_fd>=0&&(!s->gpu.quiescent||s->gpu.quiescent(s->gpu.context,s->owner_fd)!=1)){free(checked);return -EBUSY;}
 if(s->store){rc=demo_record_request(s->store,checked);if(rc){free(checked);return rc;}}
 s->request=*checked;free(checked);s->state=DEMO_STAGED;
 rc=event(s,"STAGED");if(rc)return quarantine(s,rc);
 return 0;
}
int demo_start(struct demo_session *s,const struct demo_run *run){
 int rc=demo_enqueue(s,run);if(rc||s->state!=DEMO_STAGED)return rc;
 rc=demo_owner_acquire(s);if(rc)return rc;
 s->state=DEMO_LOADED;rc=event(s,"START_INTENT");if(rc)return quarantine(s,rc);
 rc=s->gpu.start(s->gpu.context,s->owner_fd,&s->request,&s->job);
 if(rc)return quarantine(s,rc);
 s->state=DEMO_RUNNING;rc=event(s,"RUNNING");return rc?quarantine(s,rc):0;
}
int demo_status(struct demo_session *s,const char *id,struct demo_result *result){
 int rc;
 if(!s||!id||!result||strcmp(id,s->request.run_id))return -ENOENT;
 if(s->quarantined)return -EOWNERDEAD;
 if(s->state==DEMO_RUNNING){
  rc=s->gpu.poll(s->gpu.context,s->owner_fd,s->job);
  if(rc<0)return quarantine(s,rc);
  if(rc==1){
   s->state=DEMO_COMPUTE_DONE;
   if(!s->gpu.quiescent||s->gpu.quiescent(s->gpu.context,s->owner_fd)!=1)return quarantine(s,-EBUSY);
   if(!s->gpu.collect)return quarantine(s,-ENOSYS);
   memset(&s->result,0,sizeof(s->result));
   rc=s->gpu.collect(s->gpu.context,s->owner_fd,s->job,&s->result);
   if(rc)return quarantine(s,rc);
   if(s->result.run.state!=DEMO_RESULT_VALIDATED||s->result.run.generation!=s->generation||s->result.run.app!=s->request.app||strcmp(s->result.run.run_id,id)||!same_identity(&s->request,&s->result.run,0)||!current_identity(s,&s->result.run))return quarantine(s,-ESTALE);
   rc=s->store?demo_publish(s->store,&s->result):0;
   if(rc)return quarantine(s,rc);
   s->state=DEMO_RESULT_VALIDATED;
  }else if(rc!=0)return quarantine(s,-EIO);
 }
 if(s->state==DEMO_RESULT_VALIDATED)*result=s->result;
 else {memset(result,0,sizeof(*result));result->run=s->request;result->run.state=s->state;}
 return 0;
}

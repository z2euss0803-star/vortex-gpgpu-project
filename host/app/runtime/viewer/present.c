#include "kms.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
const char *dp_event_name(enum dp_event_kind k){return k==DP_SUBMITTED?"DISPLAY_SUBMITTED":k==DP_PRESENTED?"DISPLAY_PRESENTED":k==DP_RETIRED?"DISPLAY_RETIRED":"DISPLAY_FAILED";}
static void failure(struct demo_store *store,const struct demo_frame *frame,const char *phase,int original){
 struct json_object *o=json_object_new_object();char name[96];uint64_t now=dp_now();int asset_rc=-ENOMEM;
 if(o){
  json_object_object_add(o,"scope",json_object_new_string("DISPLAY_ONLY_ERROR_NOT_GPU"));
  json_object_object_add(o,"run_id",json_object_new_string(frame->run_id));
  json_object_object_add(o,"frame_sha",json_object_new_string(frame->sha256));
  json_object_object_add(o,"compute_result_sha",json_object_new_string(frame->source_result_sha));
  json_object_object_add(o,"phase",json_object_new_string(phase));
  json_object_object_add(o,"original_errno",json_object_new_int(original));
  json_object_object_add(o,"received_monotonic_ns",json_object_new_uint64(now));
  json_object_object_add(o,"raw_gpu_cycles",NULL);
  const char *text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);
  snprintf(name,sizeof(name),"error-display-%016llx-%016llx.json",(unsigned long long)now,(unsigned long long)store->sequence);
  asset_rc=demo_store_run_asset(store,frame->run_id,name,text,strlen(text));json_object_put(o);
 }
 int journal_rc=demo_event(store,frame->run_id,"DISPLAY_FAILED");
 if(asset_rc||journal_rc)fprintf(stderr,"DISPLAY_ERROR_PERSIST_FAILED original=%d asset=%d journal=%d\n",original,asset_rc,journal_rc);
}
int dp_present_frame(struct dp_context *c,const struct dp_ops *ops,struct demo_store *store,const struct demo_frame *frame,int (*observe)(void *,const struct dp_event *),void *opaque){
 struct dp_config cfg={1280,720};struct dp_event e;int rc=0;const char *phase="OPEN";
 if(!c||!ops||!store||!frame)return -EINVAL;
 if(!c->opened)rc=dp_open(c,&cfg,ops);
 if(!rc){phase="SUBMIT";rc=dp_submit(c,frame);}
 while(!rc){
  phase="POLL";rc=dp_poll(c,3000,&e);if(rc==-EINTR||rc==-EAGAIN){rc=0;continue;}
  if(rc)break;
  phase="JOURNAL";if(e.run_id[0]){rc=demo_event(store,e.run_id,dp_event_name(e.kind));if(rc)break;}
  phase="OBSERVER";if(observe){rc=observe(opaque,&e);if(rc)break;}
  if(e.kind==DP_RETIRED)break;
 }
 if(rc)failure(store,frame,phase,rc);
 return rc;
}

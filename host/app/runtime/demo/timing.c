#include <vxl/demo_timing.h>
#include <errno.h>
#include <json-c/json.h>
#include <string.h>
static const char *phases[]={"decode","preprocess","load","quant_pack","gpu_submit_wait","guard_cdma","poll","handoff","cpu_post","readback","colour","display_submit","display_present"};
static const char *clocks[]={"INVALID","A53_MONOTONIC","B_MONOTONIC","DRM_CLOCK_UNKNOWN"};
int demo_time_record_child(struct demo_timeline *t,enum demo_phase p,uint64_t b,uint64_t e,enum demo_clock c,int v,int parent){
 if(!t||p<0||p>=DEMO_PHASE_COUNT||c<DEMO_A53_MONOTONIC||c>DEMO_DRM_UNKNOWN||t->count>=2048||parent< -1||parent>=(int)t->count||v<0||v>1)return -EINVAL;
 if(v&&(b>e||c==DEMO_DRM_UNKNOWN))return -ERANGE;
 if(parent>=0){const struct demo_interval *q=&t->intervals[parent];if(c!=q->clock||(v&&(!q->valid||b<q->begin||e>q->end)))return -EXDEV;}
 t->intervals[t->count++]=(struct demo_interval){p,b,e,c,v,parent};return 0;
}
int demo_time_record(struct demo_timeline *t,enum demo_phase p,uint64_t b,uint64_t e,enum demo_clock c,int v){return demo_time_record_child(t,p,b,e,c,v,-1);}
int demo_gpu_cycles_capture(struct demo_timeline *t,uint32_t raw,int valid,uint32_t gen,uint64_t job,int pre,int enter,int wrap){
 struct demo_cycles r;
 if(!t||!t->real_board||t->cycle_count>=2048||!job||valid<0||valid>1||pre<0||pre>1||enter<0||enter>1||wrap<0||wrap>1)return -EINVAL;
 for(size_t i=0;i<t->cycle_count;i++)if(t->cycles[i].generation==gen&&t->cycles[i].job_id==job)return -EALREADY;
 r=(struct demo_cycles){raw,gen,job,valid&&pre&&enter&&!wrap&&gen==t->generation,pre,enter,wrap};t->cycles[t->cycle_count++]=r;return r.valid?0:-EBADMSG;
}
int demo_gpu_cycles_record(struct demo_timeline *t,uint32_t raw,int valid,uint32_t gen,uint64_t job,int pre){return demo_gpu_cycles_capture(t,raw,valid,gen,job,pre,valid,0);}
int demo_timeline_write(const struct demo_timeline *t,char *out,size_t cap,size_t *n){
 struct json_object *o,*intervals,*cycles;const char *text;size_t size;
 if(!t||!out||!n||t->count>2048||t->cycle_count>2048||(!t->real_board&&t->cycle_count))return -EINVAL;
 o=json_object_new_object();if(!o)return -ENOMEM;intervals=json_object_new_array();cycles=json_object_new_array();
 json_object_object_add(o,"schema",json_object_new_int(1));json_object_object_add(o,"scope",json_object_new_string(t->real_board?"REAL_BOARD_CAPTURE":"OFFLINE_TIMELINE"));json_object_object_add(o,"raw_gpu_cycles",NULL);json_object_object_add(o,"pe_active_cycles",NULL);json_object_object_add(o,"gpu_internal_stage_cycles",NULL);
 json_object_object_add(o,"intervals",intervals);json_object_object_add(o,"gpu_jobs",cycles);
 for(size_t i=0;i<t->count;i++){
  const struct demo_interval *r=&t->intervals[i];struct json_object *v=json_object_new_object();
  if(r->phase<0||r->phase>=DEMO_PHASE_COUNT||r->clock<1||r->clock>3){json_object_put(o);return -EINVAL;}
  json_object_object_add(v,"phase",json_object_new_string(phases[r->phase]));json_object_object_add(v,"clock",json_object_new_string(clocks[r->clock]));json_object_object_add(v,"begin_ns",json_object_new_uint64(r->begin));json_object_object_add(v,"end_ns",json_object_new_uint64(r->end));json_object_object_add(v,"valid",json_object_new_boolean(r->valid));json_object_object_add(v,"parent",json_object_new_int(r->parent));json_object_array_add(intervals,v);
 }
 for(size_t i=0;i<t->cycle_count;i++){
  const struct demo_cycles *r=&t->cycles[i];struct json_object *v=json_object_new_object();
  json_object_object_add(v,"observed_raw_u32",json_object_new_uint64(r->raw));json_object_object_add(v,"raw_cycles",r->valid?json_object_new_uint64(r->raw):NULL);json_object_object_add(v,"valid",json_object_new_boolean(r->valid));json_object_object_add(v,"generation",json_object_new_uint64(r->generation));json_object_object_add(v,"job_id",json_object_new_uint64(r->job_id));json_object_object_add(v,"pre_ack",json_object_new_boolean(r->pre_ack));json_object_object_add(v,"submit_enter",json_object_new_boolean(r->submit_enter));json_object_object_add(v,"wrapped",json_object_new_boolean(r->wrapped));json_object_array_add(cycles,v);
 }
 text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);size=strlen(text);if(size>=cap){json_object_put(o);return -ENOSPC;}memcpy(out,text,size+1);*n=size;json_object_put(o);return 0;
}

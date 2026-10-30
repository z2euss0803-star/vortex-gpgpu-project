#include <vxl/demo_timing.h>
#include <errno.h>
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>
static int hex(const char *s,size_t n){if(!s||strlen(s)!=n)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
int demo_error_save(struct demo_store *s,const struct demo_error *e){
 struct json_object *o;char name[64];const char *text;int rc;
 if(!s||!e||!hex(e->run_id,32)||!hex(e->error_id,32)||!hex(e->source_sha,64)||!hex(e->input_sha,64)||!e->command||!e->message||strlen(e->command)>4096||strlen(e->message)>4096||(!e->real_board&&(e->raw_observed||e->cycle_valid))||(e->cycle_valid&&!e->raw_observed))return -EINVAL;
 o=json_object_new_object();if(!o)return -ENOMEM;
 json_object_object_add(o,"schema",json_object_new_int(1));json_object_object_add(o,"run_id",json_object_new_string(e->run_id));json_object_object_add(o,"error_id",json_object_new_string(e->error_id));json_object_object_add(o,"command",json_object_new_string(e->command));json_object_object_add(o,"source_sha256",json_object_new_string(e->source_sha));json_object_object_add(o,"input_sha256",json_object_new_string(e->input_sha));json_object_object_add(o,"message",json_object_new_string(e->message));json_object_object_add(o,"natural_exit",json_object_new_int(e->natural_exit));json_object_object_add(o,"scope",json_object_new_string(e->real_board?"REAL_BOARD_ERROR":"OFFLINE_ERROR"));json_object_object_add(o,"observed_raw_u32",e->raw_observed?json_object_new_uint64(e->gpu_raw):NULL);json_object_object_add(o,"raw_gpu_cycles",e->cycle_valid?json_object_new_uint64(e->gpu_raw):NULL);json_object_object_add(o,"cycle_valid",json_object_new_boolean(e->cycle_valid));
 text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);snprintf(name,sizeof(name),"error-%s.json",e->error_id);rc=demo_store_run_asset(s,e->run_id,name,text,strlen(text));json_object_put(o);return rc;
}

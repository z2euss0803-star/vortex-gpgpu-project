#include <vxl/demo_frame.h>
#include <errno.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int has_asset(struct json_object *obj,const char *wanted_role,const char *sha){
 struct json_object *assets=json_object_object_get(obj,"artifacts");size_t i;
 if(!assets||!json_object_is_type(assets,json_type_array))return 0;
 for(i=0;i<json_object_array_length(assets);i++){
  struct json_object *a=json_object_array_get_idx(assets,i),*role=json_object_object_get(a,"role"),*digest=json_object_object_get(a,"sha256");
  if(role&&digest&&!strcmp(json_object_get_string(role),wanted_role)&&!strcmp(json_object_get_string(digest),sha))return 1;
 }
 return 0;
}
int demo_frame_publish(struct demo_store *store,const struct demo_frame *frame,const struct demo_result *result){
 char pixel_sha[65],result_sha[65],name[128],*text,*expected;size_t n=0,expected_n=0;int rc;struct json_object *compute=NULL,*metadata=NULL,*numeric;const char *layout;
 if(!store||!frame||!result||demo_frame_check(frame,1)||result->run.state!=DEMO_RESULT_VALIDATED||strlen(frame->sha256)!=64||strlen(result->source_result_sha)!=64||strcmp(frame->run_id,result->run.run_id)||strcmp(frame->source_result_sha,result->source_result_sha))return -EINVAL;
 if(frame->layout==DEMO_LAYOUT_RESULT)layout="RESULT";
 else if(frame->layout==DEMO_LAYOUT_INPUT)layout="INPUT";
 else if(frame->layout==DEMO_LAYOUT_COMPARE)layout="COMPARE";else return -EINVAL;
 demo_sha_hex(frame->pixels,frame->bytes,pixel_sha);if(strcmp(pixel_sha,frame->sha256))return -EBADMSG;
 text=malloc(DEMO_MAX_JSON+1);expected=malloc(DEMO_MAX_JSON+1);if(!text||!expected){free(text);free(expected);return -ENOMEM;}
 rc=demo_store_read_result(store,result->run.run_id,text,DEMO_MAX_JSON+1,&n);
 if(!rc){demo_sha_hex(text,n,result_sha);if(strcmp(result_sha,result->source_result_sha))rc=-ESTALE;}
 if(!rc)rc=demo_write_result(result,expected,DEMO_MAX_JSON+1,&expected_n);
 if(!rc&&(n!=expected_n||memcmp(text,expected,n)))rc=-ESTALE;
 if(!rc){
  compute=json_tokener_parse(text);
  if(!compute)rc=-ESTALE;
  else if(frame->layout==DEMO_LAYOUT_INPUT){
   if(!has_asset(compute,"input",frame->raw_source_sha))rc=-ESTALE;
  }else{
   if(!has_asset(compute,"output",frame->raw_source_sha))rc=-ESTALE;
   if(!rc&&(frame->layout==DEMO_LAYOUT_COMPARE||frame->input_source_sha[0])&&!has_asset(compute,"input",frame->input_source_sha))rc=-ESTALE;
  }
 }
 numeric=compute?json_object_object_get(compute,"numeric_class"):NULL;
 if(!rc&&(!numeric||!strcmp(json_object_get_string(numeric),"FAIL")))rc=-EACCES;
 if(!rc){
  metadata=json_object_new_object();
  json_object_object_add(metadata,"schema",json_object_new_int(1));json_object_object_add(metadata,"run_id",json_object_new_string(frame->run_id));
  json_object_object_add(metadata,"app",json_object_new_string(result->run.app==DEMO_MOSAIC?"mosaic":"hunet"));
  json_object_object_add(metadata,"source_result_sha",json_object_new_string(result_sha));
  json_object_object_add(metadata,"width",json_object_new_int(frame->width));json_object_object_add(metadata,"height",json_object_new_int(frame->height));json_object_object_add(metadata,"stride",json_object_new_int(frame->stride));
  json_object_object_add(metadata,"format",json_object_new_string("RGB8"));json_object_object_add(metadata,"layout",json_object_new_string("HWC_RGB8"));json_object_object_add(metadata,"composition",json_object_new_string(layout));
  json_object_object_add(metadata,"bytes",json_object_new_uint64(frame->bytes));json_object_object_add(metadata,"sha256",json_object_new_string(pixel_sha));
  json_object_object_add(metadata,"numeric_class",json_object_new_string(json_object_get_string(numeric)));
  json_object_object_add(metadata,"raw_source_sha",json_object_new_string(frame->raw_source_sha));json_object_object_add(metadata,"scope",json_object_new_string(json_object_get_string(json_object_object_get(compute,"scope"))));
  if(frame->input_source_sha[0])json_object_object_add(metadata,"input_source_sha",json_object_new_string(frame->input_source_sha));
  snprintf(name,sizeof(name),"frame-%s.rgb",pixel_sha);rc=demo_store_run_asset(store,frame->run_id,name,frame->pixels,frame->bytes);
  if(!rc){const char *json=json_object_to_json_string_ext(metadata,JSON_C_TO_STRING_PLAIN);snprintf(name,sizeof(name),"frame-%s-%u.json",pixel_sha,(unsigned)frame->layout);rc=demo_store_run_asset(store,frame->run_id,name,json,strlen(json));}
 }
 json_object_put(compute);json_object_put(metadata);free(text);free(expected);return rc;
}

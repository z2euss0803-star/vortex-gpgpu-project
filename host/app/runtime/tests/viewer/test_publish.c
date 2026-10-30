#define _GNU_SOURCE
#include <vxl/demo_frame.h>
#include "../../viewer/ipc.h"
#include <json-c/json.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv){
 char directory[]="/tmp/demo-frame-publish-XXXXXX",*text=calloc(1,DEMO_MAX_JSON+1),hash[65],before[65],after[65];
 struct demo_result *r=calloc(1,sizeof(*r));struct demo_frame f={0};struct demo_store store;
 float pixel[]={255,0,0};size_t n,bytes;FILE *file;struct json_object *o,*asset,*artifacts,*shape;
 assert(argc==2&&text&&r&&mkdtemp(directory));file=fopen(argv[1],"rb");assert(file);n=fread(text,1,DEMO_MAX_JSON,file);fclose(file);
 assert(!demo_frame_from_mosaic(pixel,1,1,&f));o=json_tokener_parse(text);assert(o);
 asset=json_object_new_object();json_object_object_add(asset,"role",json_object_new_string("output"));json_object_object_add(asset,"bytes",json_object_new_int(12));json_object_object_add(asset,"sha256",json_object_new_string(f.raw_source_sha));json_object_object_add(asset,"name",json_object_new_string("output.f32"));json_object_object_add(asset,"dtype",json_object_new_string("FP32"));
 shape=json_object_new_array();json_object_array_add(shape,json_object_new_int(3));json_object_array_add(shape,json_object_new_int(1));json_object_array_add(shape,json_object_new_int(1));json_object_object_add(asset,"shape",shape);
 artifacts=json_object_new_array();json_object_array_add(artifacts,asset);json_object_object_add(o,"artifacts",artifacts);
 const char *raw=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(raw,strlen(raw),&r->run));json_object_put(o);
 assert(!demo_write_result(r,text,DEMO_MAX_JSON+1,&bytes));demo_sha_hex(text,bytes,r->source_result_sha);
 strcpy(f.run_id,r->run.run_id);strcpy(f.source_result_sha,r->source_result_sha);
 assert(!demo_store_open(&store,directory,NULL));assert(demo_frame_publish(&store,&f,r)<0); /* no completed result */
 assert(!demo_publish(&store,r));assert(!demo_store_read_result(&store,r->run.run_id,text,DEMO_MAX_JSON+1,&n));demo_sha_hex(text,n,before);
 assert(!demo_frame_publish(&store,&f,r));
 char request[512];struct demo_frame loaded={0};
 snprintf(request,sizeof(request),"{\"run_id\":\"%s\",\"frame_sha\":\"%s\",\"source_result_sha\":\"%s\",\"composition\":\"RESULT\"}",f.run_id,f.sha256,r->source_result_sha);
 assert(!dp_load_reference(&store,request,strlen(request),&loaded));assert(loaded.bytes==f.bytes&&!memcmp(loaded.pixels,f.pixels,f.bytes));demo_frame_free(&loaded);
 assert(dp_load_reference(&store,request,4097,&loaded)<0);
 char *selected=strstr(request,f.run_id);selected[0]='2';assert(dp_load_reference(&store,request,strlen(request),&loaded)<0);selected[0]='1';
 selected=strstr(request,f.sha256);selected[0]=selected[0]=='a'?'b':'a';assert(dp_load_reference(&store,request,strlen(request),&loaded)<0);
 assert(!demo_store_read_result(&store,r->run.run_id,text,DEMO_MAX_JSON+1,&n));demo_sha_hex(text,n,after);assert(!strcmp(before,after));
 f.layout=DEMO_LAYOUT_INPUT;assert(demo_frame_publish(&store,&f,r)<0); /* output SHA is not an input artifact */
 f.layout=DEMO_LAYOUT_COMPARE;memset(f.input_source_sha,'c',64);f.input_source_sha[64]=0;assert(demo_frame_publish(&store,&f,r)<0);
 f.layout=DEMO_LAYOUT_RESULT;f.input_source_sha[0]=0;
 f.run_id[0]='2';assert(demo_frame_publish(&store,&f,r)<0);f.run_id[0]='1';
 strcpy(hash,f.raw_source_sha);f.raw_source_sha[0]=hash[0]=='a'?'b':'a';assert(demo_frame_publish(&store,&f,r)<0);strcpy(f.raw_source_sha,hash);
 f.pixels[0]=0;assert(demo_frame_publish(&store,&f,r)<0);
 demo_frame_free(&f);demo_store_close(&store);free(text);free(r);puts("OFFSCREEN_FRAME_EXACT_RESULT_BINDING_HOST_PASS_NOT_DP");return 0;
}

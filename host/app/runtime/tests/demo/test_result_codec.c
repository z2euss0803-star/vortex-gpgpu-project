#include <vxl/demo_result.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>
#include <ctype.h>
static void rejects(struct json_object *o,struct demo_run *r){
 const char *s=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);
 assert(demo_parse_run(s,strlen(s),r)<0);
 json_object_put(o);
}
static struct json_object *load(const char *s){struct json_object *o=json_tokener_parse(s);assert(o);return o;}
int main(int argc,char **argv){
 struct demo_result *r=calloc(1,sizeof(*r));
 struct demo_run *again=calloc(1,sizeof(*again));
 char *text=calloc(1,DEMO_MAX_JSON+1),*out=calloc(1,DEMO_MAX_JSON+1);
 size_t n,written=0;FILE *f;struct json_object *o,*v;
 assert(argc==3&&r&&again&&text&&out);
 f=fopen(argv[1],"rb");assert(f);n=fread(text,1,DEMO_MAX_JSON+1,f);assert(!ferror(f));fclose(f);
 assert(!demo_parse_run(text,n,&r->run));
 assert(r->run.app==DEMO_MOSAIC&&r->run.generation==1&&!r->run.cycle_valid);
 assert(demo_write_result(r,out,1,&written)==-ENOSPC);
 assert(!demo_write_result(r,out,DEMO_MAX_JSON+1,&written));
 assert(strstr(out,"9007199254740993"));
 assert(strstr(out,"18446744073709551615"));
 assert(!demo_parse_run(out,written,again));
 assert(!strcmp(again->run_id,r->run.run_id));
 {size_t end=n;while(end&&isspace((unsigned char)text[end-1]))end--;
  assert(end&&demo_parse_run(text,end-1,again)<0);}
 assert(demo_parse_run("{}",2,again)<0);
 assert(demo_parse_run("{} {}",5,again)<0);
 o=load(text);json_object_object_del(o,"diagnostics");rejects(o,again);
 o=load(text);json_object_object_add(o,"generation",json_object_new_boolean(1));rejects(o,again);
 o=load(text);json_object_object_add(o,"generation",json_object_new_uint64(UINT64_C(4294967296)));rejects(o,again);
 o=load(text);json_object_object_add(o,"raw_cycles",json_object_new_uint64(127273));rejects(o,again);
 o=load(text);json_object_object_add(o,"cycle_valid",json_object_new_boolean(1));rejects(o,again);
 o=load(text);v=json_object_object_get(o,"identities");json_object_object_del(v,"input");rejects(o,again);
 o=load(text);v=json_object_object_get(o,"timings_ns");json_object_object_add(v,"decode",json_object_new_double(9007199254740992.0));rejects(o,again);
 o=load(text);json_object_object_add(o,"app",json_object_new_string("ocr"));rejects(o,again);
 o=load(text);json_object_object_add(o,"app",json_object_new_string_len("mosaic\0junk",11));rejects(o,again);
 o=load(text);json_object_object_add(o,"state",json_object_new_string_len("RESULT_VALIDATED\0junk",21));rejects(o,again);
 f=fopen(argv[2],"wb");assert(f);assert(fwrite(out,1,written,f)==written);assert(!fclose(f));
 free(r);free(again);free(text);free(out);
 puts("DEMO_NATIVE_CODEC_PASS_NOT_BOARD");return 0;
}

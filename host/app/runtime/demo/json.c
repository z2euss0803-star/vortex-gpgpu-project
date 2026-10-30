#include <vxl/demo_result.h>
#include <json-c/json.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
static struct json_object *member(struct json_object *o,const char *name){
 struct json_object *v=NULL;
 if(!json_object_object_get_ex(o,name,&v))return NULL;
 return v;
}
static int type(struct json_object *v,enum json_type t){return v&&json_object_is_type(v,t);}
static int u64(struct json_object *v,uint64_t min,uint64_t max,uint64_t *out){
 const char *s;
 if(!type(v,json_type_int))return -EINVAL;
 s=json_object_get_string(v);
 if(!s||s[0]=='-')return -ERANGE;
 *out=json_object_get_uint64(v);
 return *out<min||*out>max?-ERANGE:0;
}
static int string(struct json_object *v,size_t max){
 const char *s;size_t i,n;
 if(!type(v,json_type_string))return 0;
 s=json_object_get_string(v);n=(size_t)json_object_get_string_len(v);
 if(!n||n>max||strlen(s)!=n)return 0;
 for(i=0;i<n;i++)if((unsigned char)s[i]<32)return 0;
 return 1;
}
static int hex(struct json_object *v,size_t n){
 const char *s;size_t i;
 if(!string(v,n)||(size_t)json_object_get_string_len(v)!=n)return 0;
 s=json_object_get_string(v);
 for(i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
 return 1;
}
static int equal(struct json_object *v,const char *s){return type(v,json_type_string)&&(size_t)json_object_get_string_len(v)==strlen(s)&&!memcmp(json_object_get_string(v),s,strlen(s));}
static int name_ok(struct json_object *v){
 const char *s,*start,*p;size_t n;
 if(!string(v,240))return 0;
 s=json_object_get_string(v);
 if(s[0]=='/'||strchr(s,'\\')||strchr(s,':'))return 0;
 start=s;
 for(p=s;;p++)if(*p=='/'||!*p){
  n=(size_t)(p-start);
  if(!n||(n==1&&*start=='.')||(n==2&&!memcmp(start,"..",2)))return 0;
  if(!*p)break;
  start=p+1;
 }
 return 1;
}
static int shape(struct json_object *a,uint64_t maximum,uint64_t *product){
 size_t i,n;uint64_t d;
 if(!type(a,json_type_array))return -EINVAL;
 n=json_object_array_length(a);if(!n||n>4)return -EINVAL;
 for(i=0;i<n;i++){
  if(u64(json_object_array_get_idx(a,i),1,maximum,&d)||*product>UINT64_MAX/d)return -ERANGE;
  *product*=d;
 }
 return 0;
}
static int asset(struct json_object *a){
 uint64_t n,product=0;struct json_object *dtype=member(a,"dtype"),*role=member(a,"role");
 if(!type(a,json_type_object)||!name_ok(member(a,"name"))||!hex(member(a,"sha256"),64))return -EINVAL;
 if(!(equal(role,"input")||equal(role,"model")||equal(role,"quant")||equal(role,"glyph")||equal(role,"display")||equal(role,"output")))return -EINVAL;
 if(equal(dtype,"FP32"))product=4;
 else if(equal(dtype,"I16"))product=2;
 else if(equal(dtype,"RGB8"))product=3;
 else if(equal(dtype,"U8")||equal(dtype,"BYTES"))product=1;
 else return -EINVAL;
 if(u64(member(a,"bytes"),0,DEMO_MAX_ASSET,&n)||shape(member(a,"shape"),DEMO_MAX_ASSET,&product))return -EINVAL;
 return product==n&&product<=DEMO_MAX_ASSET?0:-EINVAL;
}
static int validate(struct json_object *o,struct demo_run *r){
 static const char *ids[]={"hardware","source","program","model","input","quant","layout"};
 static const char *states[]={"STAGED","LOADED","RUNNING","COMPUTE_DONE","RESULT_VALIDATED","FAILED","QUARANTINED"};
 static const char *numeric[]={"NOT_EVALUATED","PASS","KNOWN_NUMERIC_DEVIATION","FAIL"};
 struct json_object *v,*identities,*artifacts,*timings;uint64_t n,product=1;size_t i;int found;
 if(!type(o,json_type_object)||u64(member(o,"schema"),1,1,&n))return -EINVAL;
 v=member(o,"app");
 if(equal(v,"mosaic"))r->app=DEMO_MOSAIC;
 else if(equal(v,"hunet"))r->app=DEMO_HUNET;else return -EINVAL;
 if(!hex(member(o,"run_id"),32)||!string(member(o,"boot_id"),128)||!string(member(o,"build_id"),128))return -EINVAL;
 memcpy(r->run_id,json_object_get_string(member(o,"run_id")),33);
 if(u64(member(o,"generation"),1,UINT32_MAX,&n))return -EINVAL;
 r->generation=(uint32_t)n;
 if(!type(member(o,"diagnostics"),json_type_boolean)||!type(member(o,"cycle_valid"),json_type_boolean)||!type(member(o,"timing_valid"),json_type_boolean))return -EINVAL;
 r->diagnostics=json_object_get_boolean(member(o,"diagnostics"));
 r->cycle_valid=json_object_get_boolean(member(o,"cycle_valid"));
 r->timing_valid=json_object_get_boolean(member(o,"timing_valid"));
 if(!json_object_object_get_ex(o,"raw_cycles",&v))return -EINVAL;
 if(equal(member(o,"scope"),"REAL_BOARD")){
  if(equal(member(o,"boot_id"),"MOCK")||equal(member(o,"boot_id"),"OFFLINE"))return -EINVAL;
  if(r->cycle_valid){if(u64(v,0,UINT64_MAX,&r->raw_cycles))return -EINVAL;}
  else if(v)return -EINVAL;
 }else if(equal(member(o,"scope"),"MOCK")||equal(member(o,"scope"),"OFFLINE")){
  if(v||r->cycle_valid)return -EINVAL;
 }else return -EINVAL;
 found=0;for(i=0;i<7;i++)if(equal(member(o,"state"),states[i])){r->state=(enum demo_state)(i+1);found=1;}
 if(!found)return -EINVAL;
 found=0;for(i=0;i<4;i++)if(equal(member(o,"numeric_class"),numeric[i]))found=1;
 if(!found||shape(member(o,"shape"),INT32_MAX,&product))return -EINVAL;
 identities=member(o,"identities");
 if(!type(identities,json_type_object)||json_object_object_length(identities)!=7)return -EINVAL;
 for(i=0;i<7;i++)if(!hex(member(identities,ids[i]),64))return -EINVAL;
 artifacts=member(o,"artifacts");
 if(!type(artifacts,json_type_array)||json_object_array_length(artifacts)>32)return -EINVAL;
 for(i=0;i<json_object_array_length(artifacts);i++)if(asset(json_object_array_get_idx(artifacts,i)))return -EINVAL;
 timings=member(o,"timings_ns");
 if(timings){
  if(!type(timings,json_type_object)||json_object_object_length(timings)>64)return -EINVAL;
  json_object_object_foreach(timings,key,value){
   size_t j,len=strlen(key);if(!len||len>64||u64(value,0,UINT64_MAX,&n))return -EINVAL;
   for(j=0;j<len;j++)if((unsigned char)key[j]<32)return -EINVAL;
  }
 }
 return 0;
}
static struct json_object *parse(const char *s,size_t n){
 struct json_tokener *tok;struct json_object *o;size_t used;
 if(!s||!n||n>DEMO_MAX_JSON||memchr(s,0,n))return NULL;
 tok=json_tokener_new_ex(32);if(!tok)return NULL;
 json_tokener_set_flags(tok,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
 o=json_tokener_parse_ex(tok,s,(int)n);
 used=json_tokener_get_parse_end(tok);
 if(json_tokener_get_error(tok)!=json_tokener_success){json_object_put(o);o=NULL;}
 while(used<n&&isspace((unsigned char)s[used]))used++;
 if(used!=n){json_object_put(o);o=NULL;}
 json_tokener_free(tok);return o;
}
int demo_parse_run(const char *s,size_t n,struct demo_run *r){
 struct json_object *o;int rc;
 if(!r)return -EINVAL;
 memset(r,0,sizeof(*r));o=parse(s,n);if(!o)return -EINVAL;
 rc=validate(o,r);json_object_put(o);
 if(!rc){memcpy(r->json,s,n);r->json[n]=0;r->json_bytes=n;}
 else memset(r,0,sizeof(*r));
 return rc;
}
int demo_parse_asset(const char *s,size_t n,struct demo_asset *a){
 struct json_object *o,*v;size_t i;int rc;
 if(!a)return -EINVAL;
 memset(a,0,sizeof(*a));o=parse(s,n);if(!o)return -EINVAL;
 rc=asset(o);
 if(!rc){
  strcpy(a->name,json_object_get_string(member(o,"name")));strcpy(a->sha256,json_object_get_string(member(o,"sha256")));
  a->bytes=json_object_get_uint64(member(o,"bytes"));v=member(o,"shape");a->rank=(uint32_t)json_object_array_length(v);
  for(i=0;i<a->rank;i++)a->shape[i]=(uint32_t)json_object_get_uint64(json_object_array_get_idx(v,i));
  v=member(o,"dtype");a->element_bytes=equal(v,"FP32")?4:equal(v,"RGB8")?3:equal(v,"I16")?2:1;
 }
 json_object_put(o);return rc;
}
int demo_write_result(const struct demo_result *r,char *s,size_t cap,size_t *written){
 struct json_object *o;struct demo_run *check;const char *text;size_t n;int rc;
 if(!r||!s||!written)return -EINVAL;
 o=parse(r->run.json,r->run.json_bytes);if(!o)return -EINVAL;
 check=calloc(1,sizeof(*check));if(!check){json_object_put(o);return -ENOMEM;}
 rc=validate(o,check);
 if(!rc&&(check->generation!=r->run.generation||strcmp(check->run_id,r->run.run_id)||check->app!=r->run.app||check->state!=r->run.state))rc=-EINVAL;
 free(check);
 if(!rc){
  text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);n=strlen(text);
  if(n>=cap)rc=-ENOSPC;
  else {memcpy(s,text,n+1);*written=n;}
 }
 json_object_put(o);return rc;
}

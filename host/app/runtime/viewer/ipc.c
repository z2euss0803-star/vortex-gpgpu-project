#define _GNU_SOURCE
#include "ipc.h"
#include <json-c/json.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
int dp_serve_connection(int fd,uid_t uid,int (*callback)(void *,const char *,size_t),void *ctx){
 struct ucred peer; socklen_t length=sizeof(peer);char request[4097];size_t used=0;int rc;struct timespec start,now;
 if(fd<0||!callback)return -EINVAL;
 if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length)||peer.uid!=uid)return -EACCES;
 if(clock_gettime(CLOCK_MONOTONIC,&start))return -errno;
 while(used<sizeof(request)){
  struct pollfd p={fd,POLLIN,0};int64_t elapsed;
  if(clock_gettime(CLOCK_MONOTONIC,&now))return -errno;
  elapsed=((int64_t)now.tv_sec-start.tv_sec)*1000+((int64_t)now.tv_nsec-start.tv_nsec)/1000000;
  if(elapsed>=3000)return -ETIMEDOUT;
  rc=poll(&p,1,(int)(3000-elapsed));if(rc<0&&errno==EINTR)continue;if(rc<0)return -errno;if(!rc)return -ETIMEDOUT;
  ssize_t n=recv(fd,request+used,sizeof(request)-used,MSG_DONTWAIT);if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<0)return -errno;if(!n)return -EMSGSIZE;
  size_t old=used;used+=(size_t)n;
  for(size_t i=old;i<used;i++)if(request[i]=='\n'){if(i!=used-1||i>4096)return -EMSGSIZE;return callback(ctx,request,i);}
 }
 return -EMSGSIZE;
}
static int hex(const char *s,size_t n){if(!s||strlen(s)!=n)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static struct json_object *parse(const char *s,size_t n){struct json_tokener *t=json_tokener_new();struct json_object *o;if(!t)return NULL;json_tokener_set_flags(t,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);o=json_tokener_parse_ex(t,s,(int)n);if(json_tokener_get_error(t)!=json_tokener_success||json_tokener_get_parse_end(t)!=n){json_object_put(o);o=NULL;}json_tokener_free(t);return o;}
static const char *string(struct json_object *o,const char *k){struct json_object *v=json_object_object_get(o,k);return v&&json_object_is_type(v,json_type_string)?json_object_get_string(v):NULL;}
static int load(int dir,const char *name,size_t limit,void **out,size_t *size){
 struct stat st;int fd=openat(dir,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);size_t n=0;uint8_t *p;
 if(fd<0)return -errno;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>limit){close(fd);return -EINVAL;}
 p=malloc((size_t)st.st_size+1);if(!p){close(fd);return -ENOMEM;}
 while(n<(size_t)st.st_size){ssize_t got=read(fd,p+n,(size_t)st.st_size-n);if(got<0&&errno==EINTR)continue;if(got<=0){free(p);close(fd);return -EIO;}n+=(size_t)got;}
 close(fd);p[n]=0;*out=p;*size=n;return 0;
}
static int asset(struct json_object *compute,const char *role,const char *sha){struct json_object *a=json_object_object_get(compute,"artifacts");if(!a||!sha)return 0;for(size_t i=0;i<json_object_array_length(a);i++){struct json_object *v=json_object_array_get_idx(a,i);const char *r=string(v,"role"),*s=string(v,"sha256");if(r&&s&&!strcmp(r,role)&&!strcmp(s,sha))return 1;}return 0;}
int dp_load_reference(struct demo_store *store,const char *request,size_t bytes,struct demo_frame *out){
 struct json_object *q=NULL,*m=NULL,*compute=NULL;char name[128],digest[65];const char *id,*sha,*source,*composition,*raw,*input;void *meta=NULL,*pixels=NULL,*result=NULL;size_t mn=0,pn=0,rn=0;int dir=-1,rc=-EINVAL;uint64_t w,h,stride;unsigned layout;
 if(!store||!request||!out||out->pixels||!bytes||bytes>4096)return -EINVAL;
 q=parse(request,bytes);if(!q||!json_object_is_type(q,json_type_object)||json_object_object_length(q)!=4)goto done;
 id=string(q,"run_id");sha=string(q,"frame_sha");source=string(q,"source_result_sha");composition=string(q,"composition");if(!hex(id,32)||!hex(sha,64)||!hex(source,64)||!composition)goto done;
 if(!strcmp(composition,"RESULT"))layout=DEMO_LAYOUT_RESULT;else if(!strcmp(composition,"INPUT"))layout=DEMO_LAYOUT_INPUT;else if(!strcmp(composition,"COMPARE"))layout=DEMO_LAYOUT_COMPARE;else goto done;
 dir=openat(store->dir_fd,id,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir<0){rc=-errno;goto done;}
 snprintf(name,sizeof(name),"frame-%s-%u.json",sha,layout);rc=load(dir,name,16384,&meta,&mn);if(rc)goto done;
 rc=load(dir,"compute_result.json",DEMO_MAX_JSON,&result,&rn);if(rc)goto done;
 demo_sha_hex(result,rn,digest);if(strcmp(digest,source)){rc=-ESTALE;goto done;}
 compute=parse(result,rn);m=parse(meta,mn);rc=-ESTALE;
 if(!compute||!m||!string(m,"run_id")||strcmp(string(m,"run_id"),id)||!string(m,"source_result_sha")||strcmp(string(m,"source_result_sha"),source)||!string(m,"sha256")||strcmp(string(m,"sha256"),sha)||!string(m,"composition")||strcmp(string(m,"composition"),composition)||!string(m,"format")||strcmp(string(m,"format"),"RGB8")||!string(m,"layout")||strcmp(string(m,"layout"),"HWC_RGB8"))goto done;
 struct demo_run *validated=calloc(1,sizeof(*validated));if(!validated){rc=-ENOMEM;goto done;}
 rc=demo_parse_run(result,rn,validated);if(!rc&&(validated->state!=DEMO_RESULT_VALIDATED||strcmp(validated->run_id,id)))rc=-ESTALE;free(validated);if(rc)goto done;
 if(!string(compute,"numeric_class")||!strcmp(string(compute,"numeric_class"),"FAIL")){rc=-EACCES;goto done;}
 raw=string(m,"raw_source_sha");input=string(m,"input_source_sha");
 if(!hex(raw,64)||!asset(compute,layout==DEMO_LAYOUT_INPUT?"input":"output",raw)||((layout==DEMO_LAYOUT_COMPARE||input)&&!asset(compute,"input",input))){rc=-ESTALE;goto done;}
 w=json_object_get_uint64(json_object_object_get(m,"width"));h=json_object_get_uint64(json_object_object_get(m,"height"));stride=json_object_get_uint64(json_object_object_get(m,"stride"));
 if(!w||!h||w>1920||h>1080||stride>UINT32_MAX||stride<w*3||stride*h>DEMO_MAX_ASSET){rc=-ERANGE;goto done;}
 snprintf(name,sizeof(name),"frame-%s.rgb",sha);rc=load(dir,name,DEMO_MAX_ASSET,&pixels,&pn);if(rc)goto done;
 demo_sha_hex(pixels,pn,digest);if(strcmp(digest,sha)||pn!=stride*h||json_object_get_uint64(json_object_object_get(m,"bytes"))!=pn){rc=-EBADMSG;goto done;}
 *out=(struct demo_frame){.width=(uint32_t)w,.height=(uint32_t)h,.stride=(uint32_t)stride,.bytes=pn,.pixels=pixels,.owned=1,.layout=(enum demo_layout)layout};pixels=NULL;
 memcpy(out->run_id,id,33);memcpy(out->sha256,sha,65);memcpy(out->source_result_sha,source,65);memcpy(out->raw_source_sha,raw,65);if(input)memcpy(out->input_source_sha,input,65);rc=0;
done:
 if(dir>=0)close(dir);
 free(meta);free(pixels);free(result);json_object_put(q);json_object_put(m);json_object_put(compute);return rc;
}

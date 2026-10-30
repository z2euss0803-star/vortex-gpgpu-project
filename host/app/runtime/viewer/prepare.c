#define _GNU_SOURCE
#include <vxl/demo_frame.h>
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static struct json_object *get(struct json_object *o,const char *key){return json_object_object_get(o,key);}
static int equal(struct json_object *v,const char *s){return v&&!strcmp(json_object_get_string(v),s);}
static struct json_object *find(struct json_object *a,const char *role,const char *dtype){
 struct json_object *found=NULL;size_t i;for(i=0;i<json_object_array_length(a);++i){struct json_object *o=json_object_array_get_idx(a,i);if(equal(get(o,"role"),role)&&(!dtype||equal(get(o,"dtype"),dtype))){if(found)return NULL;found=o;}}return found;
}
static int read_asset(struct demo_store *store,const char *id,struct json_object *o,void **data,size_t *bytes){
 const char *name=json_object_get_string(get(o,"name")),*sha=json_object_get_string(get(o,"sha256"));uint64_t want=json_object_get_uint64(get(o,"bytes"));struct stat st;size_t offset=0;int dir,fd,rc=0;char actual[65];void *p;
 if(!name||strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")||!sha||!want||want>DEMO_MAX_ASSET)return -EINVAL;
 dir=openat(store->dir_fd,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -errno;
 fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);close(dir);if(fd<0)return -errno;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=want||st.st_uid!=geteuid()||(st.st_mode&0022)){close(fd);return -EACCES;}
 p=malloc((size_t)want);if(!p){close(fd);return -ENOMEM;}
 while(offset<want){ssize_t n=read(fd,(char*)p+offset,(size_t)want-offset);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}offset+=(size_t)n;}
 if(close(fd)&&!rc)rc=-errno;
 if(!rc){demo_sha_hex(p,(size_t)want,actual);if(strcmp(actual,sha))rc=-EBADMSG;}
 if(rc){free(p);return rc;}*data=p;*bytes=(size_t)want;return 0;
}
static int read_rgb(struct demo_store *s,const char *id,struct json_object *o,struct demo_frame *f){
 struct json_object *shape=get(o,"shape");void *data=NULL;size_t bytes=0;int rc;if(json_object_array_length(shape)!=2)return -EINVAL;
 uint64_t h=json_object_get_uint64(json_object_array_get_idx(shape,0)),w=json_object_get_uint64(json_object_array_get_idx(shape,1));if(!h||!w||h>1080||w>1920)return -ERANGE;
 rc=read_asset(s,id,o,&data,&bytes);if(rc)return rc;f->pixels=data;f->owned=1;f->width=(uint32_t)w;f->height=(uint32_t)h;f->stride=(uint32_t)w*3;f->bytes=bytes;rc=demo_frame_check(f,0);if(!rc)rc=demo_frame_hash(f);return rc;
}
int demo_prepare_display(struct demo_store *store,const struct demo_result *r,enum demo_layout layout,struct demo_frame *out){
 char *stored=NULL,*expected=NULL,sha[65],banner[200];size_t bytes=0,n=0;struct json_object *o=NULL,*a,*input,*rgb,*raw,*numeric,*scope;struct demo_frame in={0},result={0},canvas={0};void *raw_data=NULL;size_t raw_bytes=0;int rc;
 if(!store||!r||!out||out->pixels||r->run.state!=DEMO_RESULT_VALIDATED||layout<1||layout>3)return -EINVAL;
 stored=malloc(DEMO_MAX_JSON+1);expected=malloc(DEMO_MAX_JSON+1);if(!stored||!expected){rc=-ENOMEM;goto done;}
 rc=demo_store_read_result(store,r->run.run_id,stored,DEMO_MAX_JSON+1,&bytes);if(rc)goto done;
 demo_sha_hex(stored,bytes,sha);if(strcmp(sha,r->source_result_sha)){rc=-ESTALE;goto done;}
 rc=demo_write_result(r,expected,DEMO_MAX_JSON+1,&n);if(rc)goto done;if(n!=bytes||memcmp(stored,expected,n)){rc=-ESTALE;goto done;}
 o=json_tokener_parse(stored);if(!o){rc=-EINVAL;goto done;}numeric=get(o,"numeric_class");scope=get(o,"scope");if(equal(numeric,"FAIL")){rc=-EACCES;goto done;}
 a=get(o,"artifacts");input=find(a,"input","RGB8");rgb=find(a,"display","RGB8");raw=find(a,"output",NULL);if(!input||!rgb||!raw){rc=-ENODATA;goto done;}
 rc=read_rgb(store,r->run.run_id,input,&in);if(rc)goto done;rc=read_rgb(store,r->run.run_id,rgb,&result);if(rc)goto done;
 rc=read_asset(store,r->run.run_id,raw,&raw_data,&raw_bytes);if(rc)goto done;
 strcpy(in.run_id,r->run.run_id);strcpy(result.run_id,r->run.run_id);strcpy(in.source_result_sha,r->source_result_sha);strcpy(result.source_result_sha,r->source_result_sha);
 strcpy(in.raw_source_sha,json_object_get_string(get(input,"sha256")));strcpy(result.raw_source_sha,json_object_get_string(get(raw,"sha256")));
 rc=demo_frame_alloc(1280,720,0,&canvas);if(rc)goto done;rc=demo_compose(&in,&result,layout,&canvas);if(rc)goto done;
 snprintf(banner,sizeof(banner),"%s %s %s RUN %s",r->run.app==DEMO_HUNET?"HUNET":"MOSAIC",equal(scope,"REAL_BOARD")?"LIVE":"OFFLINE",json_object_get_string(numeric),r->run.run_id);
 for(unsigned y=0;y<18;++y)memset(canvas.pixels+y*canvas.stride,0,canvas.stride);
 rc=demo_draw_ascii(&canvas,banner,8,3,1,0xffbd60);if(rc)goto done;rc=demo_frame_hash(&canvas);if(rc)goto done;*out=canvas;memset(&canvas,0,sizeof(canvas));
done:
 free(raw_data);demo_frame_free(&in);demo_frame_free(&result);demo_frame_free(&canvas);json_object_put(o);free(stored);free(expected);return rc;
}

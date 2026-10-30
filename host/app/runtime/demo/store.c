#define _GNU_SOURCE
#include <vxl/demo_runner.h>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
static int ishex(const char *s,size_t n){size_t i;if(strlen(s)!=n)return 0;for(i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static ssize_t system_write(void *p,int fd,const void *s,size_t n){(void)p;return write(fd,s,n);}
static int system_sync(void *p,int fd){(void)p;return fsync(fd)?-errno:0;}
static int system_publish(void *p,int fd,const char *temp,const char *dest){
 (void)p;
 /* link is an atomic no-clobber publication on the same filesystem. */
 if(linkat(fd,temp,fd,dest,0))return -errno;
 if(unlinkat(fd,temp,0))return -errno;
 return 0;
}
static int allwrite(struct demo_store *s,int fd,const void *data,size_t n){
 const char *p=data;ssize_t k;
 while(n){k=s->io.write(s->io.context,fd,p,n);if(k<0){if(errno==EINTR)continue;return -errno;}if(!k||(size_t)k>n)return -EIO;p+=k;n-=(size_t)k;}
 return 0;
}
static int subdir(struct demo_store *s,const char *name){
 int fd;struct stat st;
 if(mkdirat(s->dir_fd,name,0700)&&errno!=EEXIST)return -errno;
 fd=openat(s->dir_fd,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -errno;
 if(fstat(fd,&st)||st.st_uid!=geteuid()||(st.st_mode&0022)){close(fd);return -EACCES;}
 return fd;
}
static int atom(struct demo_store *s,int directory,const char *name,const void *data,size_t n){
 char temp[96];struct timespec ts;int fd,rc;
 if(clock_gettime(CLOCK_MONOTONIC,&ts))return -errno;
 snprintf(temp,sizeof(temp),".part-%ld-%lld-%ld",(long)getpid(),(long long)ts.tv_sec,ts.tv_nsec);
 fd=openat(directory,temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 if(fd<0)return -errno;
 rc=allwrite(s,fd,data,n);if(!rc)rc=s->io.sync(s->io.context,fd);
 if(close(fd)&&!rc)rc=-errno;
 if(!rc)rc=s->io.publish(s->io.context,directory,temp,name);
 if(!rc)rc=s->io.sync(s->io.context,directory);
 /* Failed .part files intentionally remain available for error investigation. */
 return rc;
}
void demo_sha_hex(const void *p,size_t n,char out[65]){
 unsigned char hash[32];static const char digits[]="0123456789abcdef";unsigned i;
 vxl_hunet_hash(p,n,hash);for(i=0;i<32;i++){out[2*i]=digits[hash[i]>>4];out[2*i+1]=digits[hash[i]&15];}out[64]=0;
}
int demo_store_open(struct demo_store *s,const char *path,const struct demo_io_ops *io){
 int fd,journal;struct stat st;FILE *stream;char line[512];uint64_t sequence=0;
 if(!s||!path)return -EINVAL;
 memset(s,0,sizeof(*s));s->dir_fd=-1;
 fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
 if(fstat(fd,&st)||st.st_uid!=geteuid()||(st.st_mode&0022)){close(fd);return -EACCES;}
 s->dir_fd=fd;if(io)s->io=*io;
 if(!s->io.write)s->io.write=system_write;
 if(!s->io.sync)s->io.sync=system_sync;
 if(!s->io.publish)s->io.publish=system_publish;
 journal=openat(fd,"events.jsonl",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(journal<0)return errno==ENOENT?0:(demo_store_close(s),-errno);
 if(flock(journal,LOCK_SH)){int error=-errno;close(journal);demo_store_close(s);return error;}
 stream=fdopen(journal,"r");if(!stream){close(journal);demo_store_close(s);return -errno;}
 while(fgets(line,sizeof(line),stream)){
  struct json_object *o,*v;
  if(!strchr(line,'\n')){fclose(stream);demo_store_close(s);return -EIO;}
  o=json_tokener_parse(line);v=o?json_object_object_get(o,"sequence"):NULL;
  if(!v||!json_object_is_type(v,json_type_int)||json_object_get_uint64(v)!=sequence+1){json_object_put(o);fclose(stream);demo_store_close(s);return -EIO;}
  sequence++;json_object_put(o);
 }
 if(ferror(stream)){fclose(stream);demo_store_close(s);return -EIO;}
 fclose(stream);s->sequence=sequence;return 0;
}
void demo_store_close(struct demo_store *s){if(s&&s->dir_fd>=0){close(s->dir_fd);s->dir_fd=-1;}}
static int journal_tail(int fd,uint64_t *sequence){
 struct stat st;char tail[513];size_t count,start;ssize_t got;struct json_object *o,*v;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0)return -EIO;
 if(!st.st_size){*sequence=0;return 0;}
 count=st.st_size>512?512:(size_t)st.st_size;
 do{got=pread(fd,tail,count,st.st_size-(off_t)count);}while(got<0&&errno==EINTR);
 if(got!=(ssize_t)count||tail[count-1]!='\n')return -EIO;
 tail[count]=0;start=count-1;
 while(start&&tail[start-1]!='\n')start--;
 if(!start&&st.st_size>(off_t)count)return -EIO;
 o=json_tokener_parse(tail+start);v=o?json_object_object_get(o,"sequence"):NULL;
 if(!v||!json_object_is_type(v,json_type_int)||json_object_get_int64(v)<0){json_object_put(o);return -EIO;}
 *sequence=json_object_get_uint64(v);json_object_put(o);return 0;
}
int demo_event(struct demo_store *s,const char *id,const char *event){
 char line[256];size_t i;int fd,n,rc;struct stat st;uint64_t sequence=0;
 if(!s||s->dir_fd<0||!id||!ishex(id,32)||!event||!strlen(event)||strlen(event)>64||s->sequence==UINT64_MAX)return -EINVAL;
 for(i=0;event[i];i++)if(!((event[i]>='A'&&event[i]<='Z')||event[i]=='_'))return -EINVAL;
 fd=openat(s->dir_fd,"events.jsonl",O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -errno;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return -EINVAL;}
 if(flock(fd,LOCK_EX)){rc=-errno;close(fd);return rc;}
 rc=journal_tail(fd,&sequence);if(rc||sequence<s->sequence||sequence==UINT64_MAX){close(fd);return rc?rc:-EOVERFLOW;}
 n=snprintf(line,sizeof(line),"{\"sequence\":%llu,\"run_id\":\"%s\",\"event\":\"%s\"}\n",(unsigned long long)(sequence+1),id,event);
 rc=allwrite(s,fd,line,(size_t)n);if(!rc)rc=s->io.sync(s->io.context,fd);
 if(!rc)rc=s->io.sync(s->io.context,s->dir_fd);
 if(close(fd)&&!rc)rc=-errno;
 if(!rc)s->sequence=sequence+1;
 return rc;
}
int demo_stage(struct demo_store *s,const struct demo_asset *a,int source){
 struct stat st;unsigned char *data;char digest[65];uint64_t total=0,product;uint32_t i;ssize_t n;int dir,rc;
 if(!s||!a||!ishex(a->sha256,64)||!a->bytes||a->bytes>DEMO_MAX_ASSET||!a->rank||a->rank>4||!a->element_bytes)return -EINVAL;
 product=a->element_bytes;
 for(i=0;i<a->rank;i++){if(!a->shape[i]||product>UINT64_MAX/a->shape[i])return -EINVAL;product*=a->shape[i];}
 if(product!=a->bytes||fstat(source,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=a->bytes)return -EINVAL;
 data=malloc((size_t)a->bytes);if(!data)return -ENOMEM;
 while(total<a->bytes){n=pread(source,data+total,(size_t)(a->bytes-total),(off_t)total);if(n<0&&errno==EINTR)continue;if(n<=0){free(data);return n<0?-errno:-EIO;}total+=(uint64_t)n;}
 demo_sha_hex(data,(size_t)total,digest);
 if(strcmp(digest,a->sha256)){free(data);return -EBADMSG;}
 dir=subdir(s,"assets");if(dir<0){free(data);return dir;}
 rc=atom(s,dir,a->sha256,data,(size_t)total);close(dir);free(data);return rc;
}
int demo_publish(struct demo_store *s,const struct demo_result *r){
 char *json,digest[65];size_t n=0;int rc,dir;
 if(!s||!r||!ishex(r->source_result_sha,64)||r->run.state!=DEMO_RESULT_VALIDATED)return -EINVAL;
 json=malloc(DEMO_MAX_JSON+1);if(!json)return -ENOMEM;
 rc=demo_write_result(r,json,DEMO_MAX_JSON+1,&n);
 if(!rc){demo_sha_hex(json,n,digest);if(strcmp(digest,r->source_result_sha))rc=-EBADMSG;}
 if(rc){free(json);return rc;}
 dir=subdir(s,r->run.run_id);if(dir<0){free(json);return dir;}
 rc=atom(s,dir,"compute_result.json",json,n);close(dir);free(json);
 if(!rc)rc=demo_event(s,r->run.run_id,"RESULT_VALIDATED");
 return rc;
}
int demo_record_request(struct demo_store *s,const struct demo_run *r){
 int directory,rc;
 if(!s||!r||!ishex(r->run_id,32))return -EINVAL;
 directory=subdir(s,r->run_id);if(directory<0)return directory;
 rc=atom(s,directory,"request.json",r->json,r->json_bytes);close(directory);return rc;
}
int demo_store_run_asset(struct demo_store *s,const char *id,const char *name,const void *data,size_t bytes){
 int directory,rc;size_t i;
 if(!s||!id||!ishex(id,32)||!name||!data||!bytes||bytes>DEMO_MAX_ASSET||strlen(name)>128||strstr(name,"..")||(strncmp(name,"frame-",6)&&strncmp(name,"error-",6)))return -EINVAL;
 for(i=0;name[i];i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='-'||name[i]=='.'))return -EINVAL;
 directory=subdir(s,id);if(directory<0)return directory;
 rc=atom(s,directory,name,data,bytes);
 if(rc==-EEXIST){
  int fd=openat(directory,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;uint8_t buffer[4096];size_t offset=0;
  if(fd<0){close(directory);return -errno;}
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=bytes)rc=-EEXIST;
  else {
   rc=0;
   while(offset<bytes){size_t want=bytes-offset>sizeof(buffer)?sizeof(buffer):bytes-offset;ssize_t got=read(fd,buffer,want);
    if(got<0&&errno==EINTR)continue;
    if(got<=0||memcmp(buffer,(const uint8_t *)data+offset,(size_t)got)){rc=-EEXIST;break;}
    offset+=(size_t)got;
   }
  }
  close(fd);
 }
 close(directory);return rc;
}
int demo_store_read_result(struct demo_store *s,const char *id,char *data,size_t cap,size_t *bytes){
 int directory,fd,rc=0;struct stat st;size_t offset=0;
 if(!s||!id||!ishex(id,32)||!data||!bytes)return -EINVAL;
 directory=openat(s->dir_fd,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return -errno;
 fd=openat(directory,"compute_result.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);close(directory);if(fd<0)return -errno;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>DEMO_MAX_JSON||(uint64_t)st.st_size>=cap){close(fd);return -EINVAL;}
 while(offset<(size_t)st.st_size){ssize_t n=read(fd,data+offset,(size_t)st.st_size-offset);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}offset+=(size_t)n;}
 if(!rc){data[offset]=0;*bytes=offset;}
 close(fd);return rc;
}
int demo_restore_pending(struct demo_store *s,struct demo_run *r){
 DIR *dirs;struct dirent *entry;int copied=0,rc=0,fd;char *data;
 if(!s||!r)return -EINVAL;
 fd=dup(s->dir_fd);if(fd<0)return -errno;
 dirs=fdopendir(fd);if(!dirs){close(fd);return -errno;}
 data=malloc(DEMO_MAX_JSON+1);if(!data){closedir(dirs);return -ENOMEM;}
 while((entry=readdir(dirs))){
  int directory,request,complete;ssize_t bytes;struct stat st;
  if(!ishex(entry->d_name,32))continue;
  directory=openat(s->dir_fd,entry->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(directory<0){rc=-errno;break;}
  complete=openat(directory,"compute_result.json",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(complete>=0){
   ssize_t n;struct demo_run *checked=calloc(1,sizeof(*checked));
   if(!checked){close(complete);close(directory);rc=-ENOMEM;break;}
   if(fstat(complete,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>DEMO_MAX_JSON){free(checked);close(complete);close(directory);rc=-EIO;break;}
   n=read(complete,data,(size_t)st.st_size);close(complete);
   if(n!=st.st_size||demo_parse_run(data,(size_t)n,checked)||checked->state!=DEMO_RESULT_VALIDATED||strcmp(checked->run_id,entry->d_name))rc=-EIO;
   free(checked);close(directory);if(rc)break;continue;
  }
  if(errno!=ENOENT){rc=-errno;close(directory);break;}
  request=openat(directory,"request.json",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);close(directory);
  if(request<0){if(errno==ENOENT)continue;rc=-errno;break;}
  if(fstat(request,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>DEMO_MAX_JSON){close(request);rc=-EIO;break;}
  bytes=read(request,data,(size_t)st.st_size);close(request);
  if(bytes!=st.st_size||copied||demo_parse_run(data,(size_t)bytes,r)||strcmp(r->run_id,entry->d_name)){rc=-EIO;break;}
  copied=1;
 }
 free(data);closedir(dirs);return rc?rc:copied;
}

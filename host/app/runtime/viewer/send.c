#define _GNU_SOURCE
#include <vxl/demo_frame.h>
#include <json-c/json.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static uint64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int ready(int fd,short events,uint64_t deadline){for(;;){uint64_t now=now_ms();if(!now||now>=deadline)return -ETIMEDOUT;struct pollfd p={fd,events,0};int rc=poll(&p,1,(int)(deadline-now));if(rc<0&&errno==EINTR)continue;if(rc<0)return -errno;if(!rc)return -ETIMEDOUT;if(p.revents&events)return 0;return -EIO;}}
int demo_send_display_reference(struct demo_store *s,const char *path,const struct demo_frame *frame){
 struct sockaddr_un address={0};struct stat st;struct ucred peer={0};socklen_t size=sizeof(peer);char parent[108],request[512],reply[1024]={0},name[100],sha[65];size_t written=0,used=0;uint64_t begin=now_ms(),deadline=begin+10000;int fd=-1,rc=0,n,original=0;struct json_object *o=NULL,*exit_code,*scope;struct json_tokener *tok=NULL;
 if(!s||!path||!frame||demo_frame_check(frame,1)||strlen(path)>=sizeof(address.sun_path)||path[0]!='/'||strstr(path,"..")||strlen(frame->run_id)!=32||strlen(frame->source_result_sha)!=64||frame->layout<1||frame->layout>3||!begin)return -EINVAL;
 demo_sha_hex(frame->pixels,frame->bytes,sha);if(strcmp(sha,frame->sha256))return -EBADMSG;
 strcpy(parent,path);char *slash=strrchr(parent,'/');if(!slash||slash==parent)return -EINVAL;*slash=0;
 if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077))return -EACCES;
 if(lstat(path,&st)||!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022))return -EACCES;
 n=snprintf(request,sizeof(request),"{\"run_id\":\"%s\",\"frame_sha\":\"%s\",\"source_result_sha\":\"%s\",\"composition\":\"%s\"}\n",frame->run_id,frame->sha256,frame->source_result_sha,frame->layout==1?"RESULT":frame->layout==2?"INPUT":"COMPARE");if(n<=0||(size_t)n>=sizeof(request))return -EINVAL;
 snprintf(name,sizeof(name),"frame-display-request-%016llx.json",(unsigned long long)begin);rc=demo_store_run_asset(s,frame->run_id,name,request,(size_t)n);if(rc)return rc;
 fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)return -errno;address.sun_family=AF_UNIX;strcpy(address.sun_path,path);
 if(connect(fd,(struct sockaddr*)&address,sizeof(address))){if(errno!=EINPROGRESS){rc=-errno;goto done;}rc=ready(fd,POLLOUT,deadline);if(rc)goto done;size=sizeof(original);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&original,&size)||original){rc=original?-original:-errno;goto done;}}
 size=sizeof(peer);if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)||peer.uid!=geteuid()){rc=-EACCES;goto done;}
 while(written<(size_t)n){rc=ready(fd,POLLOUT,deadline);if(rc)goto done;ssize_t k=send(fd,request+written,(size_t)n-written,MSG_NOSIGNAL);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0){rc=k<0?-errno:-EIO;goto done;}written+=(size_t)k;}
 while(used<sizeof(reply)-1){rc=ready(fd,POLLIN,deadline);if(rc)break;ssize_t k=recv(fd,reply+used,sizeof(reply)-1-used,0);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0){rc=k<0?-errno:-EIO;break;}used+=(size_t)k;if(memchr(reply,'\n',used))break;}
 if(used){snprintf(name,sizeof(name),"frame-display-reply-%016llx.json",(unsigned long long)begin);int saved=demo_store_run_asset(s,frame->run_id,name,reply,used);if(!rc)rc=saved;}
 if(rc)goto done;
 if(!used||reply[used-1]!='\n'||memchr(reply,'\n',used)!=reply+used-1){rc=-EBADMSG;goto done;}
 tok=json_tokener_new_ex(8);if(!tok){rc=-ENOMEM;goto done;}json_tokener_set_flags(tok,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);o=json_tokener_parse_ex(tok,reply,(int)used-1);
 if(json_tokener_get_error(tok)!=json_tokener_success||json_tokener_get_parse_end(tok)!=used-1||!o||!json_object_is_type(o,json_type_object)||json_object_object_length(o)!=2){rc=-EBADMSG;goto done;}
 exit_code=json_object_object_get(o,"exit");scope=json_object_object_get(o,"scope");if(!exit_code||!json_object_is_type(exit_code,json_type_int)||!scope||!json_object_is_type(scope,json_type_string)||strcmp(json_object_get_string(scope),"DISPLAY_ONLY_NO_INFERENCE")||json_object_get_int64(exit_code)>0||json_object_get_int64(exit_code)<-4095){rc=-EBADMSG;goto done;}rc=json_object_get_int(exit_code);
done:
 if(tok){json_tokener_free(tok);}
 json_object_put(o);
 if(fd>=0){close(fd);}
 return rc; /* IPC completion is not a fabricated DISPLAY_PRESENTED timestamp. */
}

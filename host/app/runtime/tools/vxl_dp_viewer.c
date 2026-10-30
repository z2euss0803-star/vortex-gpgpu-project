#define _GNU_SOURCE
#include "../viewer/kms.h"
#include "../viewer/ipc.h"
#include <json-c/json.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct server {struct demo_store store;struct dp_context dp;struct dp_ops ops;};
static volatile sig_atomic_t stopping;
static void stop(int signal_number){(void)signal_number;stopping=1;}
static int observe(void *p,const struct dp_event *event){
 (void)p;
  struct json_object *e=json_object_new_object();
  json_object_object_add(e,"event",json_object_new_string(dp_event_name(event->kind)));json_object_object_add(e,"run_id",json_object_new_string(event->run_id));json_object_object_add(e,"frame_sha",json_object_new_string(event->frame_sha));json_object_object_add(e,"received_monotonic_ns",json_object_new_uint64(event->received_ns));json_object_object_add(e,"kernel_timestamp_ns",json_object_new_uint64(event->kernel_ns));json_object_object_add(e,"kernel_clock_monotonic",json_object_new_boolean(event->kernel_clock_known));
  json_object_object_add(e,"scanout_sha",json_object_new_string(event->scanout_sha));json_object_object_add(e,"scanout_format",json_object_new_string("ARGB8888_LE"));json_object_object_add(e,"width",json_object_new_int(event->width));json_object_object_add(e,"height",json_object_new_int(event->height));json_object_object_add(e,"pitch",json_object_new_int(event->pitch));
  puts(json_object_to_json_string_ext(e,JSON_C_TO_STRING_PLAIN));fflush(stdout);json_object_put(e);
 return 0;
}
static int display(void *p,const char *request,size_t bytes){
 struct server *s=p;struct demo_frame frame={0};int rc=dp_load_reference(&s->store,request,bytes,&frame);
 if(!rc)rc=dp_present_frame(&s->dp,&s->ops,&s->store,&frame,observe,NULL);
 demo_frame_free(&frame);return rc;
}
int main(int argc,char **argv){
 struct server s={0};struct sockaddr_un address={0};struct stat st;char lock_path[128],parent[108];int listener=-1,lock=-1,rc=1;
 if(argc!=5||strcmp(argv[1],"--serve")){fprintf(stderr,"usage: vxl_dp_viewer --serve STORE SOCKET /dev/dri/cardN\n");return 2;}
 /* argv4 is a bounded /dev/dri/cardN path; no device is opened until a valid
    published reference is received. This executable is not run in offline CI. */
 if(strlen(argv[3])>=sizeof(address.sun_path)||strlen(argv[3])>=sizeof(parent)||argv[3][0]!='/')return 2;
 strcpy(parent,argv[3]);char *slash=strrchr(parent,'/');if(!slash||slash==parent)return 2;*slash=0;
 if(lstat(parent,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&077))return 2;
 if(lstat(argv[3],&st)==0||errno!=ENOENT)return 2;
 snprintf(lock_path,sizeof(lock_path),"%s.lock",argv[3]);lock=open(lock_path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))goto done;
 if(demo_store_open(&s.store,argv[2],NULL))goto done;
 if(dp_native_ops(&s.ops,argv[4])){demo_store_close(&s.store);goto done;}
 listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)goto close_state;
 address.sun_family=AF_UNIX;strcpy(address.sun_path,argv[3]);umask(077);
 if(bind(listener,(struct sockaddr *)&address,sizeof(address))||listen(listener,4))goto close_state;
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,SIG_IGN);rc=0;
 while(!stopping){
  struct pollfd p={listener,POLLIN,0};int ready=poll(&p,1,1000);if(ready<0&&errno==EINTR)continue;if(ready<0){rc=1;break;}if(!ready)continue;
  int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC);if(fd<0){if(errno==EINTR)continue;rc=1;break;}
  int result=dp_serve_connection(fd,getuid(),display,&s);char reply[80];int n=snprintf(reply,sizeof(reply),"{\"exit\":%d,\"scope\":\"DISPLAY_ONLY_NO_INFERENCE\"}\n",result);send(fd,reply,(size_t)n,MSG_NOSIGNAL);close(fd);
 }
 int restore_rc=dp_close(&s.dp);
 if(restore_rc)fprintf(stderr,"DISPLAY_RESTORE_FAILED original_errno=%d\n",restore_rc);
 if(restore_rc)for(;;){
  /* Uncertain scanout ownership: keep mappings and DRM fd alive. Never force
     free a scanned buffer just because the controller disconnected. */
  fprintf(stderr,"DISPLAY_QUARANTINED_BUFFERS_PINNED_NO_AUTOMATIC_MODESET_RETRY\n");poll(NULL,0,1000);
 }
close_state:
 if(listener>=0){close(listener);if(!rc)unlink(argv[3]);}
 dp_native_free(&s.ops);demo_store_close(&s.store);
done:
 if(lock>=0)close(lock);
 return rc;
}

#include "../../viewer/ipc.h"
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
static int calls;
static int callback(void *ctx,const char *text,size_t n){(void)ctx;calls++;assert(n==2&&!memcmp(text,"{}",2));return 17;}
int main(void){int pair[2];char long_message[4097];memset(long_message,'x',sizeof(long_message));
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(write(pair[0],"{}\n",3)==3);assert(dp_serve_connection(pair[1],getuid(),callback,NULL)==17&&calls==1);close(pair[0]);close(pair[1]);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(dp_serve_connection(pair[1],getuid()+1,callback,NULL)==-EACCES&&calls==1);close(pair[0]);close(pair[1]);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(write(pair[0],long_message,sizeof(long_message))==sizeof(long_message));assert(dp_serve_connection(pair[1],getuid(),callback,NULL)==-EMSGSIZE&&calls==1);close(pair[0]);close(pair[1]);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(write(pair[0],"{}\n{}\n",6)==6);assert(dp_serve_connection(pair[1],getuid(),callback,NULL)==-EMSGSIZE&&calls==1);close(pair[0]);close(pair[1]);
 puts("VIEWER_LOCAL_UID_BOUNDED_IPC_PASS_NOT_DP");return 0;
}

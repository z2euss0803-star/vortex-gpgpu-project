#define _GNU_SOURCE
#include <vxl/demo_runner.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
static ssize_t disk_full(void *p,int f,const void *s,size_t n){(void)p;(void)f;(void)s;(void)n;errno=ENOSPC;return -1;}
static int publish_fail(void *p,int d,const char *a,const char *b){(void)p;(void)d;(void)a;(void)b;return -EIO;}
int main(int argc,char **argv){
 char directory[]="/tmp/demo-store-XXXXXX",path[256],before[65],after[65],*json=calloc(1,DEMO_MAX_JSON+1);
 struct demo_store store;struct demo_result *r=calloc(1,sizeof(*r));struct demo_run *parsed=calloc(1,sizeof(*parsed));
 struct demo_asset asset={0};size_t n,bytes;FILE *f;int fd;struct demo_io_ops io={0};
 assert(argc==2&&json&&r&&parsed&&mkdtemp(directory));f=fopen(argv[1],"rb");assert(f);n=fread(json,1,DEMO_MAX_JSON,f);fclose(f);assert(!demo_parse_run(json,n,&r->run));
 assert(!demo_store_open(&store,directory,NULL));
 assert(demo_publish(&store,r)==-EINVAL); /* No result digest. */
 assert(!demo_write_result(r,json,DEMO_MAX_JSON+1,&bytes));demo_sha_hex(json,bytes,r->source_result_sha);
 assert(!demo_publish(&store,r));
 snprintf(path,sizeof(path),"%s/%s/compute_result.json",directory,r->run.run_id);
 f=fopen(path,"rb");assert(f);n=fread(json,1,DEMO_MAX_JSON,f);fclose(f);demo_sha_hex(json,n,before);
 assert(!demo_event(&store,r->run.run_id,"DISPLAY_SUBMITTED"));
 f=fopen(path,"rb");assert(f);n=fread(json,1,DEMO_MAX_JSON,f);fclose(f);demo_sha_hex(json,n,after);assert(!strcmp(before,after));
 assert(demo_publish(&store,r)==-EEXIST);
 {uint64_t sequence=store.sequence;demo_store_close(&store);assert(!demo_store_open(&store,directory,NULL));assert(store.sequence==sequence);assert(!demo_event(&store,r->run.run_id,"PRESENTED"));assert(store.sequence==sequence+1);}
 {struct demo_store viewer;uint64_t sequence=store.sequence;assert(!demo_store_open(&viewer,directory,NULL));assert(!demo_event(&store,r->run.run_id,"RUNNER_EVENT"));assert(!demo_event(&viewer,r->run.run_id,"VIEWER_EVENT"));assert(viewer.sequence==sequence+2);demo_store_close(&viewer);demo_store_close(&store);assert(!demo_store_open(&store,directory,NULL));assert(store.sequence==sequence+2);}
 snprintf(path,sizeof(path),"%s/input",directory);fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(write(fd,"abc",3)==3);
 strcpy(asset.name,"input.bin");strcpy(asset.sha256,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");asset.bytes=3;asset.rank=1;asset.shape[0]=3;asset.element_bytes=1;
 assert(!demo_stage(&store,&asset,fd));
 asset.bytes=4;assert(demo_stage(&store,&asset,fd)<0);asset.bytes=3;
 io.write=disk_full;demo_store_close(&store);assert(!demo_store_open(&store,directory,&io));
 r->run.json[0]='x';assert(demo_publish(&store,r)<0); /* malformed metadata never published */
 assert(!demo_parse_run(json,n,&r->run));
 r->run.run_id[0]='2';assert(demo_publish(&store,r)<0); /* cached struct vs JSON drift */
 r->run.run_id[0]='1';
 /* Disk full and atomic-publication failure tested on a distinct run root. */
 {char d2[]="/tmp/demo-full-XXXXXX";assert(mkdtemp(d2));demo_store_close(&store);assert(!demo_store_open(&store,d2,&io));assert(demo_publish(&store,r)==-ENOSPC);demo_store_close(&store);}
 io.write=NULL;io.publish=publish_fail;
 {char d3[]="/tmp/demo-rename-XXXXXX";assert(mkdtemp(d3));assert(!demo_store_open(&store,d3,&io));assert(demo_publish(&store,r)==-EIO);demo_store_close(&store);}
 close(fd);free(json);free(r);free(parsed);
 puts("DEMO_STORE_ATOMICITY_HOST_PASS_NOT_BOARD");return 0;
}

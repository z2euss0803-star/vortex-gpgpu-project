#define _GNU_SOURCE
#include <vxl/demo_timing.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void){char dir[]="/tmp/demo-error-XXXXXX";struct demo_store s;assert(mkdtemp(dir));assert(!demo_store_open(&s,dir,NULL));struct demo_error e={"11111111111111111111111111111111","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","offline-test","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","first failure",1,0,0,0,0};
 assert(!demo_error_save(&s,&e));e.message="different failure";assert(demo_error_save(&s,&e)<0);e.error_id="dddddddddddddddddddddddddddddddd";assert(!demo_error_save(&s,&e));e.error_id="../escape";assert(demo_error_save(&s,&e)<0);e.error_id="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";e.raw_observed=1;assert(demo_error_save(&s,&e)<0);
 char path[1024],data[4096];snprintf(path,sizeof(path),"%s/%s/error-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.json",dir,e.run_id);FILE *f=fopen(path,"rb");assert(f);size_t n=fread(data,1,sizeof(data)-1,f);data[n]=0;fclose(f);assert(strstr(data,"first failure")&&!strstr(data,"different failure"));demo_store_close(&s);puts("IMMUTABLE_ERROR_RECEIPTS_HOST_PASS");return 0;}

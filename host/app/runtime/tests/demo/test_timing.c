#include <vxl/demo_timing.h>
#include <json-c/json.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void){struct demo_timeline *t=calloc(1,sizeof(*t));char *text=malloc(262144);size_t n;assert(t&&text);
 assert(!demo_time_record(t,DEMO_GPU_SUBMIT_WAIT,100,500,DEMO_A53_MONOTONIC,1));assert(!demo_time_record_child(t,DEMO_GUARD_CDMA,110,200,DEMO_A53_MONOTONIC,1,0));assert(demo_time_record_child(t,DEMO_POLL,90,220,DEMO_A53_MONOTONIC,1,0)<0);assert(demo_time_record_child(t,DEMO_POLL,210,300,DEMO_B_MONOTONIC,1,0)<0);
 assert(demo_time_record(t,DEMO_CPU_POST,9,1,DEMO_A53_MONOTONIC,1)<0);assert(t->count==2);
 assert(demo_gpu_cycles_record(t,7,1,1,1,1)<0&&t->cycle_count==0); /* offline must not manufacture a counter */
 assert(!demo_timeline_write(t,text,262144,&n));struct json_object *o=json_tokener_parse(text);assert(o);assert(!json_object_object_get(o,"raw_gpu_cycles"));json_object_put(o);
 t->real_board=1;t->generation=9;
 assert(demo_gpu_cycles_capture(t,127273,1,9,3,1,0,0)<0);assert(t->cycle_count==1&&t->cycles[0].raw==127273&&!t->cycles[0].valid);
 assert(demo_gpu_cycles_capture(t,UINT32_MAX,1,9,4,1,1,1)<0);assert(t->cycles[1].raw==UINT32_MAX&&!t->cycles[1].valid);
 assert(demo_gpu_cycles_record(t,123,1,9,5,0)<0);assert(t->cycle_count==3&&!t->cycles[2].valid);
 assert(!demo_gpu_cycles_record(t,456,1,9,6,1));assert(t->cycles[3].valid);assert(demo_gpu_cycles_record(t,789,1,9,6,1)<0&&t->cycle_count==4);
 assert(!demo_timeline_write(t,text,262144,&n));assert(strstr(text,"127273")&&strstr(text,"4294967295"));free(text);free(t);puts("RAW_CYCLE_TIMELINE_HOST_FIXTURE_PASS_NOT_BOARD");return 0;}

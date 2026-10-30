#include <vxl/demo_frame.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 struct demo_frame a={0},b={0},canvas={0};
 assert(!demo_frame_alloc(1,1,0,&a));assert(!demo_frame_alloc(1,1,0,&b));assert(!demo_frame_alloc(4,2,0,&canvas));
 a.pixels[0]=255;b.pixels[2]=255;
 memset(a.raw_source_sha,'c',64);a.raw_source_sha[64]=0;
 strcpy(b.run_id,"11111111111111111111111111111111");memset(b.source_result_sha,'a',64);b.source_result_sha[64]=0;
 memset(b.raw_source_sha,'b',64);b.raw_source_sha[64]=0;
 assert(!demo_compose(&a,&b,DEMO_LAYOUT_COMPARE,&canvas));
 assert(canvas.pixels[0]==255&&canvas.pixels[2]==0&&canvas.pixels[6]==0&&canvas.pixels[8]==255);
 assert(!strcmp(canvas.run_id,b.run_id));assert(!strcmp(canvas.source_result_sha,b.source_result_sha));
 assert(!strcmp(canvas.input_source_sha,a.raw_source_sha));
 memset(b.run_id,'2',32);
 assert(!demo_compose(NULL,&b,DEMO_LAYOUT_RESULT,&canvas));
 assert(!strcmp(canvas.run_id,b.run_id)&&!canvas.input_source_sha[0]);
 assert(demo_compose(&a,&b,0,&canvas)<0);
 assert(demo_compose(NULL,&b,DEMO_LAYOUT_COMPARE,&canvas)<0);
 demo_frame_free(&a);demo_frame_free(&b);demo_frame_free(&canvas);puts("OFFSCREEN_COMPOSITION_HOST_PASS_NOT_DP");return 0;
}

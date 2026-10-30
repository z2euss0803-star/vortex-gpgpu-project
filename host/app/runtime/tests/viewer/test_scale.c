#include <vxl/demo_frame.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 struct demo_frame source={0},dest={0};struct demo_rect fit={0},box={0};
 assert(!demo_frame_alloc(4,3,0,&source));assert(!demo_frame_alloc(8,8,0,&dest));
 memset(source.pixels,255,source.bytes);char before[65],after[65];demo_sha_hex(source.pixels,source.bytes,before);
 assert(!demo_letterbox(&source,&dest,(struct demo_rect){0,0,8,8},&fit));
 assert(fit.x==0&&fit.y==1&&fit.width==8&&fit.height==6);
 for(unsigned x=0;x<24;x++)assert(!dest.pixels[x]);
 for(unsigned x=0;x<24;x++)assert(dest.pixels[dest.stride+x]==255);
 struct demo_bbox roi={1,1,3,3,4,3,0};assert(!demo_map_bbox(&roi,fit,&box));
 assert(box.x==2&&box.y==3&&box.width==4&&box.height==4);
 demo_sha_hex(source.pixels,source.bytes,after);assert(!strcmp(before,after));
 roi.x0=-1;assert(demo_map_bbox(&roi,fit,&box)<0);
 assert(demo_letterbox(&source,&dest,(struct demo_rect){7,7,8,8},&fit)<0);
 demo_frame_free(&source);demo_frame_free(&dest);puts("OFFSCREEN_LETTERBOX_HOST_PASS_NOT_DP");return 0;
}

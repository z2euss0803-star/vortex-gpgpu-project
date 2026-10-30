#include <vxl/demo_frame.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 struct demo_hunet_result result={0};struct demo_frame frame={0},source={0};
 assert(demo_frame_from_hunet(&result,&frame)<0);
 assert(!demo_frame_alloc(4,3,0,&source));memset(source.pixels,255,source.bytes);assert(!demo_frame_hash(&source));
 result.completed=1;result.view_scope=DEMO_VIEW_HISTORICAL;result.source_frame=&source;result.bbox=(struct demo_bbox){1,1,3,3,4,3,0};
 result.numeric_class=DEMO_NUMERIC_KNOWN_DEVIATION;result.history_byte_equal=1;result.ort_evaluated=1;result.r2_evaluated=1;result.r2_pass=1;
 strcpy(result.run_id,"11111111111111111111111111111111");memset(result.source_result_sha,'a',64);memset(result.logits_sha,'b',64);
 for(unsigned i=0;i<5;i++){result.top[i].class_id=i;snprintf(result.glyphs[i].fallback,sizeof(result.glyphs[i].fallback),"#%u",i);}
 assert(!demo_frame_from_hunet(&result,&frame));assert(frame.width==1280&&frame.height==720);
 assert(!strcmp(frame.raw_source_sha,result.logits_sha));assert(!strcmp(frame.run_id,result.run_id));
 /* Original image is a display copy and the red bbox starts at mapped (240,240). */
 assert(frame.pixels[(size_t)240*frame.stride+3*240]==255);assert(frame.pixels[(size_t)240*frame.stride+3*240+1]==0);
 assert(source.pixels[0]==255&&source.pixels[1]==255&&source.pixels[2]==255);
 uint8_t pixel=255;struct demo_glyph g={.width=1,.height=1,.stride=1,.pixels=&pixel,.available=1};
 assert(demo_draw_glyph(&frame,&g,1279,719,2)<0);
 assert(demo_draw_ascii(&frame,"中文",0,0,1,0xffffff)<0);
 demo_frame_free(&frame);assert(!demo_frame_alloc(640,480,0,&frame));
 for(unsigned i=0;i<5;i++){result.top[i].class_id=8104-i;result.top[i].score=-123.4567f;snprintf(result.glyphs[i].fallback,sizeof(result.glyphs[i].fallback),"#%u",8104-i);}
 assert(!demo_frame_from_hunet(&result,&frame));
 demo_frame_free(&source);demo_frame_free(&frame);puts("HUNET_OFFSCREEN_UI_HOST_PASS_NOT_DP_NOT_INFERENCE");return 0;
}

#include <vxl/demo_frame.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
int main(void){
 /* CHW red, green, blue. Guard and odd row padding must remain untouched. */
 float chw[]={255,0,0,0,255,0,0,0,255};uint8_t memory[17];memset(memory,0x7f,sizeof(memory));
 struct demo_frame f={0};f.width=3;f.height=1;f.stride=11;f.bytes=11;f.pixels=memory+3;
 const uint8_t expected[]={255,0,0,0,255,0,0,0,255};
 assert(!demo_frame_from_mosaic(chw,3,1,&f));assert(!memcmp(f.pixels,expected,9));
 for(unsigned i=0;i<3;i++)assert(memory[i]==0x7f);
 assert(memory[12]==0x7f&&memory[13]==0x7f&&memory[14]==0x7f&&memory[16]==0x7f);
 float clamp[]={-1,255.99f,1.99f,256,0,-.1f};struct demo_frame g={0};
 assert(!demo_frame_from_mosaic(clamp,2,1,&g));
 const uint8_t want[]={0,1,0,255,255,0};assert(!memcmp(g.pixels,want,6));
 clamp[0]=NAN;assert(demo_frame_from_mosaic(clamp,2,1,&g)<0);
 assert(demo_frame_alloc(UINT32_MAX,UINT32_MAX,0,&g)<0);
 assert(demo_frame_alloc(0,1,0,&g)<0);
 struct demo_frame huge={0};assert(demo_frame_alloc(1921,1,0,&huge)<0);
 float invalid_shape_probe=NAN;
 assert(demo_frame_from_mosaic(&invalid_shape_probe,1921,1,&huge)==-ERANGE);
 demo_frame_free(&g);puts("OFFSCREEN_FRAME_PIXELS_HOST_PASS_NOT_DP");return 0;
}

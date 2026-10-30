#include <vxl/demo_frame.h>
#include <errno.h>
#include <string.h>
#include "../generated/demo/ascii.h"
static void colour(struct demo_frame *frame,uint32_t x,uint32_t y,uint8_t alpha,uint32_t rgb){
 uint8_t *p=frame->pixels+(size_t)y*frame->stride+3*x;
 for(unsigned c=0;c<3;c++){unsigned value=(rgb>>(16-8*c))&255;p[c]=(uint8_t)((value*alpha+p[c]*(255-alpha)+127)/255);}
}
int demo_draw_ascii(struct demo_frame *frame,const char *text,uint32_t x,uint32_t y,uint32_t scale,uint32_t rgb){
 size_t n=0,i;uint32_t row,col,dx,dy;
 if(demo_frame_check(frame,1)||!text||!scale||scale>8)return -EINVAL;
 while(n<=128&&text[n]){if((unsigned char)text[n]<32||(unsigned char)text[n]>126)return -EINVAL;n++;}
 if(n>128||(uint64_t)x+n*8*scale>frame->width||(uint64_t)y+16*scale>frame->height)return -ERANGE;
 for(i=0;i<n;i++)for(row=0;row<16;row++)for(col=0;col<8;col++)if(demo_ascii_font[(unsigned char)text[i]-32][row]&(1u<<(7-col))){
  for(dy=0;dy<scale;dy++)for(dx=0;dx<scale;dx++)colour(frame,x+(uint32_t)i*8*scale+col*scale+dx,y+row*scale+dy,255,rgb);
 }
 return 0;
}
int demo_draw_glyph(struct demo_frame *frame,const struct demo_glyph *glyph,uint32_t x,uint32_t y,uint32_t scale){
 uint32_t row,col,dx,dy;
 if(demo_frame_check(frame,1)||!glyph||!scale||scale>8)return -EINVAL;
 if(!glyph->available){if(!memchr(glyph->fallback,0,sizeof(glyph->fallback)))return -EINVAL;return demo_draw_ascii(frame,glyph->fallback,x,y,scale,0xffffff);}
 if(glyph->available!=1||!glyph->pixels||!glyph->width||!glyph->height||glyph->width>64||glyph->height>64||glyph->stride<glyph->width)return -EINVAL;
 if((uint64_t)x+(uint64_t)glyph->width*scale>frame->width||(uint64_t)y+(uint64_t)glyph->height*scale>frame->height)return -ERANGE;
 for(row=0;row<glyph->height;row++)for(col=0;col<glyph->width;col++){
  uint8_t alpha=glyph->pixels[(size_t)row*glyph->stride+col];
  for(dy=0;dy<scale;dy++)for(dx=0;dx<scale;dx++)colour(frame,x+col*scale+dx,y+row*scale+dy,alpha,0xffffff);
 }
 return 0;
}

#include <vxl/demo_frame.h>
#include <errno.h>
#include <string.h>
int demo_letterbox(const struct demo_frame *source,struct demo_frame *dest,struct demo_rect region,struct demo_rect *fit){
 uint64_t fw,fh;uint32_t x,y,ox,oy;
 if(!fit||demo_frame_check(source,0)||demo_frame_check(dest,1)||!region.width||!region.height||(uint64_t)region.x+region.width>dest->width||(uint64_t)region.y+region.height>dest->height||demo_frame_overlap(source,dest))return -EINVAL;
 if((uint64_t)region.width*source->height<=(uint64_t)region.height*source->width){fw=region.width;fh=(uint64_t)source->height*fw/source->width;}
 else {fh=region.height;fw=(uint64_t)source->width*fh/source->height;}
 if(!fw)fw=1;
 if(!fh)fh=1;
 ox=region.x+(region.width-(uint32_t)fw)/2;oy=region.y+(region.height-(uint32_t)fh)/2;
 for(y=region.y;y<region.y+region.height;y++)memset(dest->pixels+(size_t)y*dest->stride+3*region.x,0,(size_t)region.width*3);
 for(y=0;y<(uint32_t)fh;y++)for(x=0;x<(uint32_t)fw;x++){
  uint32_t sx=(uint32_t)((uint64_t)x*source->width/fw),sy=(uint32_t)((uint64_t)y*source->height/fh);
  memcpy(dest->pixels+(size_t)(oy+y)*dest->stride+3*(ox+x),source->pixels+(size_t)sy*source->stride+3*sx,3);
 }
 *fit=(struct demo_rect){ox,oy,(uint32_t)fw,(uint32_t)fh};return 0;
}
int demo_map_bbox(const struct demo_bbox *bbox,struct demo_rect fit,struct demo_rect *out){
 uint64_t x0,x1,y0,y1;
 if(!bbox||!out||bbox->x0<0||bbox->y0<0||bbox->x1<=bbox->x0||bbox->y1<=bbox->y0||!bbox->image_width||!bbox->image_height||(uint64_t)bbox->x1>bbox->image_width||(uint64_t)bbox->y1>bbox->image_height||!fit.width||!fit.height||(uint64_t)fit.x+fit.width>1920||(uint64_t)fit.y+fit.height>1080)return -EINVAL;
 x0=(uint64_t)bbox->x0*fit.width/bbox->image_width;x1=((uint64_t)bbox->x1*fit.width+bbox->image_width-1)/bbox->image_width;
 y0=(uint64_t)bbox->y0*fit.height/bbox->image_height;y1=((uint64_t)bbox->y1*fit.height+bbox->image_height-1)/bbox->image_height;
 *out=(struct demo_rect){fit.x+(uint32_t)x0,fit.y+(uint32_t)y0,(uint32_t)(x1-x0),(uint32_t)(y1-y0)};return 0;
}

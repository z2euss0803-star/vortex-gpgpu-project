#include <vxl/demo_frame.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
int demo_frame_check(const struct demo_frame *f,int canvas){
 uint64_t row,bytes;
 if(!f||!f->pixels||!f->width||!f->height)return -EINVAL;
 if(canvas&&(f->width>1920||f->height>1080))return -ERANGE;
 row=(uint64_t)f->width*3;bytes=(uint64_t)f->height*f->stride;
 if(row>f->stride||bytes>DEMO_MAX_ASSET||bytes!=f->bytes)return -ERANGE;
 return 0;
}
int demo_frame_alloc(uint32_t w,uint32_t h,uint32_t stride,struct demo_frame *f){
 uint64_t bytes;
 if(!f||f->pixels)return -EINVAL;
 if(!w||!h||w>1920||h>1080)return -ERANGE;
 if(!stride)stride=w*3;
 if(stride<(uint64_t)w*3)return -ERANGE;
 bytes=(uint64_t)stride*h;if(bytes>DEMO_MAX_ASSET||bytes>SIZE_MAX)return -ERANGE;
 f->pixels=calloc(1,(size_t)bytes);if(!f->pixels)return -ENOMEM;
 f->width=w;f->height=h;f->stride=stride;f->bytes=(size_t)bytes;f->owned=1;return 0;
}
void demo_frame_free(struct demo_frame *f){if(f){if(f->owned)free(f->pixels);memset(f,0,sizeof(*f));}}
int demo_frame_hash(struct demo_frame *f){int rc=demo_frame_check(f,0);if(rc)return rc;demo_sha_hex(f->pixels,f->bytes,f->sha256);return 0;}
int demo_frame_overlap(const struct demo_frame *a,const struct demo_frame *b){
 uintptr_t x,y;
 if(!a||!b||!a->pixels||!b->pixels)return 0;
 x=(uintptr_t)a->pixels;y=(uintptr_t)b->pixels;
 return x<=y?y-x<a->bytes:x-y<b->bytes;
}
int demo_frame_from_mosaic(const float *source,uint32_t w,uint32_t h,struct demo_frame *f){
 uint64_t pixels=(uint64_t)w*h;uint32_t x,y,c;int rc;
 if(!source||!f||!w||!h||pixels>DEMO_MAX_ASSET/3)return -EINVAL;
 if(w>1920||h>1080||pixels>DEMO_MAX_ASSET/(3*sizeof(float)))return -ERANGE;
 for(uint64_t i=0;i<pixels*3;i++)if(!isfinite(source[i]))return -EDOM;
 if(!f->pixels){rc=demo_frame_alloc(w,h,0,f);if(rc)return rc;}
 if(f->width!=w||f->height!=h)return -EINVAL;
 rc=demo_frame_check(f,0);if(rc)return rc;
 for(y=0;y<h;y++)for(x=0;x<w;x++)for(c=0;c<3;c++){
  float value=source[(uint64_t)c*pixels+(uint64_t)y*w+x];
  f->pixels[(size_t)y*f->stride+3*x+c]=value<=0?0:value>=255?255:(uint8_t)value;
 }
 demo_sha_hex(source,(size_t)pixels*3*sizeof(float),f->raw_source_sha);f->layout=DEMO_LAYOUT_RESULT;
 return demo_frame_hash(f);
}

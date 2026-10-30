#include <vxl/demo_frame.h>
#include <errno.h>
#include <string.h>
int demo_compose(const struct demo_frame *input,const struct demo_frame *output,enum demo_layout layout,struct demo_frame *canvas){
 struct demo_rect fit,region;const struct demo_frame *identity;int rc;
 if(demo_frame_check(canvas,1))return -EINVAL;
 region=(struct demo_rect){0,0,canvas->width,canvas->height};
 if(layout==DEMO_LAYOUT_RESULT){if(!output)return -EINVAL;identity=output;rc=demo_letterbox(output,canvas,region,&fit);}
 else if(layout==DEMO_LAYOUT_INPUT){if(!input)return -EINVAL;identity=input;rc=demo_letterbox(input,canvas,region,&fit);}
 else if(layout==DEMO_LAYOUT_COMPARE){
  if(!input||!output||canvas->width<2)return -EINVAL;
  identity=output;region.width=canvas->width/2;rc=demo_letterbox(input,canvas,region,&fit);
  if(!rc){region.x=region.width;region.width=canvas->width-region.x;rc=demo_letterbox(output,canvas,region,&fit);}
 }else return -EINVAL;
 if(rc)return rc;
 memcpy(canvas->run_id,identity->run_id,sizeof(canvas->run_id));memcpy(canvas->source_result_sha,identity->source_result_sha,sizeof(canvas->source_result_sha));memcpy(canvas->raw_source_sha,identity->raw_source_sha,sizeof(canvas->raw_source_sha));
 memset(canvas->input_source_sha,0,sizeof(canvas->input_source_sha));
 if(input)memcpy(canvas->input_source_sha,input->raw_source_sha,sizeof(canvas->input_source_sha));
 canvas->layout=layout;return demo_frame_hash(canvas);
}

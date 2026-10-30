#ifndef VXL_DEMO_FRAME_H
#define VXL_DEMO_FRAME_H
#include <vxl/demo_hunet.h>
#ifdef __cplusplus
extern "C" {
#endif
enum demo_layout {DEMO_LAYOUT_RESULT=1,DEMO_LAYOUT_INPUT,DEMO_LAYOUT_COMPARE};
struct demo_rect {uint32_t x,y,width,height;};
struct demo_frame {
 uint32_t width,height,stride;size_t bytes;uint8_t *pixels;int owned;
 char run_id[33],source_result_sha[65],sha256[65],raw_source_sha[65],input_source_sha[65];
 enum demo_layout layout;
};
int demo_frame_alloc(uint32_t,uint32_t,uint32_t,struct demo_frame *);
void demo_frame_free(struct demo_frame *);
int demo_frame_check(const struct demo_frame *,int canvas);
int demo_frame_overlap(const struct demo_frame *,const struct demo_frame *);
int demo_frame_hash(struct demo_frame *);
int demo_frame_from_mosaic(const float *,uint32_t,uint32_t,struct demo_frame *);
int demo_frame_from_hunet(const struct demo_hunet_result *,struct demo_frame *);
int demo_letterbox(const struct demo_frame *,struct demo_frame *,struct demo_rect,struct demo_rect *);
int demo_map_bbox(const struct demo_bbox *,struct demo_rect,struct demo_rect *);
int demo_compose(const struct demo_frame *,const struct demo_frame *,enum demo_layout,struct demo_frame *);
int demo_send_display_reference(struct demo_store *,const char *,const struct demo_frame *);
int demo_prepare_display(struct demo_store *,const struct demo_result *,enum demo_layout,struct demo_frame *);
int demo_frame_publish(struct demo_store *,const struct demo_frame *,const struct demo_result *);
int demo_draw_glyph(struct demo_frame *,const struct demo_glyph *,uint32_t,uint32_t,uint32_t);
int demo_draw_ascii(struct demo_frame *,const char *,uint32_t,uint32_t,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif

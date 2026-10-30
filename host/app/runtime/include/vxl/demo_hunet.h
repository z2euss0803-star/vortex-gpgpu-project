#ifndef VXL_DEMO_HUNET_H
#define VXL_DEMO_HUNET_H
#include <vxl/demo_runner.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DEMO_HUNET_CLASSES 8105u
#define DEMO_HUNET_TOPK 5u
struct demo_bbox { int32_t x0,y0,x1,y1;uint32_t image_width,image_height,roi_index; };
struct demo_label {uint32_t class_id,codepoint,glyph_offset;uint16_t width,height,stride,available;char utf8[8];};
struct demo_labels {struct demo_label *items;size_t count;const uint8_t *atlas;size_t atlas_bytes;struct demo_bbox bbox;char classes_sha[65],font_sha[65],glyph_sha[65];};
struct demo_glyph {uint32_t class_id,width,height,stride;const uint8_t *pixels;int available;char fallback[32];};
struct demo_hunet_top {uint32_t class_id;float score;char utf8[8];};
enum demo_numeric_class {DEMO_NUMERIC_UNEVALUATED,DEMO_NUMERIC_PASS,DEMO_NUMERIC_KNOWN_DEVIATION,DEMO_NUMERIC_FAIL};
enum demo_view_scope {DEMO_VIEW_OFFLINE=1,DEMO_VIEW_LIVE,DEMO_VIEW_HISTORICAL};
struct demo_frame;
struct demo_hunet_result {
 char run_id[33],source_result_sha[65],logits_sha[65],classes_sha[65],glyph_sha[65];
 struct demo_bbox bbox;struct demo_hunet_top top[DEMO_HUNET_TOPK];
 struct demo_glyph glyphs[DEMO_HUNET_TOPK];const struct demo_frame *source_frame;
 enum demo_numeric_class numeric_class;
 enum demo_view_scope view_scope;
 int ort_evaluated,ort_pass,r2_evaluated,r2_pass,history_byte_equal,completed;
 uint32_t ort_mismatches,r2_mismatches;
 double ort_max_abs,ort_rmse,r2_max_abs,r2_rmse;
};
int demo_labels_bind(const void *,size_t,const uint8_t *,size_t,const char *,const char *,struct demo_labels *);
void demo_labels_close(struct demo_labels *);
int demo_hunet_decode(const float *,size_t,const struct demo_labels *,struct demo_hunet_result *);
int demo_glyph_lookup(uint32_t,const struct demo_labels *,struct demo_glyph *);
int demo_hunet_classify(const float *,const float *,size_t,int,int,struct demo_hunet_result *);
int demo_hunet_run(struct demo_session *,const struct demo_run *,struct demo_result *);
#ifdef __cplusplus
}
#endif
#endif

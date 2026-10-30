#ifndef DEMO_KMS_H
#define DEMO_KMS_H
#include <vxl/demo_frame.h>
#define DP_XRGB8888 0x34325258u
#define DP_ARGB8888 0x34325241u
enum dp_event_kind {DP_SUBMITTED=1,DP_PRESENTED,DP_RETIRED,DP_FAILED};
struct dp_buffer {uint32_t id,width,height,pitch,format;size_t bytes;uint8_t *map;};
struct dp_config {uint32_t width,height;};
struct dp_caps {uint32_t width,height;int dumb,master,connected,event_monotonic;};
struct dp_copy_stats {uint64_t source_bytes,pixel_bytes,clear_bytes;uint32_t identity;};
struct dp_event {enum dp_event_kind kind;uint64_t cookie,received_ns,kernel_ns;int kernel_clock_known;char run_id[33],frame_sha[65],scanout_sha[65];uint32_t width,height,pitch;uint64_t conversion_ns;struct dp_copy_stats conversion;};
struct dp_ops {
 int (*open)(void *,const struct dp_config *,struct dp_caps *);
 int (*create)(void *,uint32_t,uint32_t,struct dp_buffer *);
 int (*initial)(void *,const struct dp_buffer *);
 int (*flip)(void *,const struct dp_buffer *,uint64_t);
 int (*receive)(void *,int,uint64_t *,uint64_t *);
 int (*disable)(void *); /* success proves every scanout reference retired */
 void (*destroy)(void *,struct dp_buffer *);
 void (*close)(void *);
 void *context;
};
struct dp_context {
 struct dp_ops ops;struct dp_caps caps;struct dp_buffer buffers[2];struct dp_event frames[2],retired;
 uint64_t next_cookie;int opened,active,pending,created,quarantined,retired_event;
};
int dp_open(struct dp_context *,const struct dp_config *,const struct dp_ops *);
/* Pure CPU RGB24 -> opaque BGRA; no flip or KMS/device access. */
int dp_copy_rgb24(const struct demo_frame *,struct dp_buffer *,struct dp_copy_stats *);
int dp_submit(struct dp_context *,const struct demo_frame *);
/* Internal synchronous primitive: caller retains compositor-sealed immutable
 * RGB ownership through return. Metadata/conversion and actual scanout SHA
 * remain checked. External or unsealed inputs must use dp_submit instead. */
int dp_submit_prevalidated_immutable(struct dp_context *,const struct demo_frame *);
int dp_poll(struct dp_context *,int,struct dp_event *);
int dp_close(struct dp_context *);
const char *dp_event_name(enum dp_event_kind);
int dp_present_frame(struct dp_context *,const struct dp_ops *,struct demo_store *,const struct demo_frame *,int (*)(void *,const struct dp_event *),void *);
uint64_t dp_now(void);
/* Allocates only native userspace state. Real device open happens in dp_open. */
int dp_native_ops(struct dp_ops *,const char *device);
void dp_native_free(struct dp_ops *);
#endif

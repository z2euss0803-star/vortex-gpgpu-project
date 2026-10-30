#include <vxl/s8.h>
#include <vxl/engine.h>
#include <vxl/s8_trace.h>
/* A healthy READY row is a frozen previous job, not the next load's timing
 * sink. Loading remains covered by the user envelope until trace_begin. */
static int frozen(const struct vxl_s8_ctx *c){return c->trace&&c->trace->jobs_used&&!c->trace->active&&!c->capture_pending&&c->engine&&c->engine->job.state==VXL_READY;}
vxl_u64 vxl_s8_now_ns(struct vxl_s8_ctx *c) {return c&&c->io.now_ns?c->io.now_ns(c->io.context):0;}
void vxl_s8_timing_reset(struct vxl_s8_ctx *c) {
 if(!c)return;
 memset(&c->times,0,sizeof(c->times));
#ifdef __KERNEL__
 c->times.clock=c->io.now_ns?1:0; /* ktime_get_ns */
#else
 c->times.clock=c->io.now_ns?3:0; /* software test callback; never real kernel proof */
#endif
}
int vxl_s8_timing_event(struct vxl_s8_ctx *c,vxl_u32 event,vxl_u64 ns) {
 vxl_u64 bit;if(!c || event>=VXL_S8_TIME_EVENTS)return -EINVAL;
 if(frozen(c))return 0;
 bit=1ULL<<event;if(!(c->times.valid_events&bit))c->times.first[event]=ns;
 c->times.valid_events|=bit;c->times.last[event]=ns;
 if(c->times.event_calls[event]==~(vxl_u64)0){c->times.invalid=1;return -EOVERFLOW;}
 c->times.event_calls[event]++;return 0;
}
int vxl_s8_timing_add(struct vxl_s8_ctx *c,vxl_u32 id,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes) {
 struct vxl_s8_metric *m;if(!c || id>=VXL_S8_METRICS)return -EINVAL;
 if(frozen(c))return 0;
 m=&c->times.metrics[id];if(!m->valid)m->first_ns=begin;m->last_begin_ns=begin;m->last_ns=end;m->valid=1;
 if(end<begin || end-begin>~(vxl_u64)0-m->ns || bytes>~(vxl_u64)0-m->bytes || m->calls==~(vxl_u64)0){m->overflow=1;c->times.invalid=1;if(!c->error_class)c->error_class=VXL_S8_TIMING_INVALID;if(c->observe)c->observe(c,id,begin,end,bytes,-EOVERFLOW);return -EOVERFLOW;}
 m->ns+=end-begin;m->bytes+=bytes;m->calls++;
 if(c->observe && id!=VXL_S8_CDMA && id!=VXL_S8_GUARD_CDMA && id!=VXL_S8_CDMA_SLEEP && id!=VXL_S8_GUARD_CDMA_SLEEP)return c->observe(c,id,begin,end,bytes,0);
 return 0;
}

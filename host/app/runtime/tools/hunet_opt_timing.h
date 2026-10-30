#ifndef HUNET_OPT_TIMING_H
#define HUNET_OPT_TIMING_H
#include <vxl/core.h>
#include <time.h>
#define VXL_LOAD_INCOMPLETE 1u
#define VXL_LOAD_BACKWARDS 2u
#define VXL_LOAD_OVERFLOW 4u
#define VXL_LOAD_FULL 8u
#define VXL_LOAD_OVERLAP 16u
#define VXL_LOAD_CLOCK_ERROR 32u
enum vxl_opt_load_field {VXL_LOAD_READ,VXL_LOAD_HASH,VXL_LOAD_RESERVE,VXL_LOAD_CODE_COPY,VXL_LOAD_WEIGHTS_COPY,VXL_LOAD_SEAL,VXL_LOAD_VALIDATE};
enum vxl_opt_load_asset {VXL_LOAD_BINDING,VXL_LOAD_SELF,VXL_LOAD_CPU,VXL_LOAD_CPU_CONTRACT,VXL_LOAD_CPU_STAGES,VXL_LOAD_JOBS,VXL_LOAD_MODEL,VXL_LOAD_AUX,VXL_LOAD_FUSION,VXL_LOAD_DESCRIPTORS,VXL_LOAD_LAYOUT,VXL_LOAD_CODE,VXL_LOAD_WEIGHTS,VXL_LOAD_ADDRESS,VXL_LOAD_TRANSACTION};
struct vxl_opt_load_row {vxl_u32 asset_id,field;vxl_u64 ns,bytes,calls;int result;};
struct vxl_opt_load_timing {vxl_u32 used,flags,asset_id;vxl_u64 start_ns,end_ns,last_end_ns,read_interrupts;int result;struct vxl_opt_load_row rows[64];};
static inline vxl_u64 vxl_opt_load_now(struct vxl_opt_load_timing *t){
 struct timespec n;int saved_errno=errno,rc;if(!t)return 0;
 rc=clock_gettime(CLOCK_MONOTONIC,&n);errno=saved_errno;
 if(rc){t->flags|=VXL_LOAD_CLOCK_ERROR;return 0;}
 return (vxl_u64)n.tv_sec*1000000000ULL+(vxl_u64)n.tv_nsec;
}
static inline void vxl_opt_load_begin(struct vxl_opt_load_timing *t){if(t){memset(t,0,sizeof(*t));t->flags=VXL_LOAD_INCOMPLETE;t->start_ns=t->last_end_ns=vxl_opt_load_now(t);}}
static inline void vxl_opt_load_asset(struct vxl_opt_load_timing *t,enum vxl_opt_load_asset a){if(t)t->asset_id=(vxl_u32)a;}
static inline void vxl_opt_load_mark(struct vxl_opt_load_timing *t,enum vxl_opt_load_field f,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes,int rc){
 struct vxl_opt_load_row *r;vxl_u64 delta;vxl_u32 i;
 if(!t)return;
 if(end<begin||begin<t->start_ns){t->flags|=VXL_LOAD_BACKWARDS;return;}
 if(begin<t->last_end_ns){t->flags|=VXL_LOAD_OVERLAP;return;}
 delta=end-begin;
 for(i=0;i<t->used;i++)if(t->rows[i].asset_id==t->asset_id&&t->rows[i].field==(vxl_u32)f)break;
 if(i==t->used){if(t->used>=64){t->flags|=VXL_LOAD_FULL;return;}t->used++;r=&t->rows[i];memset(r,0,sizeof(*r));r->asset_id=t->asset_id;r->field=(vxl_u32)f;}
 r=&t->rows[i];
 if(r->ns>~(vxl_u64)0-delta||r->bytes>~(vxl_u64)0-bytes||r->calls==~(vxl_u64)0){t->flags|=VXL_LOAD_OVERFLOW;return;}
 r->ns+=delta;r->bytes+=bytes;r->calls++;if(rc&&!r->result)r->result=rc;t->last_end_ns=end;
}
static inline void vxl_opt_load_finish(struct vxl_opt_load_timing *t,int rc){
 if(!t)return;
 t->end_ns=vxl_opt_load_now(t);t->result=rc;
 if(t->end_ns<t->last_end_ns)t->flags|=VXL_LOAD_BACKWARDS;
 if(!rc&&t->flags==VXL_LOAD_INCOMPLETE)t->flags=0;
}
#endif

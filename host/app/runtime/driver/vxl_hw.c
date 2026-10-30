#include "vxl_internal.h"
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <asm/processor.h>
#include <vxl/s8.h>
#define BOUNCE_OFFSET (VXL_PS_SIZE-4096)
/* Per-transfer stack state; all callers retain the existing device mutex. */
struct cdma_wait {struct vxl_device *device;u64 started_ns;u32 remaining;bool fast;};
static u32 cdma_read(void *ctx,u32 offset){return readl(((struct cdma_wait *)ctx)->device->cdma+offset);}
static void cdma_write(void *ctx,u32 offset,u32 value){struct cdma_wait *w=ctx;if(offset==0x28)dma_wmb();writel(value,w->device->cdma+offset);if(offset==0x28&&w->fast)w->started_ns=ktime_get_ns();}
static u64 cdma_now(void *ctx){(void)ctx;return ktime_get_ns()/1000000ULL;}
static void cdma_pause(void *ctx){
 struct cdma_wait *w=ctx;struct vxl_submit_time *t=w->device->submit_trace.active;u64 begin=ktime_get_ns();
 struct vxl_mgmt_row *m=w->device->hunet.mgmt_row;
 if(w->fast&&w->remaining&&begin>=w->started_ns&&begin-w->started_ns<8000){
  w->remaining--;cpu_relax();
  if(t){vxl_time_add(t,&t->cdma_fast_poll_ns,begin,ktime_get_ns());t->cdma_fast_poll_calls++;}
  if(m){u64 end=ktime_get_ns();vxl_mgmt_nested_add(m,VXL_MGMT_CDMA_FASTPOLL,begin,end,0);vxl_mgmt_nested_add(m,VXL_MGMT_CDMA_WAIT,begin,end,0);}
  return;
 }
 w->fast=false;usleep_range(50,100);
 if(t){u64 end=ktime_get_ns();vxl_time_add(t,&t->cdma_sleep_ns,begin,end);if(t->guard_depth)vxl_time_add(t,&t->guard_cdma_sleep_ns,begin,end);t->cdma_pause_calls++;}
 if(m){u64 end=ktime_get_ns();vxl_mgmt_nested_add(m,VXL_MGMT_CDMA_SLEEP,begin,end,0);vxl_mgmt_nested_add(m,VXL_MGMT_CDMA_WAIT,begin,end,0);}
 if(w->device->engine.s8){struct vxl_s8_ctx *c=w->device->engine.s8;u64 end=ktime_get_ns();vxl_s8_timing_add(c,VXL_S8_CDMA_SLEEP,begin,end,0);if(c->times.guard_depth)vxl_s8_timing_add(c,VXL_S8_GUARD_CDMA_SLEEP,begin,end,0);}
}
static int dma_block(struct vxl_device *d,u64 src,u64 dst,u32 n){
 struct vxl_submit_time *t=d->submit_trace.active;u64 begin=ktime_get_ns();
 struct cdma_wait wait={d,0,64,d->engine.profile_id==4&&
  (((d->hunet.program_id==9||(d->hunet.program_id==10&&d->hunet.opt.release))&&(d->cdma_fastwait_active||d->engine.job.state==VXL_RUNNING))||
   (d->hunet.opt.release&&d->hunet.opt_load_locked&&d->hunet.opt_load_owner==d->engine.owner&&d->hunet.opt_load_generation==d->engine.generation&&d->cdma_fastwait_active))};
 struct vxl_cdma_io io={&wait,cdma_read,cdma_write,cdma_now,cdma_pause,0};int rc=vxl_cdma_run(&io,src,dst,n,1000);
 if(t){u64 end=ktime_get_ns();vxl_time_add(t,&t->cdma_ns,begin,end);t->cdma_calls++;t->cdma_bytes+=n;if(t->guard_depth){vxl_time_add(t,&t->guard_cdma_ns,begin,end);t->guard_cdma_calls++;t->guard_cdma_bytes+=n;}}
 if(d->hunet.mgmt_row)vxl_mgmt_nested_add(d->hunet.mgmt_row,VXL_MGMT_CDMA_TOTAL,begin,ktime_get_ns(),n);
 if(d->engine.s8){struct vxl_s8_ctx *c=d->engine.s8;u64 end=ktime_get_ns();vxl_s8_timing_add(c,VXL_S8_CDMA,begin,end,n);if(c->times.guard_depth)vxl_s8_timing_add(c,VXL_S8_GUARD_CDMA,begin,end,n);}
 d->cdma_last=io.last_status;if(!rc)dma_rmb();return rc;
}
static int alloc_mem(void *ctx,struct vxl_object *o) {
 struct vxl_device *d=ctx;
 if(!d->approved)return -EOPNOTSUPP;
 if(o->domain==VXL_PS){
  if(o->offset+o->allocated>BOUNCE_OFFSET)return -ENOMEM;
  o->storage=(u8 *)d->ps+o->offset;memset(o->storage,0,o->allocated);
 } else o->storage=NULL;
 return 0;
}
static void free_mem(void *ctx,struct vxl_object *o){(void)ctx;(void)o; /* PS reservation and PL are device-owned for whole lifetime. */}
static int transfer(void *ctx,struct vxl_object *o,u64 off,void *buf,u64 n,int write) {
 struct vxl_device *d=ctx;void *bounce=(u8 *)d->ps+BOUNCE_OFFSET;u64 bounce_dma=d->ps_dma+BOUNCE_OFFSET;int rc;
 if(vxl_range_check(o->allocated,off,n)||vxl_cdma_addresses(o->bus+off,bounce_dma,n))return -EINVAL;
 if(o->domain==VXL_PS){if(write)memcpy((u8 *)o->storage+off,buf,n);else memcpy(buf,(u8 *)o->storage+off,n);dma_wmb();return 0;}
 if(write){memcpy(bounce,buf,n);dma_wmb();rc=dma_block(d,bounce_dma,o->bus+off,n);}
 else {rc=dma_block(d,o->bus+off,bounce_dma,n);if(!rc){dma_rmb();memcpy(buf,bounce,n);}}
 return rc;
}
int vxl_private_rw(void *ctx,u64 gpu,void *buf,u64 n,int write){
 struct vxl_device *d=ctx;struct vxl_object o={0};
 if(!n||n>4096||(gpu&15)||(n&15))return -EINVAL;
 if(gpu>=VXL_PS_BASE&&!vxl_range_check(VXL_PS_SIZE,gpu-VXL_PS_BASE,n)){o.domain=VXL_PS;o.gpu=gpu;o.bus=gpu;o.allocated=n;o.storage=(u8 *)d->ps+gpu-VXL_PS_BASE;}
 else if(gpu>=0x80000000ULL&&!vxl_range_check(VXL_PL_SIZE,gpu-0x80000000ULL,n)){o.domain=VXL_PL;o.gpu=gpu;o.bus=VXL_PL_BASE+gpu-0x80000000ULL;o.allocated=n;}
 else return -ERANGE;
 return transfer(ctx,&o,0,buf,n,write);
}
int vxl_hybrid_hw_idle(struct vxl_hunet_ctx *c){
 struct vxl_device *d=c->io.context;u32 status=readl(d->gpu+0xc),cdma=readl(d->cdma+4);
 c->hybrid.evidence.cdma_status=cdma;
 if(gpiod_get_value_cansleep(d->ready)!=1||!(readl(d->gpu+0x34)&1)||((status>>8)&7)||(status&4)||readl(d->gpu+0x1c)||readl(d->gpu+0x20)||readl(d->gpu+0x24)||readl(d->gpu+0x2c)!=c->engine->snapshot.jobs||!(cdma&2)||(cdma&0x4770))return -EIO;
 return 0;
}
static int prepare_gpu(void *ctx,struct vxl_engine *e,struct vxl_object *code,u64 co,
 struct vxl_object *args,u64 ao,u32 id,struct vxl_prepared *prepared) {
 struct vxl_device *d=ctx;struct vxl_submit_time *t=d->submit_trace.active;u64 begin=ktime_get_ns();u32 status;int rc;
 if(gpiod_get_value_cansleep(d->ready)!=1||!(readl(d->gpu+0x34)&1))return -EAGAIN;
 if(readl(d->gpu)!=0x56580204||readl(d->gpu+4)!=0x40404)return -ENODEV;
 status=readl(d->gpu+0xc);
 if(((status>>8)&7)!=0||readl(d->gpu+0x1c)||readl(d->gpu+0x20)||readl(d->gpu+0x24))return -EBUSY;
 if(e->s8){u64 preflight_end=ktime_get_ns();rc=id==11?vxl_s8_prepare(e,prepared):-EACCES;/* trace_begin resets the new job's counters inside prepare; record the saved boundaries after it. */vxl_s8_timing_event(e->s8,VXL_S8_PREP_BEGIN,begin);vxl_s8_timing_add(e->s8,VXL_S8_CONTROL_PREFLIGHT,begin,preflight_end,0);vxl_s8_timing_event(e->s8,VXL_S8_PREP_END,ktime_get_ns());vxl_s8_timing_add(e->s8,VXL_S8_PREPARE,begin,ktime_get_ns(),0);}
 else rc=e->hunet?vxl_hunet_prepare(e->hunet,prepared,id):vxl_controlled_prepare(e,code,co,args,ao,id,prepared);
 if(t){vxl_time_add(t,&t->prepare_ns,begin,ktime_get_ns());if(!rc&&e->hunet){if(id==10){t->sequence=e->hunet->opt.pending.first;t->op=e->hunet->opt.pending.op;t->stage=e->hunet->opt.pending.first_stage;}else if(id==9){t->sequence=e->hunet->fused.pending.first;t->op=e->hunet->fused.pending.op;t->stage=e->hunet->fused.pending.stage;}else{t->sequence=prepared->args_words[4];t->op=prepared->args_words[3];t->stage=prepared->args_words[30];}}}
 return rc;
}
static int start_gpu(void *ctx,const struct vxl_prepared *prepared,u32 *jobs) {
 struct vxl_device *d=ctx;int rc;
 if(d->submit_trace.active)d->submit_trace.active->job_id=d->engine.job.id;
 *jobs=readl(d->gpu+0x2c);
 if(d->engine.s8){
  struct vxl_s8_ctx *c=d->engine.s8;
  if(c->error_class||c->times.invalid)return -EIO;
  if(!c->program || !c->io.now_ns || prepared->program_id!=11 || prepared->code_gpu!=0x80000000u || prepared->args_gpu!=0x80100040u)return -EACCES;
  memset(&c->hw,0,sizeof(c->hw));c->hw.job_id=d->engine.job.id;c->hw.generation=d->engine.generation;c->hw.sequence=c->job_args.sequence;c->hw.jobs_before=*jobs;
  writel(4000000000u,d->gpu+0x18);c->hw.watchdog=readl(d->gpu+0x18);if(c->hw.watchdog!=4000000000u){c->error_class=VXL_S8_TIMING_INVALID;return -EIO;}
 }
 if(d->engine.hunet){rc=vxl_hunet_before_start(d->engine.hunet,d->engine.job.id,*jobs);if(rc)return rc;writel(4000000000u,d->gpu+0x18);}
 writel(prepared->code_gpu,d->gpu+0x10);writel(prepared->args_gpu,d->gpu+0x14);
 writel(0,d->gpu+0x50); /* disable optional TEX descriptor */
 if(d->engine.s8&&(d->engine.s8->job_args.case_id==4||d->engine.s8->job_args.case_id==5)){
  struct vxl_s8_job_v1 *j=&d->engine.s8->job_args;u32 core=0;while(core<4&&!(j->core_mask&(1u<<core)))core++;
  if(core==4)return -EINVAL;
  writel(0x81000040u+(core*4+j->owner_warp)*0x20000u,d->gpu+0x40);
  writel(2,d->gpu+0x44);writel(2,d->gpu+0x48);writel(8,d->gpu+0x4c);writel(16,d->gpu+0x54);writel(j->case_id==4?9:8,d->gpu+0x50);
 }
 dma_wmb();writel(1,d->gpu+8);if(d->engine.s8)vxl_s8_timing_event(d->engine.s8,VXL_S8_GPU_START,ktime_get_ns());if(d->submit_trace.active)d->submit_trace.active->gpu_start_ns=ktime_get_ns();return 0;
}
int vxl_s8_hw_snapshot(struct vxl_device *d,struct vxl_event *e) {
 struct vxl_s8_ctx *c=d->engine.s8;struct vxl_s8_hw_snapshot *s;
 if(!c)return -EINVAL;
 s=&c->hw;
 e->status=readl(d->gpu+0xc);e->errors=readl(d->gpu+0x1c);e->pending_w=readl(d->gpu+0x20);e->pending_r=readl(d->gpu+0x24);e->jobs=readl(d->gpu+0x2c);
 e->ready=gpiod_get_value_cansleep(d->ready)==1&&(readl(d->gpu+0x34)&1);e->idle=((e->status>>8)&7)==0;
 s->event=*e;s->watchdog=readl(d->gpu+0x18);s->elapsed_raw=readl(d->gpu+0x28);s->elapsed_address=0xa0000028ULL;
 s->elapsed_valid=!!(e->status&2);s->pre_ack=1;s->cdma_status=readl(d->cdma+4);s->tex_pending=readl(d->gpu+0x58)&1;
 return 0;
}
static int s8_finish_gpu(struct vxl_device *d,struct vxl_event *e) {
 struct vxl_s8_ctx *c=d->engine.s8;struct vxl_s8_hw_snapshot *s=&c->hw;int rc;u32 status;u64 begin;
 rc=vxl_s8_hw_snapshot(d,e);if(rc)return rc;
 if(e->errors||(e->status&4)){c->error_class=VXL_S8_RUNTIME_FAULT;return -EIO;}
 if(!(e->status&2))return 0;
 vxl_s8_timing_event(c,VXL_S8_FIRST_DONE,ktime_get_ns());
 if(e->pending_w||e->pending_r||s->tex_pending||e->jobs!=(u32)(d->engine.job.jobs_before+1))return 0;
 if(!s->elapsed_raw||s->elapsed_raw>=4000000000u||s->watchdog!=4000000000u){c->error_class=VXL_S8_TIMING_INVALID;return -ENODATA;}
 dma_rmb();
 for(u32 core=0;core<4;++core){rc=vxl_s8_io_bytes(c,0x10002008ULL+core*256,&s->exitcode[core],4,0);if(rc)return rc;s->exit_valid|=1u<<core;if(s->exitcode[core]){c->error_class=VXL_S8_RUNTIME_FAULT;return -EIO;}}
 rc=vxl_s8_io_bytes(c,0x10002400,s->crt_guards,128,0);if(rc)return rc;
 rc=vxl_s8_io_bytes(c,0x10002fc0,s->crt_guards+128,64,0);if(rc)return rc;
 rc=vxl_s8_results_check(c);if(rc)return rc;
 rc=vxl_s8_guards(c,&c->job_args,0);if(rc)return rc;s->guard_valid=1;
 rc=vxl_s8_immutable_check(c);if(rc)return rc;s->immutable_valid=1;
 rc=vxl_s8_inputs_check(c,&c->job_args);if(rc)return rc;s->input_valid=1;
 s->cdma_status=readl(d->cdma+4);if(vxl_cdma_status(s->cdma_status)!=1)return -EIO;
 if(c->error_class||c->times.invalid)return -EIO;
 begin=ktime_get_ns();vxl_s8_timing_event(c,VXL_S8_ACK_BEGIN,begin);
 writel(2,d->gpu+8);status=readl(d->gpu+0xc);e->idle=((status>>8)&7)==0;e->ready=gpiod_get_value_cansleep(d->ready)==1&&(readl(d->gpu+0x34)&1);
 s->ack_idle=e->idle;s->ack_ready=e->ready;
 vxl_s8_timing_event(c,VXL_S8_ACK_END,ktime_get_ns());vxl_s8_timing_add(c,VXL_S8_ACK_TIME,begin,ktime_get_ns(),0);
 if(!e->idle||!e->ready)return -EIO;
 s->ack_valid=1;return 1;
}
static int poll_gpu(void *ctx,struct vxl_event *e) {
 struct vxl_device *d=ctx;struct vxl_submit_time *t=d->submit_trace.active;u64 begin=ktime_get_ns();u32 status;int rc;struct vxl_hunet_hw_snapshot s={0};
 if(d->engine.s8){rc=s8_finish_gpu(d,e);if(rc<0&&!d->engine.s8->error_class)d->engine.s8->error_class=VXL_S8_RUNTIME_FAULT;vxl_s8_timing_add(d->engine.s8,VXL_S8_STATE_POLL,begin,ktime_get_ns(),0);return rc;}
 e->status=readl(d->gpu+0xc);e->errors=readl(d->gpu+0x1c);e->pending_w=readl(d->gpu+0x20);e->pending_r=readl(d->gpu+0x24);e->jobs=readl(d->gpu+0x2c);
 e->ready=gpiod_get_value_cansleep(d->ready)==1&&(readl(d->gpu+0x34)&1);
 if(d->engine.hunet){s.event=*e;s.watchdog=readl(d->gpu+0x18);s.elapsed_raw=readl(d->gpu+0x28);s.cdma_status=d->cdma_last;rc=vxl_hunet_exit_snapshot(&d->hunet,&s);vxl_hunet_record_snapshot(&d->hunet,&s,ktime_get_ns());if(rc){if(t){vxl_time_add(t,&t->state_poll_ns,begin,ktime_get_ns());t->state_poll_calls++;}return rc;}}
 if(t){vxl_time_add(t,&t->state_poll_ns,begin,ktime_get_ns());t->state_poll_calls++;}
 if(e->errors||(e->status&4))return -EIO;
 if(!(e->status&2)||e->pending_w||e->pending_r||e->jobs!=(u32)(d->engine.job.jobs_before+1))return 0;
 if(t){t->gpu_done_observed_ns=ktime_get_ns();t->elapsed_cycles=s.elapsed_raw;}
 /* DONE is hardware drain/FENCE evidence. ACK is allowed only after saving snapshot. */
 dma_rmb();
 if(d->engine.hunet){rc=vxl_hunet_crt_snapshot(&d->hunet,&s);vxl_hunet_record_snapshot(&d->hunet,&s,ktime_get_ns());if(rc)return rc;rc=vxl_hunet_accept_done(&d->hunet,&s,ktime_get_ns(),&d->hunet.evidence);if(rc)return rc;}
 begin=ktime_get_ns();writel(2,d->gpu+8);status=readl(d->gpu+0xc);e->idle=((status>>8)&7)==0;
 if(d->engine.hunet){e->ready=gpiod_get_value_cansleep(d->ready)==1&&(readl(d->gpu+0x34)&1);rc=vxl_hunet_after_ack(&d->hunet,e->ready,e->idle,ktime_get_ns());if(rc){if(t)vxl_time_add(t,&t->ack_ns,begin,ktime_get_ns());return rc;}}
 if(t){t->ack_end_ns=ktime_get_ns();vxl_time_add(t,&t->ack_ns,begin,t->ack_end_ns);}
 return 1;
}
static int from_user(void *ctx,void *dst,u64 src,u64 n){struct vxl_device *d=ctx;u64 begin=ktime_get_ns();int rc=copy_from_user(dst,u64_to_user_ptr(src),n)?-EFAULT:0;if(d->engine.s8)vxl_s8_timing_add(d->engine.s8,VXL_S8_USER_COPY,begin,ktime_get_ns(),n);return rc;}
static int to_user(void *ctx,u64 dst,const void *src,u64 n){struct vxl_device *d=ctx;u64 begin=ktime_get_ns();int rc=copy_to_user(u64_to_user_ptr(dst),src,n)?-EFAULT:0;if(d->engine.s8)vxl_s8_timing_add(d->engine.s8,VXL_S8_USER_COPY,begin,ktime_get_ns(),n);return rc;}
static u64 now(void *ctx){(void)ctx;return ktime_get_ns()/1000000ULL;}
const struct vxl_ops vxl_hw_ops={alloc_mem,free_mem,transfer,prepare_gpu,start_gpu,poll_gpu,from_user,to_user,now};

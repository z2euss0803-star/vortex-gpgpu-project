#include <vxl/core.h>
int vxl_job_step(struct vxl_job *j,const struct vxl_event *e) {
 if(!j||!e)return -EINVAL;
 if(e->id!=j->id)return -ESTALE;
 if(e->kind==VXL_START) {
  if(j->state==VXL_QUARANTINED)return -EIO;
  if(j->state!=VXL_READY)return -EBUSY;
  j->pinned=1;j->state=VXL_RUNNING;return 0;
 }
 if(e->kind==VXL_CLOSE) {j->closed=1;return 0;}
 if(e->kind==VXL_WAIT_TIMEOUT) return j->state==VXL_RUNNING?-ETIMEDOUT:0;
 if(e->kind==VXL_QUIESCED) return -EOPNOTSUPP; /* H0 has no accepted recovery. */
 if(j->state!=VXL_RUNNING)return -EALREADY;
 if(e->kind==VXL_DEADLINE||e->kind==VXL_ERROR) {
  j->state=VXL_QUARANTINED;j->result=e->kind==VXL_DEADLINE?-ETIMEDOUT:-EIO;return 0;
 }
 if(e->kind==VXL_DONE) {
  if(e->errors||(e->status&4)) {j->state=VXL_QUARANTINED;j->result=-EIO;return -EIO;}
  if(!(e->status&2)||e->pending_w||e->pending_r||e->jobs!=(vxl_u32)(j->jobs_before+1)||!e->idle||!e->ready)return -EAGAIN;
  j->state=VXL_COMPLETED;j->pinned=0;j->result=0;return 0;
 }
 return -EINVAL;
}

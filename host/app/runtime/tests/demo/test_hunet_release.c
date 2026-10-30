#include <vxl/profile_switch.h>
#include <vxl/hunet.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
static int not_idle;
static int idle(void *p,struct vxl_engine *e){(void)p;(void)e;return not_idle?-EIO:0;}
int main(void){struct vxl_engine *e=calloc(1,sizeof(*e));struct vxl_hunet_ctx *c=calloc(1,sizeof(*c));struct vxl_fused_release release={0};assert(e&&c);e->profile_id=4;e->generation=9;e->owner=77;e->opened=1;e->job.state=VXL_READY;e->hunet=c;c->engine=e;c->program_id=9;c->sealed=1;c->seal_owner=77;c->seal_generation=9;c->evidence_delivered=1;c->evidence.job_id=1001;c->hybrid.enabled=1;c->hybrid.owner=77;c->hybrid.generation=9;c->hybrid.phase=VXL_HYBRID_GPU_READY;c->fused.release=&release;
 struct vxl_profile_factory factory={.enabled=1,.idle=idle};e->profile_factory=&factory;
 assert(vxl_demo_hunet_release(e,77)==-EBUSY&&c->sealed);
 struct vxl_hybrid_end_v1 *r=&c->hybrid.end_receipt;*r=(struct vxl_hybrid_end_v1){.magic=VXL_HYBRID_END_MAGIC,.version=1,.bytes=256,.generation=9,.logical_jobs=1023,.gpu_jobs=103,.cpu_jobs=13,.stages=100,.final_idle=1,.check_flags=31,.last_gpu_job_id=1001,.hw_jobs_before=3,.hw_jobs_after=106};memset(c->run_id,1,16);memcpy(r->run_id,c->run_id,16);c->hybrid.logical_jobs=1023;c->hybrid.gpu_jobs=103;c->hybrid.cpu_jobs=13;c->next_sequence=1024;e->snapshot.jobs=106;
 c->hybrid.end_pending=1;assert(vxl_demo_hunet_release(e,77)==-EBUSY&&c->sealed);c->hybrid.end_pending=0;factory.enabled=0;assert(vxl_demo_hunet_release(e,77)==-ENOTTY&&c->sealed);factory.enabled=1;not_idle=1;assert(vxl_demo_hunet_release(e,77)==-EIO&&c->sealed);not_idle=0;assert(vxl_demo_hunet_release(e,88)==-EBADF&&c->sealed);
 c->hybrid.cpu_input=malloc(32);c->hybrid.cpu_output=malloc(32);assert(!vxl_demo_hunet_release(e,77));assert(!c->sealed&&!c->active&&!c->hybrid.cpu_input&&!c->hybrid.cpu_output&&!c->hybrid.owner&&c->program_id==9&&e->owner==77&&e->opened&&e->generation==9);free(c);free(e);puts("DEMO_PROGRAM9_FULL_END_DELIVERY_REQUIRED_BEFORE_UNSEAL_NO_FD_CLOSE_HOST_PASS");return 0;}

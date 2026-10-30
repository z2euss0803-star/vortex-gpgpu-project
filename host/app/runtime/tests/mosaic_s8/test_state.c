#include <vxl/mosaic_host.h>
#include <assert.h>
#include <stdio.h>
#include "mosaic_fixture.h"
#include "mosaic_contract_fixture.h"
#include "program_fixture.h"
#include "../full-program-fixture/program_fixture.h"
static struct vxl_engine e;
static struct mosaic_ctx c;
static struct mosaic_host_state h;
static struct mosaic_mutable mut[2]={{128,64},{256,1792}};
static struct mosaic_catalog_layout catalog;
static struct mosaic_program program;
static const vxl_u8 run1[16]={1},run2[16]={2};
static int state_rw(void *p,vxl_u64 a,void *b,vxl_u64 n,int w){(void)p;(void)a;(void)b;(void)n;(void)w;return -ENOSYS;}
static void setup_size(int full){
 memset(&e,0,sizeof(e));e.profile_id=6;
 assert(!mosaic_host_init(&c,&h,&e,full?full_program_binding_sha:program_binding_sha));
 memset(&catalog,0,sizeof(catalog));catalog.layout=full?fixture_layout:fixture_small_layout;
 memcpy(catalog.graph_sha,full?full_program_graph:program_graph,32);memcpy(catalog.layout_sha,full?full_program_layout:program_layout,32);
 program=(struct mosaic_program){.flat=program_flat,.flat_bytes=256,.runtime_bytes=2048,.entry=0x80000000,.mutable_ranges=mut,.mutable_count=2,.layouts=&catalog,.layout_count=1,.assets=&fixture_manifest,.assets_bytes=3382784,.crt_base=0x10002000,.crt_bytes=4096,.tls_base=0x80000200,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.stack_stride=8192,.static_stack_bytes=1024,.stack_base=0xfff00000,.stack_bytes=0x100000};
 memcpy(&program.binding,full?full_program_binding:program_binding,512);memcpy(program.binding_sha,full?full_program_binding_sha:program_binding_sha,32);memcpy(program.graph_policy_sha,full?full_program_policy:program_policy,32);memcpy(program.runtime_deps_sha,full?full_program_deps:program_deps,32);
 assert(!mosaic_program_register(&c,&program));
 /* TEST_ONLY trusted load result. Driver load/engine routing is not tested. */
 h.loaded=h.nonce_locked=1;e.opened=1;e.owner=77;e.generation=9;e.job.state=VXL_READY;
 h.io.rw=state_rw;h.handles[0]=123;
}
static void setup(void){setup_size(0);}
static struct mosaic_job_v1 begin(void){
 struct mosaic_job_v1 j;assert(!mosaic_frame_begin(&c,77,run1,0,1,0));
 assert(!mosaic_next_job(&c,1,&j));assert(j.sequence==1&&j.tile_first==0&&j.flags==0);
 assert(!mosaic_submit_begin(&c,77,100,31,&j,10));assert(h.io.rw==state_rw&&h.handles[0]==123);return j;
}
static struct mosaic_completion proof(void){
 struct mosaic_completion p={.before_ack={.kind=VXL_DONE,.status=2|(5u<<8),.jobs=32,.idle=0,.ready=1,.id=100},.generation=9,.sequence=h.sequence,.checks=MOSAIC_REQUIRED_CHECKS,.exit_valid=15,.elapsed_raw=123,.watchdog=4000000000u,.cdma_status=0x1002,.ack_idle=1,.ack_ready=1,.elapsed_address=0xa0000028ULL,.submit_enter=10,.prepare_enter=11,.prepare_done=12,.start=13,.done_seen=14,.ack_done=15,.complete=16};return p;
}
static void deliver(void){
 assert(mosaic_reap(&c)==-EBUSY);
 assert(mosaic_evidence_delivered(&c,76,e.job.id,0,512u,1)==-EPERM);
 assert(mosaic_evidence_delivered(&c,77,e.job.id+1,0,512u,1)==-ESTALE);
 assert(mosaic_evidence_delivered(&c,77,e.job.id,1,1,1)==-EINVAL);
 assert(!mosaic_evidence_delivered(&c,77,e.job.id,0,17,1));assert(mosaic_reap(&c)==-EBUSY);
 assert(!mosaic_evidence_delivered(&c,77,e.job.id,17,512u-17,1));assert(!mosaic_reap(&c));
}
int main(void){
 for(unsigned size=0;size<2;++size)for(unsigned phase=0;phase<77;++phase){
  unsigned l,o,n;assert(!mosaic_phase(phase,size?1280:12,size?960:8,&l,&o,&n));
  const unsigned *want=size?fixture_full_phase_spec[phase]:fixture_small_phase_spec[phase];assert(l==want[0]&&o==want[1]&&n==want[2]);
 }
 {unsigned l,o,n;assert(mosaic_phase(77,12,8,&l,&o,&n)==-ERANGE);assert(mosaic_phase(0,13,8,&l,&o,&n)==-EINVAL);}
 setup();assert(mosaic_frame_begin(&c,76,run1,0,1,0)==-EPERM);assert(mosaic_frame_begin(&c,77,run1,1,1,0)==-EINVAL);
 struct mosaic_job_v1 j=begin();assert(e.job.pinned&&h.job_active);assert(!h.first&&!h.phase);
 assert(mosaic_submit_begin(&c,77,101,31,&j,10)==-EBUSY);struct mosaic_completion p=proof();assert(!mosaic_state_complete(&c,&p));
 assert(e.job.state==VXL_COMPLETED&&!e.job.pinned&&h.evidence.elapsed_valid&&h.evidence.observed.elapsed_raw==123);assert(!h.first&&!h.phase);deliver();assert(h.first==1&&h.phase==0&&h.sequence==2);
 assert(mosaic_frame_end(&c,77)==-EBUSY);
 /* Complete all remaining bounded ranges. One phase may take many jobs. */
 unsigned submitted=1;
 while(h.phase<77){
  unsigned l,o,n;assert(!mosaic_phase(h.phase,12,8,&l,&o,&n));unsigned count=n-h.first;if(count>3)count=3;
  vxl_u64 entered=20+submitted;
  assert(!mosaic_next_job(&c,count,&j));assert(!mosaic_submit_begin(&c,77,100+submitted,31+submitted,&j,entered));p=proof();p.before_ack.id=100+submitted;p.before_ack.jobs=32+submitted;
  p.submit_enter=entered;p.prepare_enter=entered+1;p.prepare_done=entered+2;p.start=entered+3;p.done_seen=entered+4;p.ack_done=entered+5;p.complete=entered+6;
  assert(h.evidence.observed.submit_enter==entered);assert(!mosaic_state_complete(&c,&p));assert(h.evidence.observed.submit_enter==entered);deliver();++submitted;
 }
 assert(submitted>77);assert(mosaic_frame_end(&c,77)==-EBUSY);
 assert(mosaic_result_delivered(&c,77,1,4,1)==-EINVAL);assert(!mosaic_result_delivered(&c,77,0,12*8*3*4,1));assert(!mosaic_frame_end(&c,77));assert(h.loaded&&h.program==&program);
 assert(mosaic_frame_begin(&c,77,run1,0,2,0)==-ESTALE);assert(mosaic_frame_begin(&c,77,run2,0,1,0)==-ESTALE);assert(!mosaic_frame_begin(&c,77,run2,0,2,0));assert(h.epoch==2);
 for(unsigned fault=0;fault<17;++fault){
  setup();begin();p=proof();
  switch(fault){case 0:p.before_ack.pending_w=1;break;case 1:p.before_ack.pending_r=1;break;case 2:p.exitcode[3]=7;break;case 3:p.checks&=~MOSAIC_CHECK_ACK;break;case 4:p.ack_idle=0;break;case 5:p.generation=8;break;case 6:p.sequence=99;break;case 7:p.elapsed_raw=0;break;case 8:p.elapsed_address+=4;break;case 9:p.submit_enter=9;break;case 10:p.ack_done=12;break;case 11:p.cdma_status=0x40;break;case 12:p.before_ack.jobs=31;break;case 13:p.checks&=~MOSAIC_CHECK_GUARDS;break;case 14:p.checks&=~MOSAIC_CHECK_IMMUTABLE;break;case 15:p.tex_pending=1;break;case 16:e.generation=10;break;}
  assert(mosaic_state_complete(&c,&p)<0);assert(e.quarantined&&e.job.pinned&&e.job.state==VXL_QUARANTINED&&h.first_error<0);int first=h.first_error;
  assert(mosaic_state_fault(&c,-ECANCELED)==first);assert(mosaic_reap(&c)<0);assert(mosaic_next_job(&c,1,&j)<0);assert(!h.first&&!h.phase);
  /* Failed evidence remains readable to the same owner, but can never unpin
   * or earn REAP. A failed row is not a completed successful job. */
  assert(!mosaic_evidence_delivered(&c,77,100,0,512u,1));
  assert(mosaic_reap(&c)==first&&e.job.pinned&&h.evidence.result==first);
  printf("MOSAIC_MODEL_FAULT case=%u result=%d state=%u pinned=%u model_elapsed_raw=%u elapsed_valid=%u phase=%u first=%u raw_gpu_cycles=null\n",fault,first,e.job.state,e.job.pinned,h.evidence.observed.elapsed_raw,h.evidence.elapsed_valid,h.phase,h.first);
 }
 setup();begin();assert(mosaic_state_fault(&c,-ETIMEDOUT)==-ETIMEDOUT);assert(e.quarantined&&e.job.pinned);
 setup();begin();p=proof();assert(!mosaic_state_complete(&c,&p));assert(mosaic_evidence_delivered(&c,77,100,0,1,0)==-EFAULT);assert(e.quarantined&&e.job.pinned);
 setup();assert(!mosaic_frame_begin(&c,77,run1,0,1,0));assert(mosaic_next_job(&c,4097,&j)==-ERANGE);h.sequence=0xffffffffu;assert(mosaic_next_job(&c,1,&j)==-EOVERFLOW);
 setup_size(1);assert(!mosaic_frame_begin(&c,77,run1,0,1,0));
 for(unsigned batch=0;batch<4;++batch){
  unsigned count=batch<3?4096:2112;assert(!mosaic_next_job(&c,count,&j));assert(j.tile_first==batch*4096&&j.sequence==batch+1);
  assert(!mosaic_submit_begin(&c,77,100+batch,31+batch,&j,10));p=proof();p.before_ack.id+=batch;p.before_ack.jobs+=batch;
  assert(!mosaic_state_complete(&c,&p));deliver();
 }
 assert(h.phase==1&&!h.first);assert(mosaic_submit_begin(&c,77,103,40,&j,10)==-EOVERFLOW);
 puts("TEST_ONLY_MOSAIC_STATE_HOST_FAULTS_AND_DELIVERY_PASS_NOT_ENGINE_NOT_GPU");return 0;
}

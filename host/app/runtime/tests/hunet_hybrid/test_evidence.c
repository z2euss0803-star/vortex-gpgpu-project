#include "fixture.h"
static void final_bytes(struct hfixture *h){
 vxl_u32 owners[1014];for(unsigned i=0;i<1014;i++)owners[i]=i<254?1:i<508?2:i<761?3:4;
 assert(!fio(&h->f,0x87044080,owners,sizeof(owners),1));memset(map(&h->f,VXL_HUNET_LOGITS,32420),0,32420);
}
static void negative_end(int kind){
 struct hfixture h;hinit(&h);h.f.c.next_sequence=1024;h.f.c.hybrid.logical_jobs=1023;h.f.c.hybrid.cpu_jobs=13;h.f.c.hybrid.gpu_jobs=1010;h.f.e.snapshot.jobs=1010;
 vxl_hunet_hash(map(&h.f,VXL_HUNET_INPUT,196608),196608,h.f.c.input_sha);h.f.c.evidence.valid_flags=VXL_HUNET_VALID_CRT;h.f.c.evidence.crt_guard_flags=3;final_bytes(&h);
 if(kind==1)*map(&h.f,0x87044080,1)=9;
 if(kind==2)*map(&h.f,VXL_HUNET_INPUT-64,1)^=1;
 if(kind==3){vxl_u32 nan=0x7fc00000;memcpy(map(&h.f,VXL_HUNET_LOGITS,4),&nan,4);}
 if(kind==4)h.f.e.snapshot.idle=0;
 struct vxl_hybrid_end_v1 end;assert(vxl_hybrid_end(&h.f.c,&end)==-EIO);assert(h.f.e.quarantined);hfree(&h);
}
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 struct hfixture h;hinit(&h);h.f.c.active=0;float *x=malloc(1572864),*y=calloc(393216,4),*input=malloc(196608);assert(x&&y&&input);
 file_read("../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32",input,196608);
 struct vxl_hunet_begin_v1 roi={.magic=VXL_HUNET_BEGIN_MAGIC,.version=1,.bytes=128,.sample_kind=4,.input_ptr=(uintptr_t)input,.input_bytes=196608};roi.run_id[0]=0x12;vxl_hunet_hash(input,196608,roi.input_sha256);assert(!vxl_hunet_begin(&h.f.c,&roi,&h.f.c.io));
 struct vxl_request r=req();r.handle=h.f.c.handles[0];r.handle2=h.f.c.handles[3];r.trust_id=8;r.deadline_ms=60000;unsigned cpus=0;
 for(unsigned seq=1;seq<=1023;seq++){
  const vxl_u32 *j=vxl_hunet_catalog_job(seq);assert(h.f.c.next_sequence==seq);
  if(j[3]!=6){
   struct vxl_hunet_job_evidence_v1 ev;assert(!vxl_hunet_stage_args(&h.f.c,j,128));assert(!vxl_engine_call(&h.f.e,123,VXL_SUBMIT,&r));h.f.done=1;assert(!vxl_engine_poll(&h.f.e));assert(!vxl_hunet_get_evidence(&h.f.c,r.job_id,&ev));assert(ev.jobs_after-ev.jobs_before==1&&ev.program_id==8);vxl_hunet_evidence_delivered(&h.f.c,&ev);assert(!vxl_engine_call(&h.f.e,123,VXL_REAP,&r));
  }else{
   /* Test payload, not a model output. No fake tensor arithmetic in GPU callbacks. */
   memset(map(&h.f,j[8],(vxl_u64)j[7]*4),0,j[7]*4);struct vxl_hybrid_begin_v1 b=hbegin_request(&h,x);assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));
   struct vxl_hybrid_commit_v1 c={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.generation=b.generation,.sequence=seq,.stage=b.stage,.algorithm=1,.token=b.token,.output_ptr=(uintptr_t)y,.output_bytes=b.output_bytes,.threads=4};memcpy(c.run_id,b.run_id,16);vxl_hunet_hash(y,b.output_bytes,c.output_sha256);assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));
   struct vxl_hybrid_cpu_evidence_v1 ev;assert(vxl_hybrid_get_cpu_evidence(&h.f.c,b.token+1,&ev)==-ESTALE);assert(!vxl_hybrid_get_cpu_evidence(&h.f.c,b.token,&ev));assert(ev.gpu_jobs_after==ev.gpu_jobs_before&&ev.check_flags==63);assert(!vxl_hybrid_payload_validate(VXL_HYBRID_GET_CPU_EVIDENCE,&ev,384));
   struct vxl_request er=req();er.user_ptr=(uintptr_t)&ev;er.length=384;er.job_id=b.token;h.f.output_fault=1;assert(vxl_engine_call(&h.f.e,123,VXL_HYBRID_GET_CPU_EVIDENCE,&er)==-EFAULT);assert(h.f.c.next_sequence==seq);h.f.output_fault=0;assert(!vxl_engine_call(&h.f.e,123,VXL_HYBRID_GET_CPU_EVIDENCE,&er));
   struct vxl_hybrid_delivery delivered;vxl_hybrid_delivery_capture(&h.f.c,VXL_HYBRID_GET_CPU_EVIDENCE,&delivered);assert(h.f.c.next_sequence==seq); /* outer copy fault: no finish */
   assert(vxl_engine_call(&h.f.e,123,VXL_SUBMIT,&r)==-EBUSY);vxl_hybrid_delivery_finish(&h.f.c,&delivered);assert(h.f.c.next_sequence==seq+1);vxl_hybrid_delivery_finish(&h.f.c,&delivered);assert(h.f.c.next_sequence==seq+1);cpus++;
  }
 }
 assert(cpus==13&&h.f.starts==1010&&h.f.hw_jobs==1010&&h.f.c.hybrid.logical_jobs==1023&&h.f.c.hybrid.cpu_jobs==13&&h.f.c.hybrid.gpu_jobs==1010);
 final_bytes(&h);struct vxl_hybrid_end_v1 end;h.f.c.hybrid.cpu_jobs--;assert(vxl_hybrid_end(&h.f.c,&end)==-EBUSY);h.f.c.hybrid.cpu_jobs++;
 assert(!vxl_hybrid_end(&h.f.c,&end));assert(end.logical_jobs==1023&&end.gpu_jobs==1010&&end.cpu_jobs==13&&end.stages==100&&end.hw_jobs_after-end.hw_jobs_before==1010&&end.last_gpu_job_id==1010);assert(!vxl_hybrid_payload_validate(VXL_HYBRID_END,&end,256));
 struct vxl_hybrid_delivery d;vxl_hybrid_delivery_capture(&h.f.c,VXL_HYBRID_END,&d);vxl_hybrid_delivery_finish(&h.f.c,&d);assert(!h.f.c.active);
 hfree(&h);free(x);free(y);free(input);for(int kind=1;kind<=4;kind++)negative_end(kind);
 puts("HOST_HYBRID_DELIVERY_1010_GPU_13_CPU_END_FAIL_STOP_NOT_MODEL_OR_BOARD");return 0;
}

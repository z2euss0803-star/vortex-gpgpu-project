#include "../hunet_hybrid/fixture.h"
#include "../../generated/hunet_fused/catalog.h"
static void evidence_ready(struct vxl_hunet_ctx*c,const struct vxl_fused_meta*m){
 c->fused.pending=*m;c->fused.acked=1;c->evidence_delivered=1;
 c->evidence.software_state=VXL_COMPLETED;c->evidence.result=0;c->evidence.generation=c->engine->generation;
 c->evidence.sequence=m->first;c->evidence.stage_id=m->stage;c->evidence.op=m->op;
 c->evidence.valid_flags=VXL_HUNET_VALID_CRT|VXL_HUNET_VALID_TIMING|VXL_HUNET_VALID_HW_SNAPSHOT|VXL_HUNET_VALID_EXIT_SNAPSHOT|VXL_HUNET_VALID_OWNER;
}
static void test_fused_range_advance(void){
 struct hfixture h;struct vxl_fused_release r={0};struct vxl_hunet_ctx*c;struct vxl_fused_meta m;unsigned i=0,cpu=0;
 hinit(&h);c=&h.f.c;c->fused.release=&r;c->program_id=9;c->next_sequence=1;
 assert(!vxl_fused_begin(c));m=vxl_fused_metadata[0];evidence_ready(c,&m);
 c->fused.acked=0;assert(vxl_fused_gpu_completed(c,&m));assert(c->next_sequence==1);
 c->fused.acked=1;c->evidence_delivered=0;assert(vxl_fused_gpu_completed(c,&m));assert(c->next_sequence==1);
 c->evidence_delivered=1;c->evidence.result=-EIO;assert(vxl_fused_gpu_completed(c,&m));assert(c->next_sequence==1);
 evidence_ready(c,&m);assert(!vxl_fused_gpu_completed(c,&m));assert(c->next_sequence==97&&c->hybrid.gpu_jobs==1&&c->hybrid.logical_jobs==96);
 assert(vxl_fused_gpu_completed(c,&m));i++;
 while(c->next_sequence<=1023){
  const vxl_u32*j=vxl_hunet_catalog_job(c->next_sequence);
  if(j[3]==6){assert(!vxl_hybrid_enter_cpu(c));assert(c->hybrid.sequence==j[4]&&c->hybrid.stage==j[30]);
   c->hybrid.phase=VXL_HYBRID_CPU_COMMITTED;assert(!vxl_hybrid_cpu_delivered(c));cpu++;
  }else{assert(i<103);m=vxl_fused_metadata[i++];assert(m.first==c->next_sequence);
   evidence_ready(c,&m);assert(!vxl_fused_gpu_completed(c,&m));}
 }
 assert(i==103&&cpu==13&&c->hybrid.logical_jobs==1023&&c->hybrid.gpu_jobs==103&&c->hybrid.cpu_jobs==13);
 assert(vxl_hybrid_expected_gpu_jobs(c)==103);
 h.f.e.generation++;assert(vxl_hybrid_enter_cpu(c)==-ESTALE);
 h.f.e.generation--;h.f.e.owner++;assert(vxl_fused_gpu_completed(c,&m)==-ESTALE);
 hfree(&h);
}
static void test_cpu_delivery_and_owner_generation(void){
 struct hfixture h;struct vxl_fused_release r={0};struct vxl_hunet_ctx*c;struct vxl_hybrid_begin_v1 b;
 struct vxl_hybrid_commit_v1 p={0};struct vxl_hybrid_cpu_evidence_v1 ev;struct vxl_hybrid_delivery d;
 vxl_u8 *input=calloc(1,1572864),*output=calloc(1,1572864);unsigned writes;
 assert(input&&output);hinit(&h);c=&h.f.c;c->program_id=9;c->fused.release=&r;
 assert(!vxl_hunet_guards(c,1));
 b=hbegin_request(&h,input);assert(!vxl_hybrid_begin_cpu(c,&b));
 p.magic=VXL_HYBRID_COMMIT_MAGIC;p.version=1;p.bytes=128;memcpy(p.run_id,c->run_id,16);p.generation=h.f.e.generation;
 p.sequence=b.sequence;p.stage=b.stage;p.algorithm=1;p.threads=4;p.token=b.token;p.output_ptr=(uintptr_t)output;p.output_bytes=b.output_bytes;
 vxl_hunet_hash(output,p.output_bytes,p.output_sha256);
 assert(!vxl_hybrid_commit_cpu(c,&p));writes=h.write_blocks;
 h.f.output_fault=1;assert(!vxl_hybrid_get_cpu_evidence(c,b.token,&ev));
 vxl_hybrid_delivery_capture(c,VXL_HYBRID_GET_CPU_EVIDENCE,&d);
 /* Emulate failed envelope copy: delivery_finish is deliberately not called. */
 assert(c->next_sequence==296&&c->hybrid.cpu_jobs==0);
 assert(!vxl_hybrid_commit_cpu(c,&p));assert(h.write_blocks==writes);
 h.f.output_fault=0;vxl_hybrid_delivery_finish(c,&d);assert(c->next_sequence==297&&c->hybrid.cpu_jobs==1);
 vxl_hybrid_delivery_finish(c,&d);assert(c->next_sequence==297&&h.write_blocks==writes);
 h.f.clock_ns=200000000;c->hybrid.network_deadline_ns=h.f.clock_ns;vxl_hybrid_poll(c);assert(h.f.e.quarantined);
 hfree(&h);free(input);free(output);
}
int main(void){(void)seal_fixture;test_fused_range_advance();test_cpu_delivery_and_owner_generation();
 puts("FUSED_103_GPU_13_CPU_1023_LOGICAL_AND_EXACT_DELIVERY_STATE_PASS_NOT_GPU");}

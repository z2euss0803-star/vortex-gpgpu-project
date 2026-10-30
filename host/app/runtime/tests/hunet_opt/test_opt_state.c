#include "../hunet_hybrid/fixture.h"
static void ready(struct vxl_hunet_ctx *c,const struct vxl_opt_meta *m){
 c->opt.pending=*m;c->opt.acked=1;c->evidence_delivered=1;
 memset(&c->evidence,0,sizeof(c->evidence));c->evidence.program_id=10;
 c->evidence.software_state=VXL_COMPLETED;c->evidence.generation=c->engine->generation;
 c->evidence.sequence=m->first;c->evidence.stage_id=m->first_stage;c->evidence.op=m->op;
 c->evidence.valid_flags=31;
}
int main(void){
 struct hfixture h;struct vxl_opt_release r={0};struct vxl_hunet_ctx *c;unsigned gpu=0,cpu=0;
 (void)hbegin_request;(void)seal_fixture;hinit(&h);c=&h.f.c;r.catalog=vxl_opt_catalog_default();c->opt.release=&r;c->program_id=10;c->next_sequence=1;
 assert(!vxl_opt_begin(c));assert(vxl_hybrid_expected_gpu_jobs(c)==r.catalog->gpu_jobs);
 while(c->next_sequence<=1023){
  const vxl_u32 *logical=vxl_hunet_catalog_job(c->next_sequence);
  if(logical[3]==6){assert(!vxl_hybrid_enter_cpu(c));c->hybrid.phase=VXL_HYBRID_CPU_COMMITTED;assert(!vxl_hybrid_cpu_delivered(c));cpu++;}
  else{
   struct vxl_opt_meta m=r.catalog->metadata[gpu];assert(m.first==c->next_sequence);ready(c,&m);
   c->opt.acked=0;assert(vxl_opt_gpu_completed(c,&m));assert(c->next_sequence==m.first);
   c->opt.acked=1;c->evidence_delivered=0;assert(vxl_opt_gpu_completed(c,&m));assert(c->next_sequence==m.first);
   ready(c,&m);c->evidence.result=-EIO;assert(vxl_opt_gpu_completed(c,&m));assert(c->next_sequence==m.first);
   ready(c,&m);assert(!vxl_opt_gpu_completed(c,&m));assert(vxl_opt_gpu_completed(c,&m));gpu++;
  }
 }
 assert(gpu==r.catalog->gpu_jobs&&cpu==13&&c->hybrid.logical_jobs==1023&&c->hybrid.gpu_jobs==gpu&&c->hybrid.cpu_jobs==13);
 h.f.e.owner++;assert(vxl_opt_gpu_completed(c,&c->opt.pending)==-ESTALE);h.f.e.owner--;
 h.f.e.generation++;assert(vxl_hybrid_enter_cpu(c)==-ESTALE);h.f.e.generation--;
 hfree(&h);puts("HOST_OPT_CATALOG_RANGE_GPU_CPU_DELIVERY_STATE_PASS_NOT_GPU");return 0;
}

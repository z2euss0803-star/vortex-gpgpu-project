#include "../hunet_hybrid/fixture.h"
#include <vxl/mgmt_timing.h>
static vxl_u64 tick(void *p){struct fixture *f=p;return ++f->clock_ns;}
static void check_row(struct vxl_mgmt_row *r){
 vxl_u64 sum=0;unsigned i;assert(!r->flags);
 for(i=0;i<VXL_MGMT_FIELD_COUNT;i++)sum+=r->ns[i];
 assert(sum<=r->total_ns && r->calls[VXL_MGMT_VALIDATE]);
}
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 struct vxl_hybrid_cpu_evidence_v1 saved={0};unsigned saved_reads=0,saved_writes=0;
 for(unsigned timed=0;timed<2;timed++){
  struct hfixture h;struct vxl_mgmt_trace t={0};struct vxl_mgmt_row rows[2];struct vxl_mgmt_key key={123,1,0,VXL_HYBRID_CPU_BEGIN};
  float *x=malloc(1572864),*y=malloc(1572864);assert(x&&y);hinit(&h);h.f.clock_ns=100000000;h.f.c.io.now_ns=tick;
  for(unsigned i=0;i<393216;i++)y[i]=(int)(i%17)-8.f;
  t.rows=rows;t.capacity=2;
  if(timed)h.f.c.mgmt_row=vxl_mgmt_begin(&t,&key,tick(&h.f));
  struct vxl_hybrid_begin_v1 b=hbegin_request(&h,x);assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));
  if(timed){vxl_mgmt_finish(&t,tick(&h.f),0);check_row(&rows[0]);assert(rows[0].calls[VXL_MGMT_ALLOC]&&rows[0].calls[VXL_MGMT_INPUT_READ]&&rows[0].calls[VXL_MGMT_HASH_INPUT]&&rows[0].calls[VXL_MGMT_COPY_USER]);}
  struct vxl_hybrid_commit_v1 c={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.generation=b.generation,.sequence=b.sequence,.stage=b.stage,.algorithm=1,.token=b.token,.output_ptr=(uintptr_t)y,.output_bytes=b.output_bytes,.cpu_duration_ns=12345,.threads=4};
  memcpy(c.run_id,b.run_id,16);vxl_hunet_hash(y,1572864,c.output_sha256);
  key.command=VXL_HYBRID_CPU_COMMIT;key.token=b.token;
  if(timed)h.f.c.mgmt_row=vxl_mgmt_begin(&t,&key,tick(&h.f));
  assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));
  if(timed){vxl_mgmt_finish(&t,tick(&h.f),0);check_row(&rows[1]);assert(rows[1].calls[VXL_MGMT_SNAPSHOT]&&rows[1].calls[VXL_MGMT_HASH_OUTPUT]);assert(rows[1].calls[VXL_MGMT_ORIGINAL_READ]==384&&rows[1].calls[VXL_MGMT_ORIGINAL_HASH]>=384);assert(rows[1].calls[VXL_MGMT_OUTPUT_WRITE]==384&&rows[1].calls[VXL_MGMT_READBACK]==384);h.f.c.mgmt_row=NULL;}
  assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));
  struct vxl_hybrid_cpu_evidence_v1 ev=h.f.c.hybrid.evidence;
  ev.read_ns=ev.write_ns=ev.guard_ns=ev.total_ns=ev.deadline_ns=0;
  if(!timed){saved=ev;saved_reads=h.read_blocks;saved_writes=h.write_blocks;}
  else {assert(!memcmp(&saved,&ev,sizeof(ev)));assert(saved_reads==h.read_blocks&&saved_writes==h.write_blocks);}
  hfree(&h);free(x);free(y);
 }
 puts("CPU_TIMING_EXCLUSIVE_AND_UNCHANGED_TRANSACTIONS_PASS_HOST_ONLY");return 0;
}

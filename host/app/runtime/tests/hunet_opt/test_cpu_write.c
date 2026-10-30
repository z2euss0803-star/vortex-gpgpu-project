#include "../hunet_hybrid/fixture.h"
static struct vxl_opt_release opt;
static void opt_init(struct hfixture *h){hinit(h);memset(&opt,0,sizeof(opt));opt.catalog=vxl_opt_catalog_default();h->f.c.opt.release=&opt;h->f.c.program_id=10;h->f.c.digest_enabled=1;h->f.c.digest_backend=VXL_DIGEST_PORTABLE;assert(!vxl_hunet_guards(&h->f.c,1));h->read_blocks=h->write_blocks=0;}
static int alloc_fail;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t a,size_t b){if(alloc_fail)return NULL;return __real_calloc(a,b);}
static struct vxl_hybrid_commit_v1 prepare(struct hfixture *h,float *x,float *y){
 struct vxl_hybrid_begin_v1 b=hbegin_request(h,x);assert(!vxl_hybrid_begin_cpu(&h->f.c,&b));
 for(unsigned i=0;i<393216;i++)y[i]=(int)(i%17)-8.f;
 struct vxl_hybrid_commit_v1 c={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.generation=b.generation,.sequence=b.sequence,.stage=b.stage,.algorithm=1,.token=b.token,.output_ptr=(uintptr_t)y,.output_bytes=b.output_bytes,.cpu_duration_ns=12345,.threads=4};memcpy(c.run_id,b.run_id,16);vxl_hunet_hash(y,1572864,c.output_sha256);h->write_blocks=0;return c;
}
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 float *x=malloc(1572864),*y=malloc(1572864);assert(x&&y);struct hfixture h;opt_init(&h);struct vxl_hybrid_commit_v1 c=prepare(&h,x,y),bad=c;
 bad.output_ptr=~(vxl_u64)0-16;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-EFAULT);
 bad=c;bad.output_bytes-=4;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-EINVAL);
 bad=c;bad.token++;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-ESTALE);
 bad=c;bad.generation++;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-ESTALE);
 bad=c;bad.run_id[1]++;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-ESTALE);
 h.f.e.owner++;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-ESTALE);h.f.e.owner--;
 bad=c;bad.output_sha256[0]^=1;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-EACCES);
 h.input_fault=1;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-EFAULT);h.input_fault=0;
 vxl_u32 nan=0x7fc00000,save;memcpy(&save,y+100,4);memcpy(y+100,&nan,4);vxl_hunet_hash(y,1572864,bad.output_sha256);assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-EDOM);memcpy(y+100,&save,4);
 assert(h.write_blocks==0&&h.f.c.next_sequence==296);
 assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));assert(h.f.c.hybrid.phase==VXL_HYBRID_CPU_COMMITTED&&h.write_blocks==384);
 const vxl_u32 *j=vxl_hunet_catalog_job(296);assert(!memcmp(map(&h.f,j[13],1572864),y,1572864));
 unsigned writes=h.write_blocks,reads=h.read_blocks;assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));assert(writes==h.write_blocks&&reads==h.read_blocks&&h.f.c.next_sequence==296&&h.f.e.next_job==0);
 bad=c;bad.output_ptr+=4;assert(vxl_hybrid_commit_cpu(&h.f.c,&bad)==-ESTALE);assert(writes==h.write_blocks);
 assert(!vxl_hybrid_cpu_delivered(&h.f.c));assert(!vxl_hybrid_commit_cpu(&h.f.c,&c));assert(writes==h.write_blocks&&h.f.c.next_sequence==297);
 struct vxl_request q=req();q.handle=h.f.c.handles[2];q.user_ptr=(uintptr_t)y;q.length=4;assert(vxl_engine_call(&h.f.e,123,VXL_COPY_TO,&q)==-EACCES);hfree(&h);
 unsigned failures[]={1,100,384};
 for(unsigned n=0;n<3;n++){
  opt_init(&h);c=prepare(&h,x,y);h.fail_write=failures[n];assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-EIO);
  assert(h.f.e.quarantined&&h.f.e.job.pinned&&h.f.c.hybrid.phase==VXL_HYBRID_ABORTED&&h.f.c.next_sequence==296&&h.f.c.hybrid.bytes_written==(vxl_u64)(failures[n]-1)*4096);
  assert(h.f.c.hybrid.cpu_output&&h.f.c.hybrid.cpu_input);q=req();q.handle=h.f.c.handles[0];q.handle2=h.f.c.handles[3];q.trust_id=10;q.deadline_ms=60000;assert(vxl_engine_call(&h.f.e,123,VXL_SUBMIT,&q)==-EIO);assert(h.f.starts==0);vxl_engine_close(&h.f.e,123);assert(h.f.c.hybrid.cpu_output&&h.f.e.owner==123);hfree(&h);
 }
 opt_init(&h);c=prepare(&h,x,y);alloc_fail=1;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-ENOMEM);alloc_fail=0;assert(h.write_blocks==0&&h.f.e.quarantined);hfree(&h);
 opt_init(&h);c=prepare(&h,x,y);h.corrupt_write=384;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-EIO);assert(h.f.e.quarantined&&h.f.c.next_sequence==296);hfree(&h);
 opt_init(&h);c=prepare(&h,x,y);h.corrupt_guard=384;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-EIO);assert(h.f.e.quarantined&&h.f.c.next_sequence==296);hfree(&h);
 opt_init(&h);c=prepare(&h,x,y);h.f.clock_ns=h.f.c.hybrid.deadline_ns-1000;h.write_tick_ns=600;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-ETIMEDOUT);assert(h.write_blocks==2&&h.f.e.quarantined&&h.f.c.next_sequence==296);hfree(&h);
 free(x);free(y);puts("HOST_OPT_FULL_OUTPUT_SNAPSHOT_PARTIAL_WRITE_QUARANTINE_IDEMPOTENCE_PASS_NOT_BOARD");return 0;
}

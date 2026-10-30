#include "fixture.h"
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 struct hfixture h;hinit(&h);float *out=malloc(1572864);assert(out);const vxl_u32 *j=vxl_hunet_catalog_job(296);float *device=(float *)map(&h.f,j[8],1572864);
 for(unsigned i=0;i<393216;i++)device[i]=(int)(i%101)-50.f;
 struct vxl_hybrid_begin_v1 b=hbegin_request(&h,out),bad=b;
 bad.input_bytes++;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-EINVAL);
 bad=b;bad.input_ptr=~(vxl_u64)0-16;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-EFAULT);
 bad=b;bad.generation++;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-ESTALE);
 bad=b;bad.stage++;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-ESTALE);
 h.f.c.next_sequence=295;assert(vxl_hybrid_begin_cpu(&h.f.c,&b)==-ESTALE);h.f.c.next_sequence=296;
 assert(h.read_blocks==0&&h.f.c.hybrid.phase==VXL_HYBRID_GPU_READY);
 h.f.output_fault=1;assert(vxl_hybrid_begin_cpu(&h.f.c,&b)==-EFAULT);unsigned reads=h.read_blocks;
 assert(reads>=384&&h.max_block==4096&&h.f.c.next_sequence==296&&h.f.c.hybrid.input_ready);
 h.f.output_fault=0;assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));assert(h.read_blocks==reads&&b.token&&!memcmp(out,device,1572864));
 vxl_u8 sha[32];vxl_hunet_hash(out,1572864,sha);assert(!memcmp(sha,b.input_sha256,32));
 device[1]+=1;assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));assert(h.read_blocks==reads&&out[1]!=device[1]);
 bad=b;bad.token++;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-ESTALE);bad=b;bad.run_id[1]++;assert(vxl_hybrid_begin_cpu(&h.f.c,&bad)==-ESTALE);
 assert(VXL_PS_SIZE==1048576&&h.f.e.next_job==0&&h.f.c.hybrid.cpu_jobs==0&&h.write_blocks==0);
 hfree(&h);free(out);puts("HOST_FIXED_1_5M_SNAPSHOT_RETRY_NO_DUPLICATE_CDMA_PASS_NOT_BOARD");return 0;
}

#include <vxl/s8.h>
#include <vxl/engine.h>
#include <assert.h>
#include <stdio.h>
static void crc(struct vxl_s8_job_v1 *j) {j->args_crc32=vxl_s8_job_crc(j);}
static struct vxl_s8_job_v1 job(vxl_u32 gen) {
 struct vxl_s8_job_v1 j={.magic=0x314a3853,.version=1,.bytes=128,.case_id=101,.k=1,.core_mask=1,
  .a_base=0x81000040,.b_base=0x81200040,.input_slot_stride=0x20000,.output_base=0x81400000,.output_stride=0x1000,
  .identity_base=0x81410000,.control_base=0x81411000,.spin_limit=65536,.generation=gen,.sequence=1,.run_id={1},.binding_tag={0x55}};
 crc(&j);return j;
}
int main(void) {
 struct vxl_engine e;struct vxl_s8_ctx c;vxl_u8 tag[16]={0x55};
 assert(vxl_engine_init_profile(&e,0,0,10,5)==0);assert(vxl_s8_context_init(&c,&e,tag)==0);assert(vxl_engine_open(&e,42)==0);
 struct vxl_s8_job_v1 good=job(e.generation),out,bad;struct vxl_s8_ctx before=c;
 assert(vxl_s8_validate_job(&c,&good)==0);assert(vxl_s8_canonical_job(&c,&good,&out)==0);
 assert(out.generation==e.generation && out.sequence==1 && out.args_crc32==vxl_s8_job_crc(&out));
 assert(memcmp(&c,&before,sizeof(c))==0);
#define BAD(field,value) do { bad=good;bad.field=(value);crc(&bad);assert(vxl_s8_validate_job(&c,&bad)<0);assert(!memcmp(&c,&before,sizeof(c))); } while(0)
 BAD(magic,0);BAD(version,2);BAD(bytes,127);BAD(case_id,201);BAD(case_id,(209u<<8)|1);
 BAD(flags,2);BAD(k,0);BAD(k,4097);BAD(core_mask,3);BAD(owner_warp,4);BAD(spin_limit,65537);
 BAD(a_base,0x81000000);BAD(b_base,0x81200000);BAD(input_slot_stride,0x10000);BAD(output_stride,0x2000);
 BAD(output_base,0x81400040);BAD(identity_base,0x81410040);BAD(control_base,0x81411040);
 BAD(generation,e.generation-1);BAD(sequence,0);BAD(sequence,2);BAD(sequence,0xffffffffu);BAD(reserved[4],1);
 bad=good;bad.args_crc32^=1;assert(vxl_s8_validate_job(&c,&bad)<0);
 bad=good;memset(bad.run_id,0,16);crc(&bad);assert(vxl_s8_validate_job(&c,&bad)<0);
 bad=good;bad.binding_tag[4]=1;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)<0);
 bad=good;bad.case_id=3;bad.k=8;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)==0);
 bad.flags=1;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)<0);
 bad=good;bad.case_id=201u<<8;bad.k=2;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)==0);
 assert(vxl_s8_accept_job(&c,&good)==0);assert(c.next_sequence==2 && c.nonce_locked);
 assert(vxl_s8_accept_job(&c,&good)<0);
 bad=good;bad.sequence=2;bad.run_id[0]=2;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)<0);
 bad.run_id[0]=1;crc(&bad);assert(vxl_s8_validate_job(&c,&bad)==0);
 e.generation++;assert(vxl_s8_validate_job(&c,&bad)<0);
 puts("S8_CANONICAL_VALIDATION_PASS rejects_do_not_mutate=1");return 0;
}

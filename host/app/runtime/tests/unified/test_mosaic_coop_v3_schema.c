/* Real portable C catalog/schema/owner/ref/relocation mechanisms. The only
 * injected operation is reading a private HOST args object, with no device I/O. */
#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <stdio.h>
#include <stdlib.h>
extern int mosaic_coop_v3_target_check(const vxl_u32 *);
static unsigned checks;
#define CHECK(x,m) do{++checks;if(!(x)){fprintf(stderr,"COOP_SCHEMA_C_ASSERT %s check=%u\n",m,checks);return 78;}}while(0)
static int copy_args(void *context,struct vxl_v2_object *o,vxl_u64 off,void *out,vxl_u64 n,int write){
 (void)context;if(write||o->purpose!=VXL_V2_ARGS||off>128||n>128-off)return -EINVAL;
 memcpy(out,(unsigned char *)o->storage+off,(size_t)n);return 0;
}
static const struct vxl_v2_memory_ops memory_ops={NULL,NULL,copy_args};
int main(int argc,char **argv){
 unsigned char schema[4096],raw[4672],identity[4608],changed[4096];size_t n;FILE *file;
 struct vxl_v2_schema_header sh;struct vxl_v2_schema_field field;
 struct vxl_v2_catalog_header ch={0};struct vxl_v2_catalog_entry entry={0};struct vxl_v2_catalog catalog;
 struct vxl_v2_provider provider={.flags=1};struct vxl_v2_program_table table={0};struct vxl_v2_ctx ctx={0};
 struct vxl_v2_job job={.major=2,.size=512,.cookie=1,.program_handle=11,.program_generation=12,.device_generation=7,.args_handle=1,.args_generation=1,.execution_budget_ns=100,.args_bytes=128,.num_refs=8};
 vxl_u32 packet[32]={3,128,1,0},out[32],abi=0,options=0,addresses=0;
 const unsigned access[8]={1,2,3,1,1,3,3,3};const vxl_u64 offsets[8]={0x1000,0x3000,0x2000,0x4000,0x4000,0x3000,0x3000,0x3000};
 CHECK(argc==2,"generated schema path required");file=fopen(argv[1],"rb");CHECK(file!=NULL,"schema file open");n=fread(schema,1,sizeof schema,file);CHECK(!ferror(file)&&feof(file)&&!fclose(file),"whole schema read");
 CHECK(!vxl_v2_schema_check(schema,(vxl_u32)n,128),"production C schema checker accepts generated wire");memcpy(&sh,schema,64);
 CHECK(n==1536&&sh.field_count==23&&sh.version==1,"ABI3 uses existing generic schema container without HW/UAPI changes");
 for(unsigned i=0;i<sh.field_count;i++){
  memcpy(&field,schema+64+i*64,64);
  if(field.kind==VXL_V2_SCALAR32&&field.arg_offset==0)abi=field.min_value==3&&field.max_value==3;
  if(field.kind==VXL_V2_SCALAR32&&field.arg_offset==124)options=field.min_value==0&&field.max_value==1;
  if(field.kind==VXL_V2_ADDRESS32){CHECK(field.data_ref<8&&field.arg_offset==16+field.data_ref*4&&field.access==access[field.data_ref]&&field.count_offsets[0]==48+field.data_ref*4,"C binding exact role offsets/access/extent words");addresses|=1u<<field.data_ref;}
 }
 CHECK(abi&&options&&addresses==255,"exact ABI3/options and eight refs required");
 memcpy(changed,schema,n);changed[0]^=1;CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"corrupt schema rejected");
 memset(provider.hardware_contract,1,32);memset(provider.bit,2,32);memset(provider.xsa,3,32);
 entry.namespace_id=1;entry.logical_profile=1;entry.logical_program=65568;entry.code_bytes=4;entry.rodata_offset=4;entry.rodata_bytes=4;
 entry.entry_pc=0x80000000;entry.args_gpu=0x80100040;entry.args_bytes=128;entry.schema_offset=576;entry.schema_bytes=(vxl_u32)n;entry.reloc_count=sh.field_count;entry.provider_flags=1;
 memcpy(entry.hardware_contract,provider.hardware_contract,32);memcpy(entry.bit,provider.bit,32);memcpy(entry.xsa,provider.xsa,32);
 vxl_hunet_hash(schema,(vxl_u32)n,entry.schema);memcpy(identity,&entry,512);memcpy(identity+512,schema,n);vxl_hunet_hash(identity,(vxl_u32)(512+n),entry.entry_id);
 ch.magic=VXL_V2_CATALOG_MAGIC;ch.version=1;ch.total_bytes=(vxl_u32)(576+n);ch.count=1;ch.entry_bytes=512;
 memcpy(raw+64,&entry,512);memcpy(raw+576,schema,n);vxl_hunet_hash(raw+64,(vxl_u32)(512+n),ch.payload_sha);memcpy(raw,&ch,64);
 CHECK(!vxl_v2_catalog_open(&catalog,raw,ch.total_bytes,&provider),"actual C hashed catalog admission");table.catalog=catalog;
 table.programs[0].entry=entry;table.programs[0].ref.handle=11;table.programs[0].ref.generation=12;table.programs[0].ref.device_generation=7;memcpy(table.programs[0].ref.entry_id,entry.entry_id,32);table.programs[0].users=1;
 ctx.owner=9;ctx.epoch=7;ctx.ops=&memory_ops;ctx.programs=&table;
 ctx.objects[0]=(struct vxl_v2_object){.handle=1,.generation=1,.owner=9,.epoch=7,.length=128,.allocated=128,.gpu=0x80100040,.domain=VXL_PL,.purpose=VXL_V2_ARGS,.access=3,.storage=packet};
 ctx.objects[1]=(struct vxl_v2_object){.handle=2,.generation=2,.owner=9,.epoch=7,.length=65536,.allocated=65536,.gpu=0x80800000,.domain=VXL_PL,.purpose=VXL_V2_DATA,.access=3};
 for(unsigned i=0;i<8;i++){packet[12+i]=64;job.refs[i]=(struct vxl_v2_ref){2,2,offsets[i],64,access[i],VXL_PL,7};}
 packet[12]=1152;packet[14]=576;packet[19]=1088;packet[20]=3;packet[21]=5;packet[22]=8;packet[23]=12;packet[24]=3;packet[25]=packet[26]=1;packet[27]=32;packet[29]=1;packet[30]=0x3f800000;
 for(unsigned i=0;i<8;i++)job.refs[i].length=packet[12+i];
 for(unsigned option=0;option<2;option++){
  packet[31]=option;CHECK(!vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128),"actual C builder accepts options0 and1");
  CHECK(out[0]==3&&out[31]==option,"C relocation preserves ABI3/options scalar");
  for(unsigned i=0;i<8;i++)CHECK(out[4+i]==0x80800000u+offsets[i],"all eight actual C relocated addresses");
  CHECK(!mosaic_coop_v3_target_check(out),"actual expanded target validator accepts real C-built packet");
 }
 for(unsigned abi_value=1;abi_value<3;abi_value++){packet[0]=abi_value;CHECK(vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128)==-ERANGE,"C schema refuses old ABI");}packet[0]=3;
 packet[31]=2;CHECK(vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128)==-ERANGE,"C schema refuses unknown option bit");packet[31]=1;
 for(unsigned i=0;i<8;i++){
  job.refs[i].length--;CHECK(vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128)<0,"C role shorter than declared extent rejected");job.refs[i].length++;
  job.refs[i].object_generation++;CHECK(vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128)==-ESTALE,"actual C stale object generation rejected");job.refs[i].object_generation--;
 }
 packet[4]=0x80800000;CHECK(vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128)==-EINVAL,"supplied GPU address cannot bypass relocation");packet[4]=0;
 job.refs[3].offset=job.refs[4].offset=0x3040;
 CHECK(!vxl_v2_build_args(&ctx,&table.programs[0],&job,out,128),"generic C schema does not pretend to enforce target geometry/alias semantics");
 CHECK(mosaic_coop_v3_target_check(out)<0,"ABI3 target rejects actual C-built inactive RO in expanded tail");
 printf("MOSAIC_COOP_ABI3_PYTHON_SCHEMA_ACTUAL_C_CATALOG_RELOCATION_TARGET_BINDING_HOST_PASS checks=%u gpu_raw_cycles=null\n",checks);return 0;
}

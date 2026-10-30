#ifndef VXL_V2_PROGRAM_H
#define VXL_V2_PROGRAM_H
#include <vxl/v2/objects.h>
#define VXL_V2_CATALOG_MAGIC 0x434c5856u
#define VXL_V2_SCHEMA_MAGIC 0x534c5856u
#define VXL_V2_ABSENT 0xffffffffu
enum vxl_v2_field_kind {VXL_V2_SCALAR32=1,VXL_V2_ADDRESS32=2,VXL_V2_ZERO_RANGE=3,VXL_V2_PREDICATE32=4};
/* Private catalog schema2 adds generic bounded relations. Schema1 and all
 * existing UAPI structures/program identities remain supported unchanged. */
enum vxl_v2_predicate_op {VXL_V2_SUM_LE=1,VXL_V2_PAIR_SET=2,VXL_V2_SET3=3,VXL_V2_STRIDED_SET=4,VXL_V2_REFS_DISJOINT=5};
struct vxl_v2_schema_predicate {
 vxl_u32 kind,arg_offset,operation,rhs_offset,aux_offset,parameters[8],reserved[3];
};
struct vxl_v2_schema_header {
 vxl_u32 magic,version,bytes,args_bytes,field_count,code_gpu,rodata_gpu,crt_gpu,crt_bytes,stack_gpu,stack_bytes,provider_flags,reserved[4];
};
struct vxl_v2_schema_field {
 vxl_u32 kind,arg_offset,data_ref,access,alignment,element_bytes,count_offsets[3],relative_arg_offset,constant_bytes,min_value,max_value,reserved[3];
};
struct vxl_v2_provider {vxl_u32 flags;vxl_u8 hardware_contract[32],bit[32],xsa[32];};
struct vxl_v2_catalog {const vxl_u8 *bytes;vxl_u32 length,count;vxl_u8 sha[32];struct vxl_v2_provider provider;};
struct vxl_v2_program_ref {vxl_u64 handle,generation,device_generation;vxl_u8 entry_id[32];};
struct vxl_v2_program {
 struct vxl_v2_program_ref ref;struct vxl_v2_catalog_entry entry;
 struct vxl_v2_ref owned[5];vxl_u32 owned_count,pins,users;
};
struct vxl_v2_program_table {struct vxl_v2_catalog catalog;struct vxl_v2_program programs[16];vxl_u64 serial;};
struct vxl_v2_file_blob {vxl_u8 *bytes;vxl_u32 length,uid,mode,regular,no_follow;};
struct vxl_v2_file_ops {void *context;int (*read)(void *,const char *,vxl_u32,struct vxl_v2_file_blob *);void (*release)(void *,struct vxl_v2_file_blob *);};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_catalog_open(struct vxl_v2_catalog *,const void *,vxl_u32,const struct vxl_v2_provider *);
int vxl_v2_catalog_lookup(const struct vxl_v2_catalog *,const vxl_u8 id[32],struct vxl_v2_catalog_entry *);
int vxl_v2_schema_check(const void *,vxl_u32,vxl_u32 args_bytes);
int vxl_v2_table_bind(struct vxl_v2_ctx *,struct vxl_v2_program_table *,const struct vxl_v2_catalog *);
int vxl_v2_program_load(struct vxl_v2_ctx *,vxl_u64,const struct vxl_v2_catalog_entry *,const void *code,const void *rodata,struct vxl_v2_program_ref *);
int vxl_v2_program_release(struct vxl_v2_ctx *,vxl_u64,const struct vxl_v2_program_ref *);
int vxl_v2_build_args(struct vxl_v2_ctx *,const struct vxl_v2_program *,const struct vxl_v2_job *,void *,vxl_u32);
int vxl_v2_read_catalog_file(const struct vxl_v2_file_ops *,const struct vxl_v2_provider *,struct vxl_v2_catalog *,struct vxl_v2_file_blob *);
int vxl_v2_load_entry_file(struct vxl_v2_ctx *,vxl_u64 owner,const vxl_u8 entry_id[32],const struct vxl_v2_file_ops *,struct vxl_v2_program_ref *);
#ifdef __KERNEL__
extern const struct vxl_v2_file_ops vxl_v2_linux_files;
#endif
#ifdef __cplusplus
}
#endif
#endif

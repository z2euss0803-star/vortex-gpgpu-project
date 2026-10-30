#ifndef VXL_V2_OBJECTS_H
#define VXL_V2_OBJECTS_H
#include <vxl/core.h>
#include <vxl/v2/types.h>
#define VXL_V2_OBJECTS 64u
#define VXL_V2_CONSUMERS 5u
#define VXL_V2_BOUNCE_OFFSET 0xfe000u /* v2 only; private stack keeps 0xff000 */
enum vxl_v2_purpose {VXL_V2_CODE=1,VXL_V2_ARGS=2,VXL_V2_DATA=3,VXL_V2_READONLY=4};
struct vxl_v2_object {
 vxl_u64 handle,generation,owner,epoch,offset,length,allocated,bus;
 vxl_u32 gpu,domain,purpose,access,pins[5],sealed,guard_bytes;
 void *storage;
};
struct vxl_v2_memory_ops {
 int (*allocate)(void *,struct vxl_v2_object *);
 int (*free)(void *,struct vxl_v2_object *);
 int (*copy)(void *,struct vxl_v2_object *,vxl_u64,void *,vxl_u64,int);
};
struct vxl_v2_program_table;
struct vxl_v2_queue;
struct vxl_v2_ctx {
 vxl_u64 owner,epoch,serial,legacy_debt,lease_serial;
 vxl_u32 active,quarantined,closed,live,guard_bytes;
 struct vxl_profile profile;
 struct vxl_v2_object objects[64];
 const struct vxl_v2_memory_ops *ops;void *memory_context;
 struct vxl_v2_program_table *programs;
 struct vxl_v2_queue *queue;
};
struct vxl_engine;
struct vxl_ops;
struct vxl_v2_adapter {struct vxl_engine *legacy;const struct vxl_ops *transport;void *hardware;int (*lock_held)(void *);vxl_u64 legacy_debt;};
#ifdef __cplusplus
extern "C" {
#endif
void vxl_v2_init(struct vxl_v2_ctx *,const struct vxl_v2_memory_ops *,void *);
int vxl_v2_owner_enter(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 epoch);
int vxl_v2_owner_check(const struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 epoch);
int vxl_v2_owner_close(struct vxl_v2_ctx *,vxl_u64 owner,int stopped_known);
int vxl_v2_buffer_create(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 epoch,vxl_u32 domain,vxl_u32 purpose,vxl_u32 access,vxl_u64 length,struct vxl_v2_ref *);
int vxl_v2_buffer_release(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *);
int vxl_v2_buffer_batch(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 epoch,vxl_u32 domain,vxl_u64 length,vxl_u32 count,struct vxl_v2_ref *);
int vxl_v2_buffer_copy(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *,void *,int write);
int vxl_v2_buffer_pin(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *,vxl_u32 consumer);
int vxl_v2_buffer_unpin(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *,vxl_u32 consumer);
int vxl_v2_buffer_seal(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *);
int vxl_v2_ref_resolve(struct vxl_v2_ctx *,vxl_u64 owner,const struct vxl_v2_ref *,vxl_u32 access,struct vxl_v2_object **);
int vxl_v2_adapter_enter(struct vxl_v2_adapter *,struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 epoch);
extern const struct vxl_v2_memory_ops vxl_v2_adapter_memory_ops;
#ifdef __cplusplus
}
#endif
#endif

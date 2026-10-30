#ifndef VXL_ENGINE_H
#define VXL_ENGINE_H
#include <vxl/core.h>
struct vxl_object {
 vxl_u64 handle,offset,length,allocated,bus;
 vxl_u32 domain,purpose,gpu;
 void *storage;
};
struct vxl_engine;
struct vxl_hunet_ctx;
struct vxl_s8_ctx;
struct mosaic_ctx;
struct vxl_profile_factory;
struct vxl_prepared {
 vxl_u32 program_id,code_gpu,args_gpu,args_word_count;
 vxl_u32 args_words[32];
};
/* Caller holds one device mutex. Transfer callback reads/writes only padded objects. */
struct vxl_ops {
 int (*alloc)(void *,struct vxl_object *);
 void (*free)(void *,struct vxl_object *);
 int (*transfer)(void *,struct vxl_object *,vxl_u64,void *,vxl_u64,int);
 int (*prepare)(void *,struct vxl_engine *,struct vxl_object *,vxl_u64,struct vxl_object *,vxl_u64,vxl_u32,struct vxl_prepared *);
 int (*start)(void *,const struct vxl_prepared *,vxl_u32 *);
 int (*poll)(void *,struct vxl_event *);
 int (*read_user)(void *,void *,vxl_u64,vxl_u64);
 int (*write_user)(void *,vxl_u64,const void *,vxl_u64);
 vxl_u64 (*now)(void *);
};
struct vxl_engine {
 struct vxl_profile profile;
 struct vxl_pool pools[2];
 struct vxl_object objects[VXL_MAX_OBJECTS];
 struct vxl_job job;
 struct vxl_event snapshot;
 struct vxl_prepared prepared;
 const struct vxl_ops *ops;
 void *context;
 struct vxl_hunet_ctx *hunet;
 struct vxl_s8_ctx *s8;
 struct mosaic_ctx *mosaic;
 struct vxl_profile_factory *profile_factory;
 vxl_u64 serial,next_job,owner;
 vxl_u32 generation,opened,quarantined,profile_id;
 vxl_u8 scratch[4096];
};
#ifdef __cplusplus
extern "C" {
#endif
void vxl_engine_init(struct vxl_engine *,const struct vxl_ops *,void *,vxl_u32 generation);
int vxl_engine_init_profile(struct vxl_engine *,const struct vxl_ops *,void *,vxl_u32 generation,vxl_u32 profile_id);
int vxl_engine_open(struct vxl_engine *,vxl_u64 owner);
int vxl_engine_call(struct vxl_engine *,vxl_u64 owner,vxl_u32 cmd,struct vxl_request *);
int vxl_engine_poll(struct vxl_engine *);
void vxl_engine_close(struct vxl_engine *,vxl_u64 owner);
int vxl_diagnostic_args(struct vxl_engine *,const vxl_u32 addresses[3]);
int vxl_controlled_prepare(struct vxl_engine *,struct vxl_object *,vxl_u64,struct vxl_object *,vxl_u64,vxl_u32,struct vxl_prepared *);
#ifdef __cplusplus
}
#endif
#endif

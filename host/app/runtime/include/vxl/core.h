#ifndef VXL_CORE_H
#define VXL_CORE_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
typedef u64 vxl_u64;
typedef u32 vxl_u32;
typedef u8 vxl_u8;
#else
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint64_t vxl_u64;
typedef uint32_t vxl_u32;
typedef uint8_t vxl_u8;
#endif
#include <uapi/vx_linux.h>
#define VXL_MAX_OBJECTS 64
#define VXL_PS_BASE 0x10000000ULL
#define VXL_PS_SIZE 0x100000ULL
#define VXL_FENCE 0x10030000ULL
#define VXL_PL_BASE 0x500000000ULL
#define VXL_PL_SIZE 0x80000000ULL
struct vxl_profile { vxl_u64 ps_base, ps_size, pl_base, pl_size; };
struct vxl_extent { vxl_u64 offset, size; };
struct vxl_pool { vxl_u64 capacity; struct vxl_extent ext[VXL_MAX_OBJECTS+1]; };
enum vxl_state { VXL_READY=1, VXL_RUNNING=2, VXL_COMPLETED=3, VXL_FAILED=4, VXL_QUARANTINED=5 };
enum vxl_event_kind { VXL_START=1, VXL_DONE, VXL_WAIT_TIMEOUT, VXL_DEADLINE, VXL_ERROR, VXL_CLOSE, VXL_QUIESCED };
struct vxl_job { vxl_u64 id, deadline; vxl_u32 state, pinned, closed, jobs_before; int result; };
struct vxl_event { vxl_u32 kind, status, errors, pending_w, pending_r, jobs, idle, ready; vxl_u64 id; };
#ifdef __cplusplus
extern "C" {
#endif
int vxl_range_check(vxl_u64 capacity, vxl_u64 offset, vxl_u64 length);
int vxl_addr_encode(const struct vxl_profile *, vxl_u32 domain, vxl_u64 bus, vxl_u64 length, vxl_u32 *gpu);
int vxl_pool_alloc(struct vxl_pool *, vxl_u64 size, vxl_u64 alignment, vxl_u64 *offset);
int vxl_pool_reserve(struct vxl_pool *, vxl_u64 offset, vxl_u64 size);
int vxl_pool_free(struct vxl_pool *, vxl_u64 offset, vxl_u64 size);
int vxl_job_step(struct vxl_job *, const struct vxl_event *);
int vxl_uapi_validate(vxl_u32 command, const struct vxl_request *, vxl_u32 size, int compat);
int vxl_cdma_status(vxl_u32 status);
int vxl_cdma_addresses(vxl_u64 src, vxl_u64 dst, vxl_u64 size);
int vxl_probe_check(int resources, int clocks, int reset, int ready, vxl_u32 id, vxl_u32 config, int other_owner);
int vxl_program_check(const vxl_u8 *bytes,vxl_u64 size,vxl_u32 gpu);
int vxl_program_check_id(const vxl_u8 *bytes,vxl_u64 size,vxl_u32 gpu,vxl_u32 id);
struct vxl_engine;
int vxl_cnn_args_check(struct vxl_engine *,vxl_u32 id,const vxl_u32 words[16]);
struct vxl_cdma_io {
 void *context;
 vxl_u32 (*read)(void *,vxl_u32);
 void (*write)(void *,vxl_u32,vxl_u32);
 vxl_u64 (*now)(void *);
 void (*pause)(void *);
 vxl_u32 last_status;
};
int vxl_cdma_run(struct vxl_cdma_io *,vxl_u64 src,vxl_u64 dst,vxl_u32 length,vxl_u64 timeout_ms);
#ifdef __cplusplus
}
#endif
#endif

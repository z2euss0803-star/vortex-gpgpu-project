/* SPDX-License-Identifier: MIT */
#ifndef VX_COMPUTE_V2_H
#define VX_COMPUTE_V2_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define VXL_V2_MAJOR 2u
#define VXL_V2_MAX_REFS 8u
#define VXL_V2_READ 1u
#define VXL_V2_ACK 2u
/* Additive opt-in; zero job flags retain the original strict contract. */
#define VXL_V2_FAST_RUNTIME_API 1u
#define VXL_V2_CAP_FAST_RUNTIME 1u
#define VXL_V2_JOB_FAST_RUNTIME 1u
#define VXL_V2_COMPLETION_FAST_VERSION 4u
#define VXL_V2_COMPLETION_FAST_UNCHECKED 1u
#define VXL_V2_FAST_POLICY_VERSION 1u
#define VXL_V2_FAST_OMITTED_CHECKS 3u /* source SHA + device args content */
#define VXL_V2_ID_BOOT_MANIFEST_DECLARED 1u
#define VXL_V2_FEATURE_NATIVE_INT16 1u
#define VXL_V2_FEATURE_P8_GRAPHICS 2u
struct vxl_compute_v2_request {
 __u32 major, size, flags, reserved;
 __aligned_u64 device_generation, handle, object_generation, offset, length;
 __aligned_u64 user_ptr, cookie, sequence, payload_bytes;
 __u32 state; __s32 result; __u32 reserved_tail[8];
};
struct vxl_v2_ref {
 __aligned_u64 handle, object_generation, offset, length;
 __u32 access, domain; __aligned_u64 device_generation;
};
struct vxl_v2_create_desc {
 __u32 domain,purpose,access,flags;__aligned_u64 bytes;__u32 reserved[10];
};
struct vxl_v2_buffer_info {
 struct vxl_v2_ref ref;__u32 purpose,sealed;__aligned_u64 allocated;
};
struct vxl_v2_copy_desc {
 struct vxl_v2_ref source,destination;__aligned_u64 user_ptr,bytes;
 __u32 direction,reserved[3]; /* 1 CPU->device, 2 device->CPU, 3 device->device */
};
struct vxl_v2_program_desc {
 __u8 entry_id[32];__aligned_u64 handle,object_generation,device_generation;__u32 reserved[2];
};
struct vxl_v2_job {
 __u32 major, size, flags, reserved0;
 __aligned_u64 cookie, program_handle, program_generation, device_generation;
 __aligned_u64 execution_budget_ns, args_handle, args_generation, args_offset, args_bytes;
 __u32 num_refs, reserved1, reserved[8];
 struct vxl_v2_ref refs[8];
};
struct vxl_v2_completion {
 __u32 version, bytes, state; __s32 result;
 __aligned_u64 cookie, sequence, device_generation, program_handle, program_generation;
 __u32 status, errors, pending_w, pending_r, jobs, core_mask, elapsed_raw, watchdog, cycles_valid, reserved0;
 __aligned_u64 submit_ns, start_ns, done_seen_ns, ack_ns, complete_ns;
 __aligned_u64 cdma_ns, guard_ns, digest_ns, poll_ns, handoff_ns, io_bytes;
 __u8 parameter_sha[32], source_id[32], reserved[8];
};
struct vxl_v2_caps {
 __u32 version, bytes, flags, reserved;
 __aligned_u64 device_generation;
 __u8 hardware_contract[32], bit[32], xsa[32], compatible[32];
 __aligned_u64 ps_base, ps_bytes, pl_base, pl_bytes;
 __u32 max_objects, max_pending, max_completions, max_refs, max_args, max_programs, cycle_bits, contexts;
 __u32 feature_mask, identity_source, reserved_tail[8];
};
struct vxl_v2_catalog_header {
 __u32 magic, version, total_bytes, count;
 __u8 payload_sha[32]; __u32 entry_bytes, reserved[3];
};
struct vxl_v2_catalog_entry {
 __u32 namespace_id, logical_profile, logical_program, flags;
 __u32 code_offset, code_bytes, rodata_offset, rodata_bytes;
 __u32 entry_pc, args_gpu, args_bytes, schema_offset, schema_bytes, reloc_count, provider_flags, reserved0;
 __u8 code[32], rodata[32], schema[32], hardware_contract[32], bit[32], xsa[32], entry_id[32], raw_source[32];
 __u8 reserved[192];
};
#define VXL_V2_CAPS _IOWR('V',0x80,struct vxl_compute_v2_request)
#define VXL_V2_BUFFER_CREATE _IOWR('V',0x81,struct vxl_compute_v2_request)
#define VXL_V2_BUFFER_INFO _IOWR('V',0x82,struct vxl_compute_v2_request)
#define VXL_V2_BUFFER_COPY _IOWR('V',0x83,struct vxl_compute_v2_request)
#define VXL_V2_BUFFER_RELEASE _IOWR('V',0x84,struct vxl_compute_v2_request)
#define VXL_V2_PROGRAM_LOAD _IOWR('V',0x85,struct vxl_compute_v2_request)
#define VXL_V2_PROGRAM_INFO _IOWR('V',0x86,struct vxl_compute_v2_request)
#define VXL_V2_PROGRAM_RELEASE _IOWR('V',0x87,struct vxl_compute_v2_request)
#define VXL_V2_SUBMIT_BATCH _IOWR('V',0x88,struct vxl_compute_v2_request)
#define VXL_V2_WAIT _IOWR('V',0x89,struct vxl_compute_v2_request)
#define VXL_V2_COMPLETE_REAP _IOWR('V',0x8a,struct vxl_compute_v2_request)
#define VXL_V2_FAULT_INFO _IOWR('V',0x8b,struct vxl_compute_v2_request)
#endif

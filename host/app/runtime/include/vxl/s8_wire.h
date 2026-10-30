/* Private, fixed little-endian S8 payload; does not extend old UAPI. */
#ifndef VXL_S8_WIRE_H
#define VXL_S8_WIRE_H
typedef __UINT32_TYPE__ s8_u32;
typedef __UINT8_TYPE__ s8_u8;
#define VXL_S8_PROFILE 5u
#define VXL_S8_PROGRAM 11u
#define VXL_S8_JOB_MAGIC 0x314a3853u
#define VXL_S8_RESULT_MAGIC 0x31523853u
#define VXL_S8_BINDING_MAGIC 0x31423853u
#define VXL_S8_DONE 0x454e4f44u
#define VXL_S8_PHASE_TIMING 1u
#define VXL_S8_SPIN_LIMIT 65536u
#define VXL_S8_DEADLINE_MS 60000u
#define VXL_S8_WATCHDOG 4000000000u
struct vxl_s8_job_v1 {
 s8_u32 magic,version,bytes,case_id,flags,k,core_mask,owner_warp;
 s8_u32 a_base,b_base,input_slot_stride,output_base,output_stride;
 s8_u32 identity_base,control_base,spin_limit,generation,sequence;
 s8_u8 run_id[16],binding_tag[16];
 s8_u32 args_crc32,reserved[5];
};
struct vxl_s8_result_v1 {
 s8_u32 magic,version,bytes,case_id,sw_status,generation,sequence;
 s8_u32 core,warp,owner_warp,k,flags,valid_bits,pages_count,status_count,phase_count;
 s8_u8 run_id[16],binding_tag[16];
 s8_u32 format,primary_elements,primary_accepted,primary_bitmap;
 s8_u32 firsterror_event,detail,done,recovery_elements,reserved[32];
};
struct vxl_s8_binding_v1 {
 s8_u32 magic,version,bytes,profile,program,abi_major,features,reserved0;
 s8_u8 digests[14][32],reserved[32];
};
struct vxl_s8_status_v1 { s8_u32 event_id,index,lane[4],valid_mask,reserved0; };
struct vxl_s8_identity_v1 { s8_u32 core,warp,lane,hart,sp,tp,stack_tag,tls_tag; };
struct vxl_s8_phase_event_v1 { s8_u32 event,flags,raw_lo,raw_hi; };
struct vxl_s8_phase_v1 {
 s8_u32 magic,version,bytes,valid_mask,core,warp,width,count,reserved0[8];
 struct vxl_s8_phase_event_v1 events[12];
 s8_u8 reserved[256];
};
#ifdef __cplusplus
#define S8_ASSERT(c) static_assert(c,#c)
#else
#define S8_ASSERT(c) _Static_assert(c,#c)
#endif
S8_ASSERT(sizeof(struct vxl_s8_job_v1)==128);
S8_ASSERT(sizeof(struct vxl_s8_result_v1)==256);
S8_ASSERT(sizeof(struct vxl_s8_binding_v1)==512);
S8_ASSERT(sizeof(struct vxl_s8_status_v1)==32);
S8_ASSERT(sizeof(struct vxl_s8_identity_v1)==32);
S8_ASSERT(sizeof(struct vxl_s8_phase_v1)==512);
#endif

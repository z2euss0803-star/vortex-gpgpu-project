#ifndef VXL_CNN_H
#define VXL_CNN_H
#include <vxl/core.h>
#define VXL_CNN_MAGIC 0x434e4e31U
#define VXL_CNN_VERSION 1U
#define VXL_CNN_CONV 2U
#define VXL_CNN_RELU 3U
#define VXL_CNN_POOL 4U
#define VXL_CNN_FC 5U
#define VXL_CNN_CODE_GPU 0x10000000U
#define VXL_CNN_CODE_BYTES 4096U
#define VXL_CNN_ARGS_GPU 0x10010000U
#define VXL_CNN_ARGS_BYTES 64U
#define VXL_CNN_GUARD_BYTES 64U
#define VXL_CNN_LEFT 0x55aa55aaU
#define VXL_CNN_RIGHT 0xaa55aa55U
#define VXL_CNN_PADDING 0xc0def00dU
#define VXL_CNN_POISON 0xdeadbeefU
struct vxl_cnn_args {
 vxl_u32 magic,byte_size,version,program_id;
 vxl_u32 input,weight,bias,output;
 vxl_u32 input_count,weight_count,bias_count,output_count;
 vxl_u32 reserved[4];
};
typedef char vxl_cnn_args_exact_64[(sizeof(struct vxl_cnn_args)==64)?1:-1];
#endif

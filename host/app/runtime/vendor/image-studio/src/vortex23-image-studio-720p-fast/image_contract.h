#ifndef IMAGE_CONTRACT_H
#define IMAGE_CONTRACT_H
#include <stdint.h>
#define IMAGE_SLOT_COUNT UINT32_C(0x00000021)
#define IMAGE_SMOOTH UINT32_C(0x27400040)
#define IMAGE_CORE UINT32_C(0x27800040)
#define IMAGE_HALO UINT32_C(0x27c00040)
#define IMAGE_MEAN UINT32_C(0x28000040)
#define IMAGE_WIDTH UINT32_C(0x00000500)
#define IMAGE_HEIGHT UINT32_C(0x000002d0)
#define IMAGE_BYTES UINT32_C(0x00384000)
#define IMAGE_SLOT_BASE UINT32_C(0x20000000)
#define IMAGE_SLOT_SIZE UINT32_C(0x00400000)
#define IMAGE_ACTIVE UINT32_C(0x20000040)
#define IMAGE_STAGE UINT32_C(0x20400040)
#define IMAGE_TABLE UINT32_C(0x22800040)
#define IMAGE_PAL_Y UINT32_C(0x22800040)
#define IMAGE_PAL_8 UINT32_C(0x22801c40)
#define IMAGE_BAYER UINT32_C(0x22801d40)
#define IMAGE_GAUSS7 UINT32_C(0x22801d80)
#define IMAGE_GAUSS9 UINT32_C(0x22801dc0)
#define IMAGE_XDOG_TABLE UINT32_C(0x22802040)
#define IMAGE_THEME UINT32_C(0x22801e40)
#define IMAGE_BG UINT32_C(0xff1e1007)
typedef struct {uint32_t x,y,w,h;} image_rect;
typedef struct {uint32_t style,preset,block;} image_params;
typedef struct {uint32_t src0,src1,src2,dst,width,height,rect_x,rect_y,rect_w,rect_h,table,param0,param1,param2,param3,flags;} image_job;
enum image_op {IMAGE_GRAY=0,IMAGE_BLUR3=1,IMAGE_SOBEL_POSTER=2,IMAGE_GAUSSIAN_H=3,IMAGE_GAUSSIAN_V=4,IMAGE_XDOG_COLOR=5,IMAGE_NEON_CORE=6,IMAGE_NEON_COMBINE=7,IMAGE_PIXEL_MEAN=8,IMAGE_PIXEL_BAYER=9};
#define IMAGE_GRAY_Q8 1u
_Static_assert(sizeof(image_job)==64,"image job ABI");
#endif

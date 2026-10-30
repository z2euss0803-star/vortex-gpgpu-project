#ifndef VXL_MOSAIC_PRIVATE_TYPES_H
#define VXL_MOSAIC_PRIVATE_TYPES_H
/* Private wire types shared by Linux and RV32 bare metal; no ioctl dependency. */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
typedef u64 mosaic_u64;typedef u32 mosaic_u32;typedef u8 mosaic_u8;
#else
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
typedef uint64_t mosaic_u64;typedef uint32_t mosaic_u32;typedef uint8_t mosaic_u8;
#endif
#endif

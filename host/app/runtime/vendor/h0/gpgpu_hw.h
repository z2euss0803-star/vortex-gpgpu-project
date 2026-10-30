#ifndef GPGPU_HW_H
#define GPGPU_HW_H

#include <stdint.h>

#define GPU_BASE UINT64_C(0xA0000000)
#define CDMA_BASE UINT64_C(0xB0010000)
#define READY_GPIO_BASE UINT64_C(0xB0020000)

#define GPU_ID 0x00u
#define GPU_CONFIG 0x04u
#define GPU_COMMAND 0x08u
#define GPU_STATUS 0x0cu
#define GPU_PC 0x10u
#define GPU_ARG 0x14u
#define GPU_WATCHDOG 0x18u
#define GPU_ERRORS 0x1cu
#define GPU_PENDING_WRITES 0x20u
#define GPU_PENDING_READS 0x24u
#define GPU_ELAPSED 0x28u
#define GPU_JOBS 0x2cu
#define GPU_FEATURES 0x30u
#define GPU_MEMORY_READY 0x34u
#define GPU_CAPABILITY 0x38u
#define GPU_TEX_MODE 0x50u

#define GPU_STATUS_ACTIVE 0x01u
#define GPU_STATUS_DONE 0x02u
#define GPU_STATUS_ERROR 0x04u
#define GPU_STATUS_STATE_SHIFT 8u
#define GPU_STATE_FAULT 6u

#define CDMA_CR 0x00u
#define CDMA_SR 0x04u
#define CDMA_SA 0x18u
#define CDMA_SA_MSB 0x1cu
#define CDMA_DA 0x20u
#define CDMA_DA_MSB 0x24u
#define CDMA_BTT 0x28u
#define CDMA_SR_IDLE 0x02u
/* Xilinx Vitis 2020.1 xaxicdma_hw.h: SR_ERR_ALL=0x770,
 * IRQ_IOC=0x1000, IRQ_DELAY=0x2000, IRQ_ERROR=0x4000. */
#define CDMA_SR_ERROR_MASK 0x00004770u
#define CDMA_SR_IOC 0x00001000u
#define CDMA_SR_DELAY 0x00002000u
#define CDMA_SR_W1C_NORMAL (CDMA_SR_IOC | CDMA_SR_DELAY)

static inline int cdma_status_error(uint32_t status) {
    return (status & CDMA_SR_ERROR_MASK) != 0u;
}

static inline int cdma_status_complete(uint32_t status) {
    return !cdma_status_error(status) &&
           (status & (CDMA_SR_IDLE | CDMA_SR_IOC)) ==
               (CDMA_SR_IDLE | CDMA_SR_IOC);
}

#define PS_CODE UINT64_C(0x10000000)
#define PS_ARGS UINT64_C(0x10010000)
#define PS_FLUSH_LINE UINT64_C(0x10030000)
#define PS_STAGE UINT64_C(0x10020000)
#define PS_STAGE2 UINT64_C(0x10021000)
#define PS_GPU_A UINT32_C(0x10022000)
#define PS_GPU_B UINT32_C(0x10022100)
#define PS_GPU_C UINT32_C(0x10022240)

#define PL_BASE UINT64_C(0x500000000)
#define PL_GPU_A UINT32_C(0x80020000)
#define PL_GPU_B UINT32_C(0x80020100)
#define PL_GPU_C UINT32_C(0x80020240)
#define PL_HIGH_BANK UINT64_C(0x580020000)
#define PL_LAST_LINE UINT64_C(0x5FFFFFFC0)

#define VEC_COUNT 64u
#define CACHE_LINE 64u

#endif

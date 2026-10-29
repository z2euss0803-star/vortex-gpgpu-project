#ifndef MOSAIC_DEVICE_H
#define MOSAIC_DEVICE_H

#include <stdint.h>
#include "mosaic_job.h"

/* Callbacks are supplied by the standalone BSP or a strict host mock. */
typedef struct {
    uint32_t (*read32)(void *opaque, uint64_t address);
    void (*write32)(void *opaque, uint64_t address, uint32_t value);
    void (*stage_job)(void *opaque, uint64_t address, const MosaicJob *job);
    void (*flush)(void *opaque, uint64_t address, uint32_t bytes);
    void (*barrier)(void *opaque);
    uint64_t (*now_us)(void *opaque);
    void *opaque;
} MosaicDeviceIo;

typedef struct {
    uint32_t status, errors, pending_writes, pending_reads, elapsed;
    uint32_t jobs_before, jobs_after, cdma_status;
    uint64_t started_us, ended_us;
} MosaicDeviceSnapshot;

enum {
    MOSAIC_DEVICE_OK = 0,
    MOSAIC_DEVICE_BAD_ARGUMENT = -1,
    MOSAIC_DEVICE_DMA_PRIOR_ERROR = -2,
    MOSAIC_DEVICE_DMA_STALE_IOC = -3,
    MOSAIC_DEVICE_DMA_ERROR = -4,
    MOSAIC_DEVICE_TIMEOUT = -5,
    MOSAIC_DEVICE_CLOCK = -6,
    MOSAIC_DEVICE_NOT_IDLE = -7,
    MOSAIC_DEVICE_NOT_READY = -8,
    MOSAIC_DEVICE_GPU_FAULT = -9,
    MOSAIC_DEVICE_GPU_INCOMPLETE = -10,
    MOSAIC_DEVICE_GPU_ACK = -11
};

uint64_t mosaic_gpu_bus_address(uint32_t gpu_address);
int mosaic_cdma_copy(const MosaicDeviceIo *io, uint64_t source, uint64_t dest,
                     uint32_t bytes, uint64_t timeout_us,
                     MosaicDeviceSnapshot *snapshot);
int mosaic_cdma_copy_all(const MosaicDeviceIo *io, uint64_t source, uint64_t dest,
                         uint32_t bytes, uint64_t timeout_per_chunk_us,
                         MosaicDeviceSnapshot *snapshot);
int mosaic_submit(const MosaicDeviceIo *io, const MosaicJob *job,
                  uint32_t watchdog_cycles, uint64_t host_timeout_us,
                  MosaicDeviceSnapshot *snapshot);

#endif

#include "mosaic_device.h"
#include "mosaic_addresses.h"

#include <stddef.h>
#include <string.h>

/* Offsets and status masks match the frozen R4 gpgpu_hw.h and controller RTL. */
#define GPU_BASE UINT64_C(0xA0000000)
#define GPIO_READY UINT64_C(0xB0020000)
#define CDMA_BASE UINT64_C(0xB0010000)
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
#define GPU_MEMORY_READY 0x34u
#define GPU_TEX_MODE 0x50u
#define CDMA_CR 0x00u
#define CDMA_SR 0x04u
#define CDMA_SA 0x18u
#define CDMA_SA_MSB 0x1cu
#define CDMA_DA 0x20u
#define CDMA_DA_MSB 0x24u
#define CDMA_BTT 0x28u
#define DMA_ERROR_MASK 0x00004770u
#define DMA_IDLE 0x02u
#define DMA_IOC 0x1000u
#define DMA_NORMAL_W1C 0x3000u
#define GPU_DONE 0x02u
#define GPU_ERROR 0x04u
#define GPU_FAULT_STATE 6u

static int ready(const MosaicDeviceIo *io) {
    return io && io->read32 && io->write32 && io->stage_job &&
           io->flush && io->barrier && io->now_us;
}
static uint32_t rd(const MosaicDeviceIo *io, uint64_t base, uint32_t offset) {
    return io->read32(io->opaque, base + offset);
}
static void wr(const MosaicDeviceIo *io, uint64_t base, uint32_t offset, uint32_t value) {
    io->write32(io->opaque, base + offset, value);
    io->barrier(io->opaque);
}
static int elapsed(const MosaicDeviceIo *io, uint64_t start, uint64_t limit) {
    uint64_t now = io->now_us(io->opaque);
    if (now < start) return MOSAIC_DEVICE_CLOCK;
    return now - start >= limit ? MOSAIC_DEVICE_TIMEOUT : 0;
}
static int in_window(uint64_t address, uint32_t bytes) {
    uint64_t end = address + bytes;
    if (!bytes || end < address) return 0;
    return (address >= UINT64_C(0x10000000) && end <= UINT64_C(0x30000000)) ||
           (address >= UINT64_C(0x500000000) && end <= UINT64_C(0x580000000));
}
static void dma_snapshot(const MosaicDeviceIo *io, MosaicDeviceSnapshot *s) {
    s->cdma_status = rd(io, CDMA_BASE, CDMA_SR);
    s->ended_us = io->now_us(io->opaque);
}
static void gpu_snapshot(const MosaicDeviceIo *io, MosaicDeviceSnapshot *s) {
    s->status = rd(io, GPU_BASE, GPU_STATUS);
    s->errors = rd(io, GPU_BASE, GPU_ERRORS);
    s->pending_writes = rd(io, GPU_BASE, GPU_PENDING_WRITES);
    s->pending_reads = rd(io, GPU_BASE, GPU_PENDING_READS);
    s->elapsed = rd(io, GPU_BASE, GPU_ELAPSED);
    s->jobs_after = rd(io, GPU_BASE, GPU_JOBS);
    s->cdma_status = rd(io, CDMA_BASE, CDMA_SR);
    s->ended_us = io->now_us(io->opaque);
}

uint64_t mosaic_gpu_bus_address(uint32_t gpu_address) {
    if (gpu_address < UINT32_C(0x80000000)) return 0;
    return UINT64_C(0x500000000) + (gpu_address - UINT32_C(0x80000000));
}

int mosaic_cdma_copy(const MosaicDeviceIo *io, uint64_t source, uint64_t dest,
                     uint32_t bytes, uint64_t timeout_us,
                     MosaicDeviceSnapshot *snapshot) {
    uint32_t status;
    int result;
    if (!ready(io) || !snapshot || !timeout_us || bytes > 4096 ||
        (source | dest | bytes) & 15 ||
        !in_window(source, bytes) || !in_window(dest, bytes))
        return MOSAIC_DEVICE_BAD_ARGUMENT;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->started_us = io->now_us(io->opaque);
    for (;;) {
        status = rd(io, CDMA_BASE, CDMA_SR);
        if (status & DMA_ERROR_MASK) { result = MOSAIC_DEVICE_DMA_PRIOR_ERROR; goto failed; }
        if (status & DMA_IDLE) break;
        result = elapsed(io, snapshot->started_us, timeout_us);
        if (result) goto failed;
    }
    wr(io, CDMA_BASE, CDMA_CR, 0);
    /* Old IOC is W1C; it must not satisfy the next BTT. */
    wr(io, CDMA_BASE, CDMA_SR, DMA_NORMAL_W1C);
    status = rd(io, CDMA_BASE, CDMA_SR);
    if (status & DMA_ERROR_MASK) { result = MOSAIC_DEVICE_DMA_ERROR; goto failed; }
    if (status & DMA_NORMAL_W1C) { result = MOSAIC_DEVICE_DMA_STALE_IOC; goto failed; }
    wr(io, CDMA_BASE, CDMA_SA_MSB, (uint32_t)(source >> 32));
    wr(io, CDMA_BASE, CDMA_SA, (uint32_t)source);
    wr(io, CDMA_BASE, CDMA_DA_MSB, (uint32_t)(dest >> 32));
    wr(io, CDMA_BASE, CDMA_DA, (uint32_t)dest);
    wr(io, CDMA_BASE, CDMA_BTT, bytes);
    for (;;) {
        status = rd(io, CDMA_BASE, CDMA_SR);
        if (status & DMA_ERROR_MASK) { result = MOSAIC_DEVICE_DMA_ERROR; goto failed; }
        if ((status & (DMA_IDLE | DMA_IOC)) == (DMA_IDLE | DMA_IOC)) {
            dma_snapshot(io, snapshot);
            return 0;
        }
        result = elapsed(io, snapshot->started_us, timeout_us);
        if (result) goto failed;
    }
failed:
    dma_snapshot(io, snapshot);
    return result;
}

int mosaic_cdma_copy_all(const MosaicDeviceIo *io, uint64_t source, uint64_t dest,
                         uint32_t bytes, uint64_t timeout_per_chunk_us,
                         MosaicDeviceSnapshot *snapshot) {
    uint32_t offset;
    int rc;
    if (!bytes || bytes & 15 || !in_window(source, bytes) ||
        !in_window(dest, bytes)) return MOSAIC_DEVICE_BAD_ARGUMENT;
    for (offset = 0; offset < bytes;) {
        uint32_t count = bytes - offset > 4096 ? 4096 : bytes - offset;
        rc = mosaic_cdma_copy(io, source + offset, dest + offset, count,
                              timeout_per_chunk_us, snapshot);
        if (rc) return rc;
        offset += count;
    }
    return 0;
}

static int valid_job_pointers(const MosaicJob *job) {
    const uint64_t *p = &job->x;
    unsigned i;
    for (i = 0; i < 8; ++i)
        if (p[i] < UINT64_C(0x80000000) || p[i] > UINT32_MAX) return 0;
    return 1;
}

int mosaic_submit(const MosaicDeviceIo *io, const MosaicJob *job,
                  uint32_t watchdog_cycles, uint64_t host_timeout_us,
                  MosaicDeviceSnapshot *snapshot) {
    int result;
    if (!ready(io) || !job || !snapshot || !valid_job_pointers(job) ||
        !job->tasks || !watchdog_cycles || !host_timeout_us) return MOSAIC_DEVICE_BAD_ARGUMENT;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->started_us = io->now_us(io->opaque);
    if (rd(io, GPU_BASE, GPU_STATUS) != 0) { result = MOSAIC_DEVICE_NOT_IDLE; goto failed; }
    if (!(rd(io, GPU_BASE, GPU_MEMORY_READY) & 1) ||
        !(rd(io, GPIO_READY, 0) & 1)) {
        result = MOSAIC_DEVICE_NOT_READY; goto failed;
    }
    snapshot->jobs_before = rd(io, GPU_BASE, GPU_JOBS);
    io->stage_job(io->opaque, MOSAIC_PS_JOB_PAYLOAD, job);
    io->flush(io->opaque, MOSAIC_PS_JOB_PAYLOAD, sizeof(*job));
    io->barrier(io->opaque);
    wr(io, GPU_BASE, GPU_PC, MOSAIC_GPU_CODE);
    wr(io, GPU_BASE, GPU_ARG, (uint32_t)MOSAIC_PS_JOB_PAYLOAD);
    wr(io, GPU_BASE, GPU_WATCHDOG, watchdog_cycles);
    wr(io, GPU_BASE, GPU_TEX_MODE, 0);
    wr(io, GPU_BASE, GPU_COMMAND, 1);
    for (;;) {
        uint32_t status = rd(io, GPU_BASE, GPU_STATUS);
        if ((status & GPU_ERROR) || ((status >> 8) & 7) == GPU_FAULT_STATE) {
            result = MOSAIC_DEVICE_GPU_FAULT; goto failed;
        }
        if (status & GPU_DONE) break;
        result = elapsed(io, snapshot->started_us, host_timeout_us);
        if (result) goto failed;
    }
    gpu_snapshot(io, snapshot);
    if (!(snapshot->status & GPU_DONE) || snapshot->errors ||
        snapshot->pending_writes || snapshot->pending_reads ||
        snapshot->jobs_after != snapshot->jobs_before + 1u) {
        return MOSAIC_DEVICE_GPU_INCOMPLETE;
    }
    wr(io, GPU_BASE, GPU_COMMAND, 2);
    if (rd(io, GPU_BASE, GPU_STATUS) != 0) {
        gpu_snapshot(io, snapshot);
        return MOSAIC_DEVICE_GPU_ACK;
    }
    return 0;
failed:
    gpu_snapshot(io, snapshot);
    return result;
}

#ifndef MOSAIC_JOB_H
#define MOSAIC_JOB_H

#include <stddef.h>
#include <stdint.h>

enum { MOSAIC_PACK = 0, MOSAIC_GEMM = 1, MOSAIC_NORM = 2, MOSAIC_COPY = 3 };

/* Exact little-endian GPU kernel ABI from src/operators.h. */
typedef struct {
    uint64_t x, w, b, g, beta, y, scratch, res;
    uint32_t ci, co, ih, iw, oh, ow, k, stride, up, start, count;
    uint32_t op, relu, residual, tasks, reserved;
} MosaicJob;

_Static_assert(sizeof(MosaicJob) == 128, "GPU Job ABI size");
_Static_assert(offsetof(MosaicJob, tasks) == 120, "GPU Job ABI tasks offset");

#endif

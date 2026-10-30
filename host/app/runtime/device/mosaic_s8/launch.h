#ifndef MOSAIC_OWNER_LAUNCH_H
#define MOSAIC_OWNER_LAUNCH_H
/* The SDK fills warps before cores. Four groups of sixteen scheduled lanes
 * reserve one full core per group; only warp0 owns the native PE on that core.
 * The other three warps must return BEFORE touching the owner abort/barriers. */
#define MOSAIC_LAUNCH_GRID 4u
#define MOSAIC_LAUNCH_BLOCK 16u
static inline int mosaic_owner_warp(unsigned warp){return warp==0;}
#endif

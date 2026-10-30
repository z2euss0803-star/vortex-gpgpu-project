#ifndef MOSAIC_NORM_COMPAT_LUT_V2_H
#define MOSAIC_NORM_COMPAT_LUT_V2_H
// Immutable, model-independent primitive data. No golden layer statistics.
// The optional provider is built separately; no data enter the default graph.
extern "C" {
extern const unsigned char mosaic_norm_rcp_count_v2[16384];
extern const unsigned char mosaic_norm_rsqrt_delta_v2[6291456];
}
#endif

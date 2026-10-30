#ifndef VXL_MOSAIC_ASSETS_H
#define VXL_MOSAIC_ASSETS_H
#include <vxl/core.h>
#define MOSAIC_ASSET_HEADER 256u
#define MOSAIC_ASSET_LAYER 384u
#define MOSAIC_ASSET_LAYERS 16u
#define MOSAIC_ASSET_PAYLOAD 6400u
#define MOSAIC_ASSET_MAX (16u*1024u*1024u)
struct mosaic_layer_shape {vxl_u32 co,ci,k,norm;};
/* Trusted catalog object. A caller must not accept a manifest supplied by the
 * same untrusted party as the blob. This is not the public176B request UAPI. */
struct mosaic_manifest {
 vxl_u8 package_sha[32],source_sha[4][32];
 struct mosaic_layer_shape layers[MOSAIC_ASSET_LAYERS];
};
#ifdef __cplusplus
extern "C" {
#endif
int mosaic_assets_validate(const void *,size_t,const struct mosaic_manifest *);
#ifdef __cplusplus
}
#endif
#endif

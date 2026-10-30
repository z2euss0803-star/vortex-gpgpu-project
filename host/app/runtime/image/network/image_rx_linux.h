#ifndef VX_IMAGE_RX_LINUX_H
#define VX_IMAGE_RX_LINUX_H
#include "image/pipeline_linux.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/network/image_rx.h"
#define VX_IMAGE_UDP_PORT 5011u
#define VX_IMAGE_UDP_WINDOW 8u
/* Internal sole-service API, never a wire/UAPI receipt supplied by a client.
 * U20 charges stage+active buffers for the entire receiver lifetime. */
typedef struct vx_image_receiver vx_image_receiver;
typedef struct {image_request wire;const uint8_t *rgba;uint32_t bytes,peer_ip;uint16_t peer_port;uint64_t serial,display_cookie;} vx_image_ingress;
typedef struct {uint64_t serial,display_cookie,total_ns;image_result output;uint32_t jobs_before,jobs_after,scope,cycles_valid,execution_complete;} vx_image_rx_completion;
typedef struct {uint64_t cookie,had_old_front,submitted_ns,presented_ns,old_retired_ns;uint32_t scope,clock_domain,presented,old_retired;} vx_image_rx_display;
int vx_image_receiver_create(uint32_t session,const char*bundle,uint64_t display_base,int allow_host,vx_image_receiver**);
int vx_image_receiver_handle(vx_image_receiver*,const uint8_t*,size_t,uint32_t peer,uint16_t port,uint64_t now_ms,uint8_t*,size_t,size_t*);
int vx_image_receiver_poll(vx_image_receiver*,uint64_t now_ms);
int vx_image_receiver_acquire(vx_image_receiver*,vx_image_ingress*);
/* Called only after the shared runtime accepted this immutable input. */
int vx_image_receiver_admitted(vx_image_receiver*,uint64_t serial);
int vx_image_receiver_complete(vx_image_receiver*,const vx_image_rx_completion*,const vx_image_rx_display*);
int vx_image_receiver_fail(vx_image_receiver*,uint64_t serial,uint32_t reason);
int vx_image_receiver_close(vx_image_receiver*);
/* Trusted shutdown only; original protocol cancellation, never GPU retirement. */
int vx_image_receiver_quiesce_unadmitted(vx_image_receiver*);
#ifdef __cplusplus
}
#endif
#endif

#ifndef TEXTURE_RX_H
#define TEXTURE_RX_H
#include <stddef.h>
#include <stdint.h>
#define TEX_SMALL_BYTES (512u*512u*4u)
#define TEX_LARGE_BYTES (2048u*2048u*4u)
#define TEX_PACKET_MAX 1280u
#define TEX_HEADER_BYTES 64u
#define TEX_CHUNK_BYTES 1200u
#define TEX_IDLE_TIMEOUT_MS 15000u
#define TEX_TOTAL_TIMEOUT_MS 180000u
#define TEX_MAGIC 0x56545831u
enum { TEX_HELLO=1, TEX_BEGIN=2, TEX_DATA=3, TEX_STATUS=4,
       TEX_CANCEL=5, TEX_ACK=6, TEX_REPLY=7 };
enum { TEX_IDLE=0, TEX_RECEIVING=1, TEX_READY=2, TEX_APPLIED=3, TEX_FAILED=4 };
enum { TEX_ERR_CRC=1, TEX_ERR_TIMEOUT=2, TEX_ERR_CANCELLED=3 };
typedef struct {uint32_t session,tx_id,small_crc,large_crc;} tex_ready;
void tex_rx_init(uint8_t *small_staging,uint8_t *large_staging,uint32_t session);
size_t tex_rx_handle(const uint8_t *packet,size_t length,uint32_t peer_ip,uint16_t peer_port,uint64_t now_ms,uint8_t *reply,size_t reply_capacity);
void tex_rx_poll(uint64_t now_ms);
int tex_rx_get_ready(tex_ready *out);
/* Foreground serial owner: atomically inspect READY and pin staging/tx before any
 * reentrant network poll. Wire CANCEL cannot release this pin. Only matching
 * tex_rx_applied/tex_rx_failed releases it. Not an IRQ/thread synchronization API. */
int tex_rx_acquire_ready(tex_ready *out);
/* Root calls only AFTER safe dual-LOD commit, current-pose GPU redraw and retirement. */
void tex_rx_applied(uint32_t tx_id,uint32_t rgba_crc,uint32_t depth_crc,uint32_t asset_id,uint32_t pose,uint32_t gpu_jobs);
void tex_rx_failed(uint32_t tx_id,uint32_t reason);
#endif

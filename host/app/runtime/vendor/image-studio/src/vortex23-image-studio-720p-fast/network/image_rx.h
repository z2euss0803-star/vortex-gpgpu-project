#ifndef IMAGE_RX_H
#define IMAGE_RX_H
#include "image_protocol.h"
typedef struct {uint32_t tx,input_id,image_crc,new_image;image_rect rect;image_params params;uint64_t started_ms;} image_request;
void image_rx_init(uint8_t *staging,uint32_t capacity,uint32_t session);
size_t image_rx_handle(const uint8_t *p,size_t n,uint32_t peer_ip,uint16_t port,uint64_t now,uint8_t *reply,size_t cap);
void image_rx_poll(uint64_t now);
int image_rx_acquire(image_request *request);
void image_rx_set_active(uint32_t id,uint32_t crc);
void image_rx_set_bundle(const char *bundle);
int image_rx_complete(uint32_t tx,const image_result *result,uint32_t jobs_before,uint32_t jobs_after,uint64_t timer_hz,uint64_t total_ticks);
void image_rx_fail(uint32_t tx,uint32_t reason);
#endif

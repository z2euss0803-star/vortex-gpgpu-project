/* Adapter-only introspection in the same translation unit. Vendor bytes and
 * the original protocol implementation remain unchanged. The C++ service owns
 * the single global receiver and serializes every call, including these reads. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "vendor/p8/network/texture_rx.c"
#pragma GCC diagnostic pop
uint32_t vx_p8_rx_state(void){return rx.state;}
uint32_t vx_p8_rx_tx(void){return rx.tx;}
uint32_t vx_p8_rx_ip(void){return rx.peer_ip;}
uint16_t vx_p8_rx_port(void){return rx.peer_port;}

#include "texture_rx.h"
#include <string.h>
#define SMALL_CHUNKS ((TEX_SMALL_BYTES+TEX_CHUNK_BYTES-1)/TEX_CHUNK_BYTES)
#define LARGE_CHUNKS ((TEX_LARGE_BYTES+TEX_CHUNK_BYTES-1)/TEX_CHUNK_BYTES)
/* No allocation; 3496 bytes of bounded reception bitmap. Single-thread owner. */
static struct {
    uint8_t *buffer[2];
    uint8_t bitmap[2][(LARGE_CHUNKS+7)/8];
    uint32_t count[2],crc[2],session,tx,peer_ip,state,reason,held;
    uint16_t peer_port;
    uint64_t start,last;
    uint32_t rgba,depth,asset,pose,jobs;
} rx;
static uint32_t crc_table[256];
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(uint8_t*p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static uint32_t crc32(const uint8_t*p,size_t n){uint32_t c=0xffffffffu;while(n--)c=crc_table[(c^*p++)&255]^(c>>8);return c^0xffffffffu;}
void tex_rx_init(uint8_t *small,uint8_t *large,uint32_t session){
    uint32_t i,j,c;memset(&rx,0,sizeof(rx));rx.buffer[0]=small;rx.buffer[1]=large;rx.session=session;
    for(i=0;i<256;i++){c=i;for(j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);crc_table[i]=c;}
}
void tex_rx_failed(uint32_t tx,uint32_t reason){if(rx.tx==tx&&(rx.state==TEX_READY||rx.state==TEX_RECEIVING)){rx.state=TEX_FAILED;rx.reason=reason;rx.held=0;}}
void tex_rx_poll(uint64_t now){
    if(rx.state==TEX_RECEIVING&&(now<rx.last||now<rx.start||now-rx.last>TEX_IDLE_TIMEOUT_MS||now-rx.start>TEX_TOTAL_TIMEOUT_MS))tex_rx_failed(rx.tx,TEX_ERR_TIMEOUT);
}
int tex_rx_get_ready(tex_ready*out){if(rx.state!=TEX_READY||!out)return 0;out->session=rx.session;out->tx_id=rx.tx;out->small_crc=rx.crc[0];out->large_crc=rx.crc[1];return 1;}
int tex_rx_acquire_ready(tex_ready*out){if(!tex_rx_get_ready(out))return 0;rx.held=1;return 1;}
void tex_rx_applied(uint32_t tx,uint32_t rgba,uint32_t depth,uint32_t asset,uint32_t pose,uint32_t jobs){
    if(rx.tx!=tx||rx.state!=TEX_READY)return;
    rx.rgba=rgba;rx.depth=depth;rx.asset=asset;rx.pose=pose;rx.jobs=jobs;rx.state=TEX_APPLIED;rx.held=0;
}
static size_t respond(uint8_t*r,uint8_t type,uint32_t lod,uint32_t index){
    memset(r,0,64);put32(r,TEX_MAGIC);r[4]=1;r[5]=type;put32(r+8,rx.session);put32(r+12,rx.tx);
    put32(r+16,lod);put32(r+20,index);put32(r+32,rx.crc[0]);put32(r+36,rx.crc[1]);put32(r+40,rx.state);
    /* Reply length word carries failure reason; no payload is attached. */
    put32(r+24,rx.reason);put32(r+44,rx.rgba);put32(r+48,rx.depth);put32(r+52,rx.asset);put32(r+56,rx.pose);put32(r+60,rx.jobs);return 64;
}
size_t tex_rx_handle(const uint8_t*p,size_t n,uint32_t ip,uint16_t port,uint64_t now,uint8_t*r,size_t cap){
    uint32_t op,session,tx,lod,index,len,total,chunks,offset,want,small_crc,large_crc;
    uint8_t mask;tex_rx_poll(now);
    if(!p||!r||cap<64||n<64||n>TEX_PACKET_MAX||get32(p)!=TEX_MAGIC||p[4]!=1||p[6]||p[7])return 0;
    op=p[5];session=get32(p+8);tx=get32(p+12);lod=get32(p+16);index=get32(p+20);len=get32(p+24);
    if(op==TEX_HELLO){if(n!=64||len)return 0;return respond(r,TEX_REPLY,0,0);}
    if(session!=rx.session||!session||!tx)return 0;
    if(op==TEX_BEGIN){
        if(n!=64||len||!rx.buffer[0]||!rx.buffer[1])return 0;
        small_crc=get32(p+32);large_crc=get32(p+36);
        if(tx==rx.tx&&ip==rx.peer_ip&&port==rx.peer_port&&rx.state!=TEX_FAILED&&small_crc==rx.crc[0]&&large_crc==rx.crc[1])return respond(r,TEX_REPLY,0,0);
        if(tx<=rx.tx||rx.held||rx.state==TEX_RECEIVING||rx.state==TEX_READY)return 0;
        memset(rx.bitmap,0,sizeof(rx.bitmap));rx.count[0]=rx.count[1]=0;
        rx.tx=tx;rx.peer_ip=ip;rx.peer_port=port;rx.crc[0]=small_crc;rx.crc[1]=large_crc;
        rx.state=TEX_RECEIVING;rx.reason=0;rx.rgba=rx.depth=rx.asset=rx.pose=rx.jobs=0;rx.start=rx.last=now;
        return respond(r,TEX_REPLY,0,0);
    }
    if(tx!=rx.tx||ip!=rx.peer_ip||port!=rx.peer_port)return 0;
    if(op==TEX_STATUS){if(n!=64||len)return 0;return respond(r,TEX_REPLY,0,0);}
    if(op==TEX_CANCEL){if(n!=64||len)return 0;if(!rx.held)tex_rx_failed(tx,TEX_ERR_CANCELLED);return respond(r,TEX_REPLY,0,0);}
    if(op!=TEX_DATA||rx.state!=TEX_RECEIVING||lod>1||len>TEX_CHUNK_BYTES||n!=64u+len)return 0;
    total=lod?TEX_LARGE_BYTES:TEX_SMALL_BYTES;chunks=(total+TEX_CHUNK_BYTES-1)/TEX_CHUNK_BYTES;
    if(index>=chunks)return 0;
    offset=index*TEX_CHUNK_BYTES;want=total-offset;if(want>TEX_CHUNK_BYTES)want=TEX_CHUNK_BYTES;
    if(len!=want||crc32(p+64,len)!=get32(p+28))return 0;
    mask=(uint8_t)(1u<<(index&7));
    if(rx.bitmap[lod][index>>3]&mask){if(memcmp(rx.buffer[lod]+offset,p+64,len))return 0;}
    else {memcpy(rx.buffer[lod]+offset,p+64,len);rx.bitmap[lod][index>>3]|=mask;rx.count[lod]++;
        if(rx.count[lod]==chunks&&crc32(rx.buffer[lod],total)!=rx.crc[lod])tex_rx_failed(tx,TEX_ERR_CRC);
    }
    rx.last=now;
    if(rx.state==TEX_RECEIVING&&rx.count[0]==SMALL_CHUNKS&&rx.count[1]==LARGE_CHUNKS)rx.state=TEX_READY;
    return respond(r,TEX_ACK,lod,index);
}

/* Loopback-only actual C RX. Completion is a stub, no GPU or display. */
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network/image_rx.h"
uint32_t host_ps_read(uint64_t p){(void)p;return 0;}
int main(void){
 WSADATA ws;WSAStartup(MAKEWORD(2,2),&ws);SOCKET fd=socket(AF_INET,SOCK_DGRAM,0);
 struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
 if(bind(fd,(struct sockaddr*)&address,sizeof(address)))return 1;int alen=sizeof(address);getsockname(fd,(struct sockaddr*)&address,&alen);
 printf("PORT %u\n",ntohs(address.sin_port));fflush(stdout);
 uint8_t *stage=malloc(IMAGE_BYTES),packet[1280],reply[1280];image_rx_init(stage,IMAGE_BYTES,0x12345678);image_rx_set_bundle("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");unsigned dropped_first=0,dropped_last=0,dropped_data=0,jobs=0;
 for(;;){struct sockaddr_in peer;int plen=sizeof(peer),n=recvfrom(fd,(char*)packet,sizeof(packet),0,(struct sockaddr*)&peer,&plen);if(n<=0)break;if(n==4&&!memcmp(packet,"STOP",4))break;
  image_header h;if(image_header_decode(&h,packet,(size_t)n))continue;
  if(h.op==IMAGE_DATA&&h.offset==1200&&!dropped_data++)continue;
  size_t length=image_rx_handle(packet,n,peer.sin_addr.s_addr,ntohs(peer.sin_port),GetTickCount64(),reply,sizeof(reply));
  image_request q;if(image_rx_acquire(&q)){
   if(q.new_image)image_rx_set_active(q.input_id,q.image_crc);
   unsigned count=q.params.style==1?5:q.params.style==2?6:q.params.style==3?11:q.params.style==4?2:0;
   image_result r={.input_id=q.input_id,.style=q.params.style,.preset=q.params.preset,.block=q.params.block,.output_crc=q.image_crc^q.params.style,.pass_count=count,.generate_ticks=1,.blit_ticks=1,.retire_ticks=2};
   if(image_rx_complete(q.tx,&r,jobs,jobs+count,1000,4))return 2;jobs+=count;
   printf("STUB_COMPLETE tx=%u new_image=%u style=%u\n",q.tx,q.new_image,q.params.style);fflush(stdout);
  }
  if(h.op==IMAGE_DATA&&h.offset==0&&!dropped_first++){continue;}
  if(h.op==IMAGE_DATA&&h.offset+ h.length==IMAGE_BYTES&&!dropped_last++){continue;}
  if(length)sendto(fd,(char*)reply,(int)length,0,(struct sockaddr*)&peer,plen);
 }
 free(stage);closesocket(fd);WSACleanup();return 0;
}

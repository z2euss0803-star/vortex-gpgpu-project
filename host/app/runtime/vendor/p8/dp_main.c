#include <stdint.h>
#include "dp_output.h"
#include "network/complex_network_api.h"
#include "network/texture_rx.h"
#include "network/texture_net.h"
#include "host_platform.h"
#include "gpgpu_hw.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "xtime_l.h"
#include "xparameters.h"
#include "sleep.h"
#include "w4_platform.h"
#ifndef VFX_BUILD_ID
#define VFX_BUILD_ID "unbound"
#endif
#ifndef DP_WIDTH
#define DP_WIDTH 1280
#define DP_HEIGHT 720
#define DP_SCALE 2
#endif
#ifndef DEFAULT_GALLERY_WARRIOR
#define DEFAULT_GALLERY_WARRIOR 1
#endif
static const uint32_t gallery_poses[8]={0,100,25,101,50,102,75,103};
static const char *gallery_names[8]={"front","front_left","left","back_left","back","back_right","right","front_right"};
extern int baseline_host_main(void);
typedef struct {p2_frame_stats gpu;uint64_t provider,blit,wait,total;} frame_record;
static frame_record records[100];
static uint32_t completed;
static int network_active;
void host_service_poll(void){if(network_active)tex_net_poll();}
static int uart_command(void){
 if(!XUartPs_IsReceiveData(XPAR_PSU_UART_0_BASEADDR))return 0;
 return (int)XUartPs_ReadReg(XPAR_PSU_UART_0_BASEADDR,XUARTPS_FIFO_OFFSET)&255;
}
static void state(const char *mode,uint32_t pose){
 xil_printf("MINIDP_DISPLAY_STATE mode=%s asset=%s completed=%u next_pose=%u gpu_jobs=%u gpu_errors=%08x controls=1:vase,2:bag,3:warrior,f:front,a:left,d:right,b:back,q:front_left,e:front_right,z:back_left,c:back_right,g:auto,p:pause,r:100,l:loop,s:stop,i:info\r\n",mode,host_display_asset_name(),completed,pose,host_mmio_read(GPU_BASE+GPU_JOBS),host_mmio_read(GPU_BASE+GPU_ERRORS));
 dp_output_snapshot(mode);
}
static void report(uint32_t n,uint64_t wall,uint32_t start_vsync,uint32_t end_vsync,int detailed){
 uint64_t provider=0,blit=0,wait=0,total=0,cycles=0;
 xil_printf("MINIDP_RAM_LOG_BEGIN records=%u asset=%s deferred=1\r\n",n,host_display_asset_name());
 for(uint32_t i=0;i<n;i++){
  frame_record *r=&records[i];provider+=r->provider;blit+=r->blit;wait+=r->wait;total+=r->total;cycles+=r->gpu.transform_elapsed+(uint64_t)r->gpu.raster_elapsed;
  if(detailed)xil_printf("MINIDP_FRAME_PRESENTED frame=%u asset=%s pose=%u rgba_crc32=%08x depth_crc32=%08x transform_cycles=%u raster_cycles=%u provider_ticks=%08x%08x blit_ticks=%08x%08x wait_ticks=%08x%08x frame_ticks=%08x%08x deferred=1 monitor_receipt=unverified confirmed_at_batch_end=1\r\n",completed-n+i,host_display_asset_name(),r->gpu.frame,r->gpu.rgba_crc32,r->gpu.depth_crc32,r->gpu.transform_elapsed,r->gpu.raster_elapsed,(uint32_t)(r->provider>>32),(uint32_t)r->provider,(uint32_t)(r->blit>>32),(uint32_t)r->blit,(uint32_t)(r->wait>>32),(uint32_t)r->wait,(uint32_t)(r->total>>32),(uint32_t)r->total);
  else xil_printf("MINIDP_FRAME_CRC frame=%u pose=%u rgba_crc32=%08x depth_crc32=%08x\r\n",completed-n+i,r->gpu.frame,r->gpu.rgba_crc32,r->gpu.depth_crc32);
 }
 uint64_t fps=wall?(uint64_t)n*COUNTS_PER_SECOND*1000/wall:0;
 xil_printf("MINIDP_PERF asset=%s frames=%u elapsed_ticks=%08x%08x timer_hz=%u fps_milli=%u mean_queue_process_us=%u mean_gpu_us=%u mean_provider_us=%u mean_blit_us=%u mean_wait_us=%u vsync_delta=%u gpu_clock_generated_hz=96968727 output_width=%u output_height=%u output_nominal_hz=60 native_width=%u native_height=%u texture_width=%u texture_height=%u epoch=%u triangles=%u monitor_receipt=unverified timing_scope=batch_includes_final_drain\r\n",host_display_asset_name(),n,(uint32_t)(wall>>32),(uint32_t)wall,(uint32_t)COUNTS_PER_SECOND,(uint32_t)fps,(uint32_t)(total*1000000/COUNTS_PER_SECOND/n),(uint32_t)(cycles*1000000/96968727u/n),(uint32_t)(provider*1000000/COUNTS_PER_SECOND/n),(uint32_t)(blit*1000000/COUNTS_PER_SECOND/n),(uint32_t)(wait*1000000/COUNTS_PER_SECOND/n),end_vsync-start_vsync,DP_WIDTH,DP_HEIGHT,host_display_native_width(),host_display_native_width(),host_display_texture_width(),host_display_texture_width(),host_display_texture_epoch(),host_display_asset_faces());
 xil_printf("MINIDP_RAM_LOG_END records=%u asset=%s\r\n",n,host_display_asset_name());
}
int main(void){
 uint32_t binding_reason=0;
 int binding_status=w4_platform_check(host_mmio_read,&binding_reason);
 xil_printf("W4_PLATFORM_BINDING status=%d reason=%u platform=HUNET_HW4_AXI_W4_20261015_faeaddb98d5d bit_sha256=faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc xsa_sha256=08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536 write_window=4 read_capacity=16 diag_abi=1 read_only=1 hardware_hash_measured=0\r\n",binding_status,binding_reason);
 if(binding_status)return binding_status;
 xil_printf("VFX_MINIDP_CANDIDATE_BEGIN source_bundle=%s profile=P8_ADAPTIVE_GLAZE warrior_views=8 warrior_material=theme_armor texture_fit=cover_visible_mean_v1 face_color=theme_glaze hands_color=face asset=porcelain objects=3 vertices=398 triangles=768 slots=16064 texture=default_qinghua_shared_LOD native=256_or_512 output=%ux%u refresh_nominal=60 scale=%u diagnostic=0 scan_source=pixol3d_embedded_CC_BY_4_0 shape_priority=1 scan_target_fps=optional default_gallery=%u hold_ms=4000 NN=0 ethernet_texture=1 ethernet_video=0 warrior_rotation=0 warrior_native=512 warrior_texture=2048 low_native=256 low_texture=512 edid_startup_fix=1 pipeline=1\r\n",VFX_BUILD_ID,DP_WIDTH,DP_HEIGHT,DP_SCALE,(uint32_t)DEFAULT_GALLERY_WARRIOR);
 int status=dp_output_init(DP_WIDTH,DP_HEIGHT,DP_SCALE);
 if(!status)status=dp_output_measure_refresh();
 if(status){xil_printf("MINIDP_INIT_FAIL status=%d\r\n",status);dp_output_snapshot("INIT_FAIL");return status;}
 xil_printf("MINIDP_COLORBAR_READY user_visual_acceptance_required=1\r\n");
 status=baseline_host_main();if(!status)status=host_complex_prepare();
 if(status){xil_printf("MINIDP_PREFLIGHT_FAIL status=%d\r\n",status);return status;}
 host_display_texture_storage_init();
 int net_status=tex_net_init((uint8_t*)(uintptr_t)0x17800000u,(uint8_t*)(uintptr_t)0x18000000u);
 network_active=net_status==0;
 xil_printf("TEXTURE_NET_APPLICATION enabled=%u init_status=%d shared_objects=3 default=qinghua link_board_validation=pending\r\n",(uint32_t)network_active,net_status);
 uint32_t initial_jobs=host_mmio_read(GPU_BASE+GPU_JOBS);
 uint32_t pose=0,remaining=100,n=0,batch_vsync=dp_output_vsyncs();uint64_t batch_begin=dp_ticks();
 int looping=0,paused=0,first_verified=0;
 int gallery=0,automatic=0;uint32_t view=0;uint64_t gallery_due=0;
 uint32_t displayed_pose=0,have_displayed=0,texture_redraw=0,resume_pose=0;
 tex_ready update={0};
 state("RUN100",pose);batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();
 for(;;){
  host_service_poll();
  int command=uart_command();
  /* First 100 are an uninterrupted identity/CRC gate; drain queued input. */
  if(!first_verified)command=0;
  if(command=='s'||command=='p'||command=='r'||command=='l'||command=='i'||command=='f'||command=='a'||command=='d'||command=='b'||command=='g'||command=='q'||command=='e'||command=='z'||command=='c'||(command>='1'&&command<='3')){
   status=dp_output_drain();if(status)break;
   if(dp_output_confirmed()!=completed+1u){status=-49;break;}
   uint64_t end=dp_ticks();uint32_t end_vsync=dp_output_vsyncs();
   if(n){report(n,end-batch_begin,batch_vsync,end_vsync,1);n=0;}
   if(command=='s'){gallery=0;automatic=0;looping=0;remaining=0;paused=0;}
   else if(command=='p'){paused=!paused;if(host_display_native_width()==512u)gallery=1;if(gallery){if(!paused)gallery_due=dp_ticks()+4ull*COUNTS_PER_SECOND;}else if(!remaining&&!looping&&!paused)looping=1;}
   else if(command=='r'||command=='l'){
    if(host_display_native_width()==512u)xil_printf("MINIDP_ROTATION_UNAVAILABLE asset=%s command=%c native_width=512 reason=shape_detail_fixed_views\r\n",host_display_asset_name(),command);
    else {gallery=0;automatic=0;pose=0;remaining=command=='r'?100:0;looping=command=='l';paused=0;}
   }
   else if(command=='f'||command=='a'||command=='d'||command=='b'||command=='g'||command=='q'||command=='e'||command=='z'||command=='c'){
    if((command=='q'||command=='e'||command=='z'||command=='c')&&host_display_native_width()!=512u){
     xil_printf("MINIDP_GALLERY_UNAVAILABLE asset=%s command=%c native_width=%u reason=warrior_diagonal_only\r\n",host_display_asset_name(),command,host_display_native_width());
    }else{
    gallery=1;automatic=command=='g';view=command=='a'?2:command=='b'?4:command=='d'?6:command=='q'?1:command=='z'?3:command=='c'?5:command=='e'?7:0;
    pose=gallery_poses[view];remaining=1;looping=0;paused=0;
    xil_printf("MINIDP_GALLERY_REQUEST asset=%s view=%s pose=%u auto=%u hold_ms=4000\r\n",host_display_asset_name(),gallery_names[view],pose,(uint32_t)automatic);
    }
   }
   else if(command>='1'&&command<='3'){
    uint32_t before=host_mmio_read(GPU_BASE+GPU_JOBS),id=(uint32_t)(command-'1');
    status=host_display_select_asset(id);if(status)break;
    xil_printf("MINIDP_ASSET_SWITCH id=%u asset=%s vertices=%u triangles=%u jobs_before=%u jobs_after=%u pending=-1 gpu_idle=1\r\n",id,host_display_asset_name(),host_display_asset_vertices(),host_display_asset_faces(),before,host_mmio_read(GPU_BASE+GPU_JOBS));
    int was_gallery=gallery;gallery=DEFAULT_GALLERY_WARRIOR&&id==2;automatic=0;view=0;
    if(gallery||was_gallery)looping=0;
    pose=0;remaining=gallery?1:looping?0:100;paused=0;have_displayed=0;
    if(gallery)xil_printf("MINIDP_GALLERY_REQUEST asset=%s view=front pose=0 auto=0 hold_ms=4000\r\n",host_display_asset_name());
   }
   state(paused?"PAUSED":gallery?"GALLERY":looping?"LOOP":remaining?"RUN100":"IDLE",pose);
   batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();
  }
  status=dp_output_health();if(status)break;
  status=host_display_texture_storage_check();if(status)break;
  if(first_verified&&network_active&&!texture_redraw&&tex_rx_acquire_ready(&update)){
   status=dp_output_drain();if(status)break;
   if(dp_output_confirmed()!=completed+1u){status=-49;break;}
   if(n){report(n,dp_ticks()-batch_begin,batch_vsync,dp_output_vsyncs(),1);n=0;}
   const uint32_t *small=(const uint32_t*)(uintptr_t)0x17800000u,*large=(const uint32_t*)(uintptr_t)0x18000000u;
   if(host_display_texture_ready_check(update.small_crc,update.large_crc)){
    tex_rx_failed(update.tx_id,TEX_ERR_CRC);
    xil_printf("TEXTURE_REJECTED tx=%u session=%u reason=CRC_OR_GUARD active_epoch=%u\r\n",update.tx_id,update.session,host_display_texture_epoch());
    continue;
   }
   uint32_t epoch=host_display_texture_epoch()+1u;
   if(!epoch){status=-53;break;}
   status=host_display_texture_commit(small,large,epoch);if(status){tex_rx_failed(update.tx_id,10u);break;}
   xil_printf("TEXTURE_COMMITTED tx=%u session=%u epoch=%u small_crc32=%08x large_crc32=%08x shared_objects=3 gpu_jobs=%u\r\n",update.tx_id,update.session,epoch,update.small_crc,update.large_crc,host_mmio_read(GPU_BASE+GPU_JOBS));
   resume_pose=pose;pose=have_displayed?displayed_pose:pose;texture_redraw=1;
   batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();
  }
  if(!texture_redraw&&gallery&&automatic&&!paused&&!remaining&&dp_ticks()>=gallery_due){
   view=(view+(host_display_native_width()==512u?1u:2u))%8;pose=gallery_poses[view];remaining=1;
   xil_printf("MINIDP_GALLERY_REQUEST asset=%s view=%s pose=%u auto=1 hold_ms=4000\r\n",host_display_asset_name(),gallery_names[view],pose);
   batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();
  }
  if(!texture_redraw&&(paused||(!looping&&!remaining))){usleep(1000);continue;}
  frame_record *r=&records[n];uint64_t begin=dp_ticks();
  status=host_display_render_pose(pose,&r->gpu);r->provider=dp_ticks()-begin;if(status)break;
  status=dp_output_present_profile((const uint32_t*)(uintptr_t)ROTATION_FB,host_display_native_width(),&r->blit,&r->wait);r->total=dp_ticks()-begin;if(status)break;
  n++;completed++;displayed_pose=pose;have_displayed=1;
  if(texture_redraw){
   status=dp_output_drain();if(status)break;
   if(dp_output_confirmed()!=completed+1u){status=-49;break;}
   tex_rx_applied(update.tx_id,r->gpu.rgba_crc32,r->gpu.depth_crc32,host_display_native_width()==512u?2u:host_display_asset_faces()==648u?1u:0u,pose,host_mmio_read(GPU_BASE+GPU_JOBS));
   xil_printf("TEXTURE_APPLIED tx=%u epoch=%u asset=%s pose=%u native_width=%u texture_width=%u rgba_crc32=%08x depth_crc32=%08x gpu_jobs=%u confirmed=%u pending=-1 monitor_receipt=unverified\r\n",update.tx_id,host_display_texture_epoch(),host_display_asset_name(),pose,host_display_native_width(),host_display_texture_width(),r->gpu.rgba_crc32,r->gpu.depth_crc32,host_mmio_read(GPU_BASE+GPU_JOBS),dp_output_confirmed());
   report(n,dp_ticks()-batch_begin,batch_vsync,dp_output_vsyncs(),1);n=0;
   pose=resume_pose;texture_redraw=0;
   state(paused?"PAUSED":gallery?"GALLERY":looping?"LOOP":remaining?"RUN100":"IDLE",pose);
   batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();continue;
  }
  if(!gallery)pose=(pose+1)%100;
  if(remaining)remaining--;
  if(n==100||(!looping&&!remaining)){
   status=dp_output_drain();if(status)break;
   if(dp_output_confirmed()!=completed+1u){status=-49;break;}
   uint64_t end=dp_ticks();uint32_t end_vsync=dp_output_vsyncs();
   report(n,end-batch_begin,batch_vsync,end_vsync,!looping);n=0;
   if(!first_verified){
    if(host_mmio_read(GPU_BASE+GPU_JOBS)!=initial_jobs+200u||host_mmio_read(GPU_BASE+GPU_STATUS)||host_mmio_read(GPU_BASE+GPU_ERRORS)){status=-46;break;}
    first_verified=1;
    xil_printf("MINIDP_100FRAME_GPU_DISPLAY_PASS frames=100 jobs=%u errors=00000000 guards=64 monitor_receipt=unverified confirmed=101 pipeline=1\r\n",initial_jobs+200u);
   }
   if(gallery){
    xil_printf("MINIDP_GALLERY_HOLD asset=%s view=%s pose=%u auto=%u hold_ms=4000 new_gpu_frames=%u\r\n",host_display_asset_name(),gallery_names[view],pose,(uint32_t)automatic,completed);
    gallery_due=dp_ticks()+4ull*COUNTS_PER_SECOND;
   }
   state(gallery?"GALLERY":looping?"LOOP":"IDLE",pose);batch_begin=dp_ticks();batch_vsync=dp_output_vsyncs();
  }
 }
 if(update.tx_id)tex_rx_failed(update.tx_id,11u);
 network_active=0;tex_net_close();
 xil_printf("MINIDP_DISPLAY_FAIL status=%d completed=%u asset=%s\r\n",status,completed,host_display_asset_name());dp_output_snapshot("FAULT");state("FAULT",pose);
 for(;;){if(uart_command()=='i')state("FAULT",pose);usleep(1000);}
}

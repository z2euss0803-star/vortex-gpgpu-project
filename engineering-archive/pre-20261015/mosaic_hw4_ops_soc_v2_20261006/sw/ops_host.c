#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_platform.h"
#include "host_dpi.h"
#include "mosaic_device.h"
#include "mosaic_addresses.h"
#include "fixture_inputs.h"
#include "fixture_reference.h"
#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
static unsigned round_index;
static FILE *new_output(const char *name){
    FILE *old=fopen(name,"rb");if(old){fclose(old);return NULL;}
    /* The one-shot launcher owns this new run; reject all existing outputs. */
    return fopen(name,"wb");
}
static uint32_t access_(int op,uint64_t a,uint32_t value) {
    unsigned int reply=0;
    if(host_bus(op,a,value,&reply)) { fprintf(stderr,"HOST_DPI_TASK_DISABLED\n");abort(); }
    return reply;
}
uint32_t host_mmio_read(uint64_t a){return access_(0,a,0);}
void host_mmio_write(uint64_t a,uint32_t v){(void)access_(1,a,v);}
uint32_t host_ps_read(uint64_t a){return access_(2,a,0);}
void host_ps_write(uint64_t a,uint32_t v){(void)access_(3,a,v);}
void host_flush(uint64_t a,uint32_t n){(void)access_(4,a,n);}
void host_invalidate(uint64_t a,uint32_t n){(void)access_(5,a,n);}
void host_barrier(void){(void)access_(6,0,0);}
void host_phase(uint32_t v){(void)access_(7,0,v);}
void host_log(const char *fmt,...){va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);fflush(stdout);}
static uint32_t rd(void *p,uint64_t a){(void)p;return host_mmio_read(a);}
static void wr(void *p,uint64_t a,uint32_t v){(void)p;host_mmio_write(a,v);}
static void stage_job(void *p,uint64_t a,const MosaicJob *j){
    unsigned i;uint32_t words[32];(void)p;memcpy(words,j,128);
    for(i=0;i<32;i++)host_ps_write(a+4*i,words[i]);
}
static void flush(void *p,uint64_t a,uint32_t n){(void)p;host_flush(a,n);}
static void barrier(void *p){(void)p;host_barrier();}
static uint64_t now_us(void *p){(void)p;return access_(8,0,0);}
static const MosaicDeviceIo io={rd,wr,stage_job,flush,barrier,now_us,NULL};
static MosaicDeviceSnapshot snapshot;
static int failure(const char *where,int rc,unsigned job){
    host_log("MOSAIC_SOC_FAIL where=%s rc=%d round=%u job=%u status=%08x errors=%08x pw=%u pr=%u jobs_before=%u jobs_after=%u cdma=%08x\n",
        where,rc,round_index,job,snapshot.status,snapshot.errors,snapshot.pending_writes,
        snapshot.pending_reads,snapshot.jobs_before,snapshot.jobs_after,snapshot.cdma_status);
    return -1;
}
static int dma(uint64_t source,uint64_t dest,uint32_t bytes){
    return mosaic_cdma_copy_all(&io,source,dest,bytes,100000,&snapshot);
}
static int initialize(void){
    FILE *f;uint32_t word;unsigned i;int rc;
    host_phase(1);
    if(host_mmio_read(0xa0000000)!=0x56580204 || host_mmio_read(0xa0000004)!=0x00040404)
        return failure("ID_CONFIG",-1,0);
    {uint64_t start=now_us(NULL);
     while(!(host_mmio_read(0xa0000034)&1) || !(host_mmio_read(0xb0020000)&1))
         if(now_us(NULL)-start>=100000)return failure("CALIBRATION_TIMEOUT",-1,0);}
    /* PS-local preparation models CPU/JTAG ownership. No PL memory backdoor. */
    for(i=0;i<1024;i++)host_ps_write(4u*i,0);
    host_flush(0,4096);
    for(i=0;i<16;i++)host_ps_write(0x10030000+4*i,0);
    host_flush(0x10030000,64);
    f=fopen("payload.bin","rb");if(!f)return failure("PAYLOAD_OPEN",-1,0);
    for(i=0;i<PAYLOAD_BYTES;i+=4){
        if(fread(&word,4,1,f)!=1){fclose(f);return failure("PAYLOAD_SHORT",-1,0);}
        host_ps_write(0x11000000u+i,word);
    }
    if(fgetc(f)!=EOF){fclose(f);return failure("PAYLOAD_TRAILING",-1,0);}fclose(f);
    host_flush(0x11000000,PAYLOAD_BYTES);host_barrier();host_phase(2);
    for(i=0;i<ARRAY_COUNT(regions);i++){
        rc=dma(regions[i].ps,mosaic_gpu_bus_address(regions[i].gpu),regions[i].bytes);
        if(rc)return failure("INITIAL_DMA",rc,0);
        host_log("MOSAIC_SOC_LOADED round=%u region=%u gpu=%08x bytes=%u\n",round_index,i,regions[i].gpu,regions[i].bytes);
    }
    /* Explicit real CDMA readback of every PACK buffer before any GPU job. */
    {const unsigned pack_jobs[]={0,2,4,6};unsigned k,n;
     for(k=0;k<4;k++){
        unsigned j=pack_jobs[k];uint32_t addr=job_words[j][12];uint32_t bytes=output_regions[j].bytes;
        rc=dma(mosaic_gpu_bus_address(addr),0x14000040,bytes);
        if(rc)return failure("PACK_ZERO_DMA",rc,j);
        host_invalidate(0x14000040,bytes);host_barrier();
        for(n=0;n<bytes;n+=4)if(host_ps_read(0x14000040+n)!=0)return failure("PACK_INITIAL_NONZERO",-1,j);
        host_log("MOSAIC_SOC_PACK_ZERO round=%u job=%u gpu=%08x values=%u\n",round_index,j,addr,bytes/4);
     }}
    return 0;
}
static int capture(unsigned job){
    const Region *r=&output_regions[job];FILE *f;unsigned i,g;int rc,bad_numeric=0,bad_guard=0;char name[96];
    host_phase(5);
    /* Poison and flush the CPU readback allocation before the external write. */
    for(i=0;i<r->bytes;i+=4)host_ps_write(r->ps+i,0xa55a1234);
    host_flush(r->ps,r->bytes);host_barrier();
    rc=dma(mosaic_gpu_bus_address(r->gpu),r->ps,r->bytes);if(rc)return failure("OUTPUT_DMA",rc,job);
    host_invalidate(r->ps,r->bytes);host_barrier();
    snprintf(name,sizeof(name),"actual_round%u_job%u.hex",round_index,job);
    f=new_output(name);if(!f)return failure("OUTPUT_OPEN",-1,job);
    for(i=0;i<r->bytes;i+=4){
        uint32_t v=host_ps_read(r->ps+i),e=golden_bits[golden_offsets[job]+i/4];float a,b;double d,limit;
        fprintf(f,"%08x %08x\n",r->gpu+i,v);
        memcpy(&a,&v,4);memcpy(&b,&e,4);d=(double)a-b;if(d<0)d=-d;
        limit=0.02+0.0002*fabs((double)b);
        if((v&0x7f800000u)==0x7f800000u || d>limit)bad_numeric=1;
    }fclose(f);
    snprintf(name,sizeof(name),"guards_round%u_job%u.hex",round_index,job);
    f=new_output(name);if(!f)return failure("GUARDS_OPEN",-1,job);
    for(g=0;g<ARRAY_COUNT(guard_regions);g++){
        r=&guard_regions[g];rc=dma(mosaic_gpu_bus_address(r->gpu),r->ps,r->bytes);
        if(rc){fclose(f);return failure("GUARD_DMA",rc,job);}
        host_invalidate(r->ps,r->bytes);host_barrier();
        for(i=0;i<r->bytes;i+=4){uint32_t v=host_ps_read(r->ps+i);fprintf(f,"%08x %08x\n",r->gpu+i,v);
            if(v!=0x5aa59669)bad_guard=1;}
    }fclose(f);
    if(bad_numeric)return failure("NUMERIC",-1,job);
    if(bad_guard)return failure("GUARD_VALUE",-1,job);
    return 0;
}
int host_main(void){
    unsigned j;int rc;MosaicJob job;
    if(round_index>=2)return failure("EXTRA_ROUND",-1,0);
    host_log("MOSAIC_SOC_ROUND_START round=%u\n",round_index);
    if(initialize())return -1;
    for(j=0;j<10;j++){
        memcpy(&job,job_words[j],128);host_phase(4);(void)access_(9,0,j);
        host_log("MOSAIC_SOC_JOB_START round=%u job=%u op=%u tasks=%u\n",round_index,j,job.op,job.tasks);
        rc=mosaic_submit(&io,&job,10000000,200000,&snapshot);
        if(rc)return failure("GPU_SUBMIT",rc,j);
        if(snapshot.jobs_before!=j || snapshot.jobs_after!=j+1)return failure("JOB_SEQUENCE",-1,j);
        host_log("MOSAIC_SOC_DONE round=%u job=%u status=%08x errors=%08x pw=%u pr=%u elapsed=%u jobs=%u\n",
            round_index,j,snapshot.status,snapshot.errors,snapshot.pending_writes,snapshot.pending_reads,snapshot.elapsed,snapshot.jobs_after);
        if(capture(j))return -1;
        host_log("MOSAIC_SOC_JOB_PASS round=%u job=%u checks=%u guards=544\n",round_index,j,output_regions[j].bytes/4);
    }
    host_phase(6);host_log("MOSAIC_SOC_ROUND_PASS round=%u jobs=10 checks=24920 guards=5440\n",round_index);
    round_index++;return 0;
}
int host_entry(int *result){*result=host_main();return 0;}

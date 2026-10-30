#define _POSIX_C_SOURCE 200809L
#include <vxl/mosaic_client.h>
#include <vxl/mosaic_runtime.h>
#include <vxl/hunet.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
static vxl_u64 clock_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (vxl_u64)t.tv_sec*1000000000u+t.tv_nsec;}
static vxl_u32 u32(const vxl_u8 *p){return p[0]|((vxl_u32)p[1]<<8)|((vxl_u32)p[2]<<16)|((vxl_u32)p[3]<<24);}
static vxl_u64 u64(const vxl_u8 *p){return u32(p)|((vxl_u64)u32(p+4)<<32);}
static int fail(struct mosaic_client *c,int rc){if(!c->first_error)c->first_error=rc;return c->first_error;}
static struct vxl_request request(const struct mosaic_client *c){struct vxl_request r={.abi_major=1,.struct_size=176,.profile_id=6,.generation=c->generation};return r;}
static int invoke(struct mosaic_client *c,vxl_u32 cmd,struct vxl_request *r){return ioctl(c->fd,cmd,r)<0?-errno:0;}
static int payload(struct mosaic_client *c,vxl_u32 cmd,void *p,size_t n){struct vxl_request r=request(c);r.user_ptr=(uintptr_t)p;r.length=n;return invoke(c,cmd,&r);}
static int transfer(struct mosaic_client *c,int to,unsigned role,vxl_u64 offset,void *p,size_t n){struct vxl_request r=request(c);r.handle=c->handles[role];r.offset=offset;r.user_ptr=(uintptr_t)p;r.length=n;int rc=invoke(c,to?VXL_COPY_TO:VXL_COPY_FROM,&r);return rc?rc:r.used!=n?-EIO:0;}
static int equal_manifest(const struct mosaic_manifest *a,const struct mosaic_manifest *b){return !memcmp(a,b,sizeof(*a));}
int mosaic_client_attach_fd(struct mosaic_client *c,int fd){
 if(!c||fd<0)return -EINVAL;
 memset(c,0,sizeof(*c));c->fd=fd;c->evidence_fd=-1;struct vxl_request r=request(c);int rc=invoke(c,VXL_GET_INFO,&r);if(rc)return rc;
 if(r.profile_id!=6||!r.generation||r.state!=VXL_READY||r.errors||r.pending_w||r.pending_r||r.capability!=0x10001)return -ENODEV;
 c->generation=r.generation;rc=payload(c,VXL_MOSAIC_BINDING,&c->binding,512);if(rc)return rc;
 if(c->binding.magic!=MOSAIC_BINDING_MAGIC||c->binding.version!=1||c->binding.bytes!=512||c->binding.profile!=6||c->binding.program!=12||c->binding.abi_major!=1)return -EPROTO;
 vxl_hunet_hash(&c->binding,512,c->binding_sha);c->attached=1;return 0;
}
int mosaic_client_configure(struct mosaic_client *c,const struct mosaic_program *p,const void *assets,size_t bytes,vxl_u32 layout){
 if(!c||!p||!assets||!c->attached||c->loaded||c->first_error||!p->assets||!p->flat||!p->flat_bytes||layout>=p->layout_count||bytes!=p->assets_bytes)return -EINVAL;
 if(memcmp(p->binding_sha,c->binding_sha,32)||mosaic_layout_check(&p->layouts[layout].layout))return -ESTALE;
 if(mosaic_program_rodata_check(p))return -EBADMSG;
 vxl_u8 sha[32];vxl_hunet_hash(p->flat,p->flat_bytes,sha);if(memcmp(sha,c->binding.digests[MOSAIC_FLAT_SHA],32))return -EBADMSG;
 if(mosaic_assets_validate(assets,bytes,p->assets)||memcmp(p->assets->package_sha,c->binding.digests[MOSAIC_ASSETS_SHA],32)||memcmp(p->assets->source_sha[0],c->binding.digests[MOSAIC_MODEL_SHA],32))return -EBADMSG;
 mosaic_program_catalog_hash(p,0,sha);if(memcmp(sha,c->binding.digests[MOSAIC_LAYOUT_SHA],32))return -ESTALE;
 mosaic_program_catalog_hash(p,1,sha);if(memcmp(sha,c->binding.digests[MOSAIC_GRAPH_SHA],32))return -ESTALE;
 c->program=p;c->assets=assets;c->assets_bytes=bytes;c->layout_index=layout;return 0;
}
int mosaic_client_load(struct mosaic_client *c,const struct mosaic_manifest *m){
 if(!c||!c->program||!m||!c->attached||c->first_error)return -EINVAL;
 if(c->loaded)return -EALREADY;
 if(!equal_manifest(m,c->program->assets))return -ESTALE;
 vxl_u64 start=clock_ns();if(!start)return fail(c,-ENODATA);
 if(getrandom(c->nonce,16,0)!=16)return fail(c,-EIO);
 vxl_u8 nz=0;for(unsigned i=0;i<16;++i)nz|=c->nonce[i];if(!nz)return fail(c,-ENODATA);
 const vxl_u32 address[5]={0x80000000,0x80800000,0x82000000,0xfff00000,0};const vxl_u64 sizes[5]={0x800000,0x1000000,c->program->layouts[c->layout_index].layout.end-0x82000000,0x100000,128};
 for(unsigned i=0;i<5;++i){struct vxl_request r=request(c);r.domain=i==4?VXL_PS:VXL_PL;r.purpose=i==0?VXL_CODE:i==4?VXL_ARGS:VXL_DATA;r.length=sizes[i];r.address=address[i];r.alignment=64;int rc=invoke(c,i==4?VXL_ALLOC:VXL_RESERVE,&r);if(rc)return fail(c,rc);if(!r.handle||r.gpu_address!=address[i])return fail(c,-EPROTO);c->handles[i]=r.handle;}
 int rc=transfer(c,1,0,0,(void *)c->program->flat,c->program->flat_bytes);if(!rc)rc=transfer(c,1,1,0,(void *)c->assets,c->assets_bytes);
 if(!rc&&c->program->rodata_bytes)rc=transfer(c,1,0,c->program->rodata_gpu-MOSAIC_CODE_GPU,(void *)c->program->rodata,c->program->rodata_bytes);
 struct vxl_mosaic_seal_v1 s={.magic=VXL_MOSAIC_SEAL_MAGIC,.version=1,.bytes=128,.layout=c->layout_index,.generation=c->generation};memcpy(s.handles,c->handles,sizeof(s.handles));memcpy(s.binding_sha,c->binding_sha,32);memcpy(s.nonce,c->nonce,16);
 if(!rc)rc=payload(c,VXL_MOSAIC_SEAL,&s,128);
 if(rc)return fail(c,rc);
 c->loaded=1;c->metrics.cold_ns=clock_ns()-start;return 0;
}
int mosaic_client_frame(struct mosaic_client *c,const void *input,size_t in,void *output,size_t out,int evidence_fd){
 if(!c||!c->loaded||c->frame_active||c->first_error||!input||!output||evidence_fd<0)return -EINVAL;
 const struct mosaic_layout *l=&c->program->layouts[c->layout_index].layout;size_t need=(size_t)l->width*l->height*12;
 if(in!=need||out!=need)return -EMSGSIZE;
 /* Input and result must not overlap: input identity remains reproducible. */
 uintptr_t a=(uintptr_t)input,b=(uintptr_t)output;if(a>UINTPTR_MAX-in||b>UINTPTR_MAX-out||(a<b+out&&b<a+in))return -EINVAL;
 c->input=input;c->input_bytes=in;c->output=output;c->output_bytes=out;c->evidence_fd=evidence_fd;return 0;
}
static int write_evidence(struct mosaic_client *c,const vxl_u8 wire[512]){size_t done=0;while(done<512){ssize_t n=write(c->evidence_fd,wire+done,512-done);if(n<0&&errno==EINTR)continue;if(n<=0)return n<0?-errno:-EIO;done+=(size_t)n;}return 0;}
static int evidence(struct mosaic_client *c,vxl_u64 id,const struct mosaic_job_v1 *job,const vxl_u8 input_sha[32]){
 struct vxl_request r=request(c);r.job_id=id;r.user_ptr=(uintptr_t)c->last_evidence;r.length=512;int rc=invoke(c,VXL_MOSAIC_EVIDENCE,&r);if(rc)return rc;
 rc=write_evidence(c,c->last_evidence);if(rc)return rc;
 const vxl_u8 *w=c->last_evidence;
 if(u32(w)!=0x31454d56||u32(w+4)!=1||u32(w+8)!=512||u32(w+12)!=6||memcmp(w+16,job,128)||u64(w+144)!=id||u32(w+152)!=VXL_COMPLETED||u32(w+156)||u32(w+160)!=127||!u32(w+164)||u32(w+164)>=4000000000u||u32(w+168)!=1||u32(w+172)!=4000000000u||u64(w+176)!=0xa0000028ULL||u32(w+184)!=VXL_DONE||u32(w+192)||u32(w+196)||u32(w+200)||u32(w+208)||!u32(w+212)||u32(w+216)!=c->generation||u32(w+220)!=job->sequence||u32(w+224)!=15||!u32(w+248)||!u32(w+252)||u32(w+256)||memcmp(w+320,c->binding_sha,32)||memcmp(w+352,input_sha,32)||memcmp(w+432,c->nonce,16))return -EBADMSG;
 if(!(u32(w+188)&2u)||(u32(w+188)&4u)||u32(w+244)&0x4770u||u32(w+248)!=1||u32(w+252)!=1||u32(w+260))return -EBADMSG;
 if(!u64(w+264))return -ENODATA;
 for(unsigned i=0;i<4;++i)if(u32(w+228+i*4))return -EIO;
 for(unsigned i=0;i<6;++i)if(u64(w+272+i*8)<u64(w+264+i*8))return -ENODATA;
 for(unsigned i=448;i<512;++i)if(w[i])return -EPROTO;
 if(UINT64_MAX-c->metrics.raw_cycles<u32(w+164))return -EOVERFLOW;
 c->metrics.raw_cycles+=u32(w+164);++c->metrics.jobs;
 vxl_u64 *m[6]={&c->metrics.io_ns,&c->metrics.io_bytes,&c->metrics.guard_ns,&c->metrics.immutable_ns,&c->metrics.poll_ns,&c->metrics.handoff_ns};for(unsigned i=0;i<6;++i){vxl_u64 n=u64(w+384+i*8);if(UINT64_MAX-*m[i]<n)return -EOVERFLOW;*m[i]+=n;}
 return 0;
}
static int run_id(const char *s,vxl_u8 id[16]){for(unsigned i=0;i<32;++i){unsigned x=s[i]>='0'&&s[i]<='9'?s[i]-'0':s[i]>='a'&&s[i]<='f'?s[i]-'a'+10:99;if(x>15)return -EINVAL;if(i&1)id[i/2]|=x;else id[i/2]=(vxl_u8)(x<<4);}if(s[32])return -EINVAL;vxl_u8 nz=0;for(unsigned i=0;i<16;++i)nz|=id[i];return nz?0:-EINVAL;}
static int workload_admitted(const struct mosaic_client *c){
 int changed=0;for(unsigned i=0;i<6;++i)if(c->batch_limits[i]&&c->batch_limits[i]!=4096)changed=1;
 if(c->prefix_max_jobs||(!changed&&!c->frame_max_jobs))return 0;
 if(!c->frame_max_jobs||!c->frame_max_evidence_bytes)return -E2BIG;
 const struct mosaic_layout *l=&c->program->layouts[c->layout_index].layout;vxl_u64 jobs=0;
 for(vxl_u32 phase=0;phase<77;++phase){vxl_u32 layer,op,items;int rc=mosaic_phase(phase,l->width,l->height,&layer,&op,&items);if(rc)return rc;
  if(!op||op>6||!items)return -EPROTO;
  vxl_u32 limit=c->batch_limits[op-1]?c->batch_limits[op-1]:4096;
  if(limit>4096)return -ERANGE;
  jobs+=((vxl_u64)items+limit-1)/limit;
 }
 return jobs>c->frame_max_jobs||jobs>c->frame_max_evidence_bytes/512?-E2BIG:0;
}
int mosaic_client_run(struct mosaic_client *c,const struct demo_run *run,struct demo_result *result){
 if(!c||!run||!result||!c->loaded||!c->input||!c->output||c->evidence_fd<0)return -EINVAL;
 if(c->first_error)return c->first_error;
 if(c->frame_active)return -EBUSY;
 if(run->app!=DEMO_MOSAIC||run->generation!=c->generation||run->diagnostics)return -EINVAL;
 int admission=workload_admitted(c);if(admission)return admission;
 if(c->epoch==UINT32_MAX)return -EOVERFLOW;
 vxl_u8 id[16];int rc=run_id(run->run_id,id);if(rc||!memcmp(id,c->last_run,16))return -ESTALE;
 vxl_u64 cold=c->metrics.cold_ns;memset(&c->metrics,0,sizeof(c->metrics));c->metrics.cold_ns=cold;vxl_u64 start=clock_ns(),t=start;
 if(!start)return fail(c,-ENODATA);
 const struct mosaic_catalog_layout *l=&c->program->layouts[c->layout_index];vxl_u64 offset=l->layout.region[MOSAIC_FP_A].address-0x82000000ULL;
 rc=transfer(c,1,2,offset,(void *)c->input,c->input_bytes);c->metrics.input_ns=clock_ns()-t;if(rc)return fail(c,rc);
 struct vxl_mosaic_frame_v1 f={.magic=VXL_MOSAIC_FRAME_MAGIC,.version=1,.bytes=128,.layout=c->layout_index,.generation=c->generation,.input_epoch=c->epoch+1};memcpy(f.run_id,id,16);memcpy(f.binding_tag,c->binding_sha,16);vxl_hunet_hash(c->input,c->input_bytes,f.input_sha);
 t=clock_ns();rc=payload(c,VXL_MOSAIC_BEGIN,&f,128);c->metrics.begin_ns=clock_ns()-t;if(rc)return fail(c,rc);
 c->frame_active=1;c->epoch=f.input_epoch;memcpy(c->last_run,id,16);vxl_u32 sequence=1;
 for(vxl_u32 phase=0;phase<77;++phase){vxl_u32 layer,op,items;rc=mosaic_phase(phase,l->layout.width,l->layout.height,&layer,&op,&items);if(rc)return fail(c,rc);
  for(vxl_u32 first=0;first<items;){vxl_u32 count=0;
   if(!op||op>6)return fail(c,-EPROTO);
   rc=mosaic_batch_next(items,first,c->batch_limits[op-1]?c->batch_limits[op-1]:4096,&count);if(rc)return fail(c,rc);
   struct mosaic_job_v1 j={.magic=0x31434d56,.version=1,.bytes=128,.generation=c->generation,.sequence=sequence,.op=op,.layer=layer,.tile_first=first,.tile_count=count,.core_mask=15,.input_epoch=c->epoch};memcpy(j.run_id,id,16);memcpy(j.binding_tag,c->binding_sha,16);memcpy(j.layout_tag,l->layout_sha,16);memcpy(j.graph_tag,l->graph_sha,16);j.crc=mosaic_job_crc(&j);
   t=clock_ns();rc=transfer(c,1,4,0,&j,128);struct vxl_request r=request(c);r.handle=c->handles[0];r.handle2=c->handles[4];r.trust_id=12;r.deadline_ms=60000;if(!rc)rc=invoke(c,VXL_SUBMIT,&r);vxl_u64 job=r.job_id;
   if(!rc&&!job)rc=-EPROTO;
   vxl_u64 deadline=clock_ns()+60000000000ULL;
   while(!rc){r=request(c);r.job_id=job;r.wait_ms=1000;rc=invoke(c,VXL_WAIT,&r);if(rc==-ETIMEDOUT&&clock_ns()<deadline){rc=0;continue;}break;}
   c->metrics.submit_wait_ns+=clock_ns()-t;
   t=clock_ns();int saved=rc;int observed=job?evidence(c,job,&j,f.input_sha):-ENODATA;if(saved||observed)return fail(c,saved?saved:observed);
   r=request(c);r.job_id=job;rc=invoke(c,VXL_REAP,&r);c->metrics.evidence_ns+=clock_ns()-t;if(rc)return fail(c,rc);
   ++sequence;first+=count;
   if(c->prefix_max_jobs&&(c->metrics.jobs>=c->prefix_max_jobs||c->metrics.jobs>=c->prefix_max_evidence_bytes/512)){
    /* Valid evidence and REAP already completed. Keep frame/owner resident:
     * no output readback, END, successor dispatch or application switch. */
    c->prefix_stopped=1;c->metrics.total_ns=clock_ns()-start;return -EINPROGRESS;
   }
  }
 }
 t=clock_ns();rc=transfer(c,0,2,offset,c->output,c->output_bytes);c->metrics.output_ns=clock_ns()-t;if(rc)return fail(c,rc);
 const vxl_u8 *bytes=c->output;for(size_t i=0;i<c->output_bytes;i+=4)if((u32(bytes+i)&0x7f800000u)==0x7f800000u)return fail(c,-EDOM);
 t=clock_ns();rc=payload(c,VXL_MOSAIC_END,&f,128);c->metrics.end_ns=clock_ns()-t;if(rc)return fail(c,rc);c->frame_active=0;c->metrics.total_ns=clock_ns()-start;
 /* Adapter must serialize measured fields and output artifact before publication. */
 memset(result,0,sizeof(*result));result->run=*run;result->run.state=DEMO_COMPUTE_DONE;result->run.raw_cycles=c->metrics.raw_cycles;result->run.cycle_valid=1;result->run.timing_valid=1;
 vxl_u8 sha[32];vxl_hunet_hash(c->output,c->output_bytes,sha);static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;++i){result->source_result_sha[i*2]=hex[sha[i]>>4];result->source_result_sha[i*2+1]=hex[sha[i]&15];}return 0;
}
int mosaic_client_close(struct mosaic_client *c){
 if(!c||!c->attached)return -EINVAL;
 if(c->first_error||c->frame_active)return c->first_error?c->first_error:-EBUSY;
 for(unsigned i=5;i;--i)if(c->handles[i-1]){struct vxl_request r=request(c);r.handle=c->handles[i-1];int rc=invoke(c,VXL_FREE,&r);if(rc)return fail(c,rc);c->handles[i-1]=0;}
 c->loaded=c->attached=0;return 0;
}

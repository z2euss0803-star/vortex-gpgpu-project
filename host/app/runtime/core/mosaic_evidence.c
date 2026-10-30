#include <vxl/mosaic_runtime.h>
static void put32(vxl_u8 *p,vxl_u32 n){for(unsigned i=0;i<4;++i)p[i]=(vxl_u8)(n>>(8*i));}
static void put64(vxl_u8 *p,vxl_u64 n){put32(p,(vxl_u32)n);put32(p+4,(vxl_u32)(n>>32));}
int mosaic_evidence_encode(const struct mosaic_ctx *c,vxl_u8 out[512]){
 if(!c||!c->host||!c->host->program||!out)return -EINVAL;
 const struct mosaic_host_state *h=c->host;const struct mosaic_evidence *e=&h->evidence;const struct mosaic_completion *p=&e->observed;const struct vxl_event *v=&p->before_ack;
 if(!e->job_id&&!h->first_error)return -ENODATA;
 memset(out,0,512);put32(out,0x31454d56);put32(out+4,1);put32(out+8,512);put32(out+12,6);memcpy(out+16,&e->args,128);put64(out+144,e->job_id);put32(out+152,e->state);put32(out+156,(vxl_u32)e->result);put32(out+160,p->checks);put32(out+164,p->elapsed_raw);put32(out+168,e->elapsed_valid);put32(out+172,p->watchdog);put64(out+176,p->elapsed_address);
 const vxl_u32 event[8]={v->kind,v->status,v->errors,v->pending_w,v->pending_r,v->jobs,v->idle,v->ready};for(unsigned i=0;i<8;++i)put32(out+184+i*4,event[i]);
 put32(out+216,p->generation);put32(out+220,p->sequence);put32(out+224,p->exit_valid);for(unsigned i=0;i<4;++i)put32(out+228+i*4,p->exitcode[i]);put32(out+244,p->cdma_status);put32(out+248,p->ack_idle);put32(out+252,p->ack_ready);put32(out+256,p->tex_pending);
 const vxl_u64 times[7]={p->submit_enter,p->prepare_enter,p->prepare_done,p->start,p->done_seen,p->ack_done,p->complete};for(unsigned i=0;i<7;++i)put64(out+264+i*8,times[i]);
 memcpy(out+320,h->program->binding_sha,32);memcpy(out+352,h->input_sha,32);const vxl_u64 metrics[6]={h->io_ns,h->io_bytes,h->guard_ns,h->immutable_ns,h->poll_ns,h->handoff_ns};for(unsigned i=0;i<6;++i)put64(out+384+i*8,metrics[i]);memcpy(out+432,h->nonce,16);return 0;
}
int mosaic_uapi_validate(vxl_u32 cmd,const struct vxl_request *r,vxl_u32 bytes,int compat){
 if(compat)return -EOPNOTSUPP;
 if(!r)return -EFAULT;
 if(r->profile_id!=6)return -ENOTTY;
 switch(cmd){case VXL_MOSAIC_BINDING:case VXL_MOSAIC_SEAL:case VXL_MOSAIC_BEGIN:case VXL_MOSAIC_END:case VXL_MOSAIC_EVIDENCE:break;default:return -ENOTTY;}
 if(r->abi_major!=1)return -EPROTONOSUPPORT;
 if(bytes!=176||r->struct_size!=176||!r->generation||!r->user_ptr||r->length>~(vxl_u64)0-r->user_ptr)return -EINVAL;
 vxl_u64 length=cmd==VXL_MOSAIC_BINDING?512:128;
 if(cmd==VXL_MOSAIC_EVIDENCE){if(!r->length||r->length>512||r->offset>512-r->length)return -EINVAL;}
 else if(r->length!=length)return -EINVAL;
 struct vxl_request rest=*r;rest.abi_major=rest.struct_size=rest.generation=rest.profile_id=0;rest.user_ptr=rest.length=0;
 if(cmd==VXL_MOSAIC_EVIDENCE)rest.offset=rest.job_id=0;
 const vxl_u8 *raw=(const vxl_u8*)&rest;for(unsigned i=0;i<176;++i)if(raw[i])return -EINVAL;
 return 0;
}

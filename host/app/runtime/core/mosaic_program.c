#include <vxl/mosaic_host.h>
#include <vxl/hunet.h>
static int nz(const vxl_u8 *p,unsigned n){vxl_u8 v=0;for(unsigned i=0;i<n;++i)v|=p[i];return v!=0;}
static void word(struct vxl_hunet_sha *s,vxl_u32 value){vxl_u8 b[4];for(unsigned i=0;i<4;++i)b[i]=(vxl_u8)(value>>(8*i));vxl_hunet_sha_update(s,b,4);}
static void wide(struct vxl_hunet_sha *s,vxl_u64 value){word(s,(vxl_u32)value);word(s,(vxl_u32)(value>>32));}
int mosaic_program_rodata_check(const struct mosaic_program *p){
 vxl_u8 actual[32];if(!p)return -EINVAL;
 if(!p->rodata_bytes)return p->rodata||p->rodata_gpu||nz(p->rodata_sha,32)?-EINVAL:0;
 if(!p->rodata||p->rodata_bytes!=6307840u||p->rodata_gpu!=0x80110000u||p->rodata_bytes>0x80800000u-p->rodata_gpu)return -ERANGE;
 vxl_hunet_hash(p->rodata,p->rodata_bytes,actual);return memcmp(actual,p->rodata_sha,32)?-ESTALE:0;
}
int mosaic_program_code_span(const struct mosaic_program *p,vxl_u64 offset,vxl_u64 bytes){
 if(!p||!bytes)return 0;
 if(offset<=p->flat_bytes&&bytes<=p->flat_bytes-offset)return 1;
 if(p->rodata_bytes&&p->rodata_gpu==0x80110000u&&offset>=0x110000u&&offset-0x110000u<=p->rodata_bytes&&bytes<=p->rodata_bytes-(offset-0x110000u))return 1;
 return 0;
}
int mosaic_host_init(struct mosaic_ctx *c,struct mosaic_host_state *h,struct vxl_engine *e,const vxl_u8 expected[32]){
 if(!c||!h||!e||!expected||!nz(expected,32))return -EINVAL;
 if(e->profile_id!=MOSAIC_PROFILE)return -EXDEV;
 if(e->hunet||e->s8||e->owner||e->opened||e->quarantined||e->job.pinned)return -EBUSY;
 memset(c,0,sizeof(*c));memset(h,0,sizeof(*h));h->engine=e;memcpy(h->expected_binding_sha,expected,32);c->host=h;return 0;
}
void mosaic_program_catalog_hash(const struct mosaic_program *p,int graph,vxl_u8 out[32]){
 struct vxl_hunet_sha s;if(!out)return;memset(out,0,32);
 if(!p||!p->layouts||!p->layout_count||p->layout_count>32||(graph!=0&&graph!=1))return;
 vxl_hunet_sha_init(&s);word(&s,graph?0x31474d56u:0x314c4d56u);word(&s,1);word(&s,p->layout_count);word(&s,graph?32:296);
 if(graph)vxl_hunet_sha_update(&s,p->graph_policy_sha,32);
 for(unsigned i=0;i<p->layout_count;++i){
  const struct mosaic_catalog_layout *entry=&p->layouts[i];const struct mosaic_layout *l=&entry->layout;
  word(&s,l->width);word(&s,l->height);
  if(!graph){for(unsigned j=0;j<MOSAIC_ROLES;++j){wide(&s,l->region[j].address);wide(&s,l->region[j].bytes);word(&s,l->region[j].guard);word(&s,l->region[j].readonly);}wide(&s,l->end);}
  vxl_hunet_sha_update(&s,entry->graph_sha,32);
  if(!graph)vxl_hunet_sha_update(&s,entry->layout_sha,32);
 }
 vxl_hunet_sha_final(&s,out);
}
void mosaic_program_runtime_hash(const struct mosaic_program *p,vxl_u8 out[32]){
 struct vxl_hunet_sha s;if(!out)return;memset(out,0,32);
 if(!p||!p->mutable_ranges||!p->mutable_count||p->mutable_count>64)return;
 vxl_u32 fields[8]={p->crt_base,p->crt_bytes,p->tls_base,p->tbss_bytes,p->tbss_stride,p->tbss_align,p->stack_stride,p->static_stack_bytes};
 vxl_hunet_sha_init(&s);word(&s,0x31524d56u);word(&s,1);word(&s,p->runtime_bytes);word(&s,p->mutable_count);
 for(unsigned i=0;i<8;++i){word(&s,fields[i]);}
 wide(&s,p->stack_base);wide(&s,p->stack_bytes);vxl_hunet_sha_update(&s,p->runtime_deps_sha,32);
 for(unsigned i=0;i<p->mutable_count;++i){word(&s,p->mutable_ranges[i].offset);word(&s,p->mutable_ranges[i].bytes);}
 if(p->rodata_bytes){word(&s,0x31524f4du);word(&s,p->rodata_gpu);word(&s,p->rodata_bytes);vxl_hunet_sha_update(&s,p->rodata_sha,32);}
 vxl_hunet_sha_final(&s,out);
}
int mosaic_program_register(struct mosaic_ctx *c,const struct mosaic_program *p){
 struct mosaic_host_state *h;vxl_u8 digest[32];vxl_u32 end=4;
 if(!c||!c->host||!c->host->engine||!p)return -EINVAL;
 h=c->host;if(h->engine->profile_id!=MOSAIC_PROFILE)return -EXDEV;
 if(h->nonce_locked||h->loaded||h->frame_active||h->engine->quarantined||h->engine->job.pinned)return -EBUSY;
 if(h->engine->hunet||h->engine->s8)return -EBUSY;
 if(!p->flat||p->flat_bytes<4||p->flat_bytes%64||p->flat_bytes>p->runtime_bytes||!p->runtime_bytes||p->runtime_bytes%64||p->runtime_bytes>0xfffc0||p->entry!=MOSAIC_CODE_GPU)return -EINVAL;
 if(mosaic_program_rodata_check(p))return -ESTALE;
 if(!p->mutable_ranges||!p->mutable_count||p->mutable_count>64||!p->layouts||!p->layout_count||p->layout_count>32||!p->assets||p->assets_bytes<MOSAIC_ASSET_PAYLOAD||p->assets_bytes>MOSAIC_ASSET_MAX||p->assets_bytes%64)return -EINVAL;
 if(p->crt_base!=0x10002000u||p->crt_bytes!=4096||p->stack_base!=0xfff00000ULL||p->stack_bytes!=0x100000ULL||p->stack_stride!=8192||!p->static_stack_bytes||p->static_stack_bytes>=8128)return -EINVAL;
 if(p->tbss_align<16||(p->tbss_align&(p->tbss_align-1))||p->tls_base%p->tbss_align||!p->tbss_bytes||p->tbss_stride<p->tbss_bytes||p->tbss_stride%p->tbss_align||p->tls_base<MOSAIC_CODE_GPU||(vxl_u64)p->tls_base-MOSAIC_CODE_GPU+64ULL*p->tbss_stride>p->runtime_bytes)return -EINVAL;
 for(unsigned i=0;i<p->mutable_count;++i){const struct mosaic_mutable *m=&p->mutable_ranges[i];if(m->offset<end||((m->offset|m->bytes)&3)||!m->bytes||m->offset>=p->runtime_bytes||m->bytes>p->runtime_bytes-m->offset)return -EINVAL;end=m->offset+m->bytes;}
 if(p->binding.magic!=MOSAIC_BINDING_MAGIC||p->binding.version!=1||p->binding.bytes!=512||p->binding.profile!=MOSAIC_PROFILE||p->binding.program!=MOSAIC_PROGRAM||p->binding.abi_major!=1||p->binding.features!=1||p->binding.reserved0||nz(p->binding.reserved,32))return -EINVAL;
 for(unsigned i=0;i<14;++i)if(!nz(p->binding.digests[i],32))return -ENODEV;
 if(!nz(p->graph_policy_sha,32)||!nz(p->runtime_deps_sha,32))return -ENODEV;
 for(unsigned i=0;i<p->layout_count;++i){
  if(mosaic_layout_check(&p->layouts[i].layout)||!nz(p->layouts[i].graph_sha,32)||!nz(p->layouts[i].layout_sha,32))return -EINVAL;
  for(unsigned j=0;j<i;++j)if(!memcmp(p->layouts[i].graph_sha,p->layouts[j].graph_sha,16)||!memcmp(p->layouts[i].layout_sha,p->layouts[j].layout_sha,16))return -EINVAL;
 }
 {const vxl_u32 co[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};vxl_u32 ci=3;
  for(unsigned i=0;i<16;++i){const struct mosaic_layer_shape *shape=&p->assets->layers[i];if(shape->ci!=ci||shape->co!=co[i]||shape->k!=(i==0||i==15?9u:3u)||shape->norm!=(i!=15))return -EINVAL;ci=co[i];}}
 if(memcmp(p->assets->package_sha,p->binding.digests[MOSAIC_ASSETS_SHA],32)||memcmp(p->assets->source_sha[0],p->binding.digests[MOSAIC_MODEL_SHA],32))return -ESTALE;
 vxl_hunet_hash(p->flat,p->flat_bytes,digest);if(memcmp(digest,p->binding.digests[MOSAIC_FLAT_SHA],32))return -ESTALE;
 mosaic_program_catalog_hash(p,0,digest);if(memcmp(digest,p->binding.digests[MOSAIC_LAYOUT_SHA],32))return -ESTALE;
 mosaic_program_catalog_hash(p,1,digest);if(memcmp(digest,p->binding.digests[MOSAIC_GRAPH_SHA],32))return -ESTALE;
 mosaic_program_runtime_hash(p,digest);if(memcmp(digest,p->binding.digests[MOSAIC_RUNTIME_SHA],32))return -ESTALE;
 vxl_hunet_hash(&p->binding,512,digest);if(memcmp(digest,p->binding_sha,32)||memcmp(digest,h->expected_binding_sha,32))return -ESTALE;
 h->program=p;c->layout=&p->layouts[0].layout;return 0;
}

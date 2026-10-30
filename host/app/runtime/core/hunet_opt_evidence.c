#include <vxl/engine.h>
#include <vxl/hunet.h>
static int guard(struct vxl_hunet_ctx *c,vxl_u64 a,int initialize){
 vxl_u8 b[64];unsigned i;int rc;memset(b,0xa5,64);rc=vxl_hunet_io_bytes(c,a,b,64,initialize);if(rc)return rc;
 if(!initialize)for(i=0;i<64;i++)if(b[i]!=0xa5)return -EIO;
 return 0;
}
int vxl_opt_guards(struct vxl_hunet_ctx *c,int initialize){
 static const vxl_u64 addresses[]={0x84040000,0x84140040,0x84141000,0x84141fc0,0x86d0df40,0x86d12e00,0x86d1ee40};unsigned i;int rc;
 for(i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++){rc=guard(c,addresses[i],initialize);if(rc)return rc;}
 if(!initialize&&(c->opt.release->catalog->runtime_flags&4u)&&(c->opt.pending.op==4||c->opt.pending.op==16)){
  rc=guard(c,0x86c4df40,0);if(rc)return rc;return guard(c,0x86c4ef00,0);
 }
 return 0;
}
int vxl_opt_prepare_control(struct vxl_hunet_ctx *c){
 vxl_u8 *page;unsigned i;int rc;
 if(!c||!c->engine||c->program_id!=10)return -EINVAL;
 page=c->engine->scratch;memset(page,0,4096);memset(page,0xa5,64);memset(page+4032,0xa5,64);
 if(c->opt.release->catalog->runtime_flags&2u){rc=vxl_hunet_io_bytes(c,0x84141000,page,4096,1);if(rc)return rc;}
 if((c->opt.release->catalog->runtime_flags&4u)&&(c->opt.pending.op==4||c->opt.pending.op==16)){rc=vxl_hunet_io_bytes(c,0x86c4df40,page,4096,1);if(rc)return rc;}
 if(c->opt.pending.owner_tiles){
  vxl_u64 a=0x84040040,n=(vxl_u64)c->opt.pending.owner_tiles*4,take;
  if(n>1048576)return -EINVAL;
  for(i=0;i<4096;i++)page[i]=0xff;
  while(n){take=n>4096?4096:n;rc=vxl_hunet_io_bytes(c,a,page,take,1);if(rc)return rc;a+=take;n-=take;}
 }
 return 0;
}
static vxl_u32 core_for(vxl_u32 group,vxl_u32 total){
 vxl_u32 active=(total+3)/4,base,extra,offset=0,c;if(active>4)active=4;if(!active)return 0;
 base=total/active;extra=total%active;
 for(c=0;c<active;c++){vxl_u32 n=base+(c<extra);if(group-offset<n)return c+1;offset+=n;}
 return 0;
}
static int matrix_owner(const struct vxl_opt_catalog *cat,const vxl_u32 *words,vxl_u32 tile,vxl_u32 *owner){
 HunetFusedStageV1 s;HunetFusedDescriptorV1 d;vxl_u32 i,total=0,prefix=0;vxl_u64 offset;
 memcpy(&s,words,128);if(!cat->tile_n||!s.descriptor_count||s.descriptor_count>32||tile>=s.gemm_grid_tiles||s.stage_descriptors_gpu<0x86d0df80)return -EINVAL;
 offset=s.stage_descriptors_gpu-0x86d0df80ULL;
 if(offset+(vxl_u64)s.descriptor_count*64>cat->matrix_bytes)return -EINVAL;
 for(i=0;i<s.descriptor_count;i++){vxl_u32 columns;memcpy(&d,cat->matrix_data+offset+i*64,64);columns=d.np/4;if(!columns||!d.mp||d.mp%8)return -EINVAL;total+=(d.mp/8)*((columns+cat->tile_n-1)/cat->tile_n);}
 for(i=0;i<s.descriptor_count;i++){
  vxl_u32 columns,tiles,groups;memcpy(&d,cat->matrix_data+offset+i*64,64);columns=d.np/4;tiles=(d.mp/8)*columns;groups=(columns+cat->tile_n-1)/cat->tile_n;
  if(tile>=d.gemm_grid_base&&tile-d.gemm_grid_base<tiles){vxl_u32 local=tile-d.gemm_grid_base;*owner=core_for(prefix+(local/columns)*groups+(local%columns)/cat->tile_n,total);return *owner?0:-EINVAL;}
  prefix+=(d.mp/8)*groups;
 }
 return -EINVAL;
}
int vxl_opt_owner_expected(const struct vxl_opt_catalog *cat,const struct vxl_opt_meta *m,vxl_u32 tile,vxl_u32 *owner){
 const vxl_u32 *job;vxl_u32 i,offset=0;
 if(!cat||!m||!owner||tile>=m->owner_tiles)return -EINVAL;
 job=vxl_opt_catalog_job(cat,m->first);if(!job)return -ESTALE;
 if(m->op==15)return matrix_owner(cat,job,tile,owner);
 if(m->op==17){
  vxl_u32 header[32];vxl_u64 a=(vxl_u64)(m->block_id-1)*4096;if(!m->block_id||a+job[10]>cat->graph_bytes)return -EINVAL;
  memcpy(header,cat->graph_data+a,128);
  for(i=0;i<header[8];i++){
   vxl_u32 node[24];memcpy(node,cat->graph_data+a+128+i*96,96);
   if(node[0]==15){const vxl_u32 *matrix=vxl_fused_catalog_job(node[2]);vxl_u32 count;if(!matrix)return -ESTALE;count=matrix[17];if(tile-offset<count)return matrix_owner(cat,matrix,tile-offset,owner);offset+=count;}
  }
 }
 return -EINVAL;
}
int vxl_opt_owner_trace(struct vxl_hunet_ctx *c,const struct vxl_opt_meta *m,vxl_u32 counts[4]){
 vxl_u32 i,take,offset=0,words[256],expected;int rc;
 if(!c||!m||!counts||c->program_id!=10||!c->opt.release||!m->owner_tiles||m->owner_tiles>262144)return -EINVAL;
 memset(counts,0,16);
 while(offset<m->owner_tiles){
  take=m->owner_tiles-offset;if(take>256)take=256;
  rc=vxl_hunet_io_bytes(c,0x84040040ULL+(vxl_u64)offset*4,words,take*4,0);if(rc)return rc;
  for(i=0;i<take;i++){rc=vxl_opt_owner_expected(c->opt.release->catalog,m,offset+i,&expected);if(rc)return rc;if(words[i]!=expected)return -EIO;counts[expected-1]++;}
  offset+=take;
 }
 return 0;
}

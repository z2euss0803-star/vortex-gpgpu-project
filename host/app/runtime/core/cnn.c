#include <vxl/engine.h>
#include <vxl/cnn.h>
int vxl_cnn_args_check(struct vxl_engine *e,vxl_u32 id,const vxl_u32 words[16]) {
 static const vxl_u32 counts[4][4]={{64,18,2,72},{72,0,0,72},{72,0,0,18},{18,36,2,2}};
 const struct vxl_object *selected[4]={0};unsigned i,j,k;
 if(!e||!words||id<VXL_CNN_CONV||id>VXL_CNN_FC)return -EINVAL;
 if(words[0]!=VXL_CNN_MAGIC||words[1]!=VXL_CNN_ARGS_BYTES||words[2]!=VXL_CNN_VERSION||words[3]!=id)return -EINVAL;
 for(i=12;i<16;i++)if(words[i])return -EINVAL;
 for(i=0;i<4;i++){
  vxl_u64 total=128+((counts[id-2][i]*4+63)&~63U);
  if(words[8+i]!=counts[id-2][i])return -EINVAL;
  if(!counts[id-2][i]){if(words[4+i])return -EINVAL;continue;}
  if(words[4+i]&63)return -EINVAL;
  for(j=0;j<VXL_MAX_OBJECTS;j++){
   const struct vxl_object *o=&e->objects[j];
   if(!o->handle||o->domain!=VXL_PL||o->purpose!=VXL_DATA||o->gpu<0x80000000U)continue;
   if((vxl_u64)o->gpu+64!=words[4+i]||o->length!=total||o->allocated!=total)continue;
   if((vxl_u64)o->gpu+total>0x100000000ULL)continue;
   selected[i]=o;break;
  }
  if(!selected[i])return -ERANGE;
  for(k=0;k<i;k++)if(selected[k]){
   const struct vxl_object *a=selected[i],*b=selected[k];
   if(a==b||((vxl_u64)a->gpu<b->gpu+b->length&&(vxl_u64)b->gpu<a->gpu+a->length))return -ERANGE;
  }
 }
 return 0;
}
/* Shared production preflight. No job ID/START allocation occurs here. */
int vxl_controlled_prepare(struct vxl_engine *e,struct vxl_object *code,vxl_u64 co,
 struct vxl_object *args,vxl_u64 ao,vxl_u32 id,struct vxl_prepared *out) {
 int rc;vxl_u8 block[16];
 if(id<1||id>5)return -EACCES;
 if(id!=1&&co)return -EINVAL;
 if(co||code->domain!=VXL_PS||code->purpose!=VXL_CODE||!code->storage)return -EACCES;
 rc=vxl_program_check_id(code->storage,code->length,code->gpu,id);if(rc)return rc;
 memset(out,0,sizeof(*out));out->program_id=id;out->code_gpu=code->gpu;out->args_gpu=args->gpu+(vxl_u32)ao;
 if(id!=1){
  if(ao||args->domain!=VXL_PS||args->purpose!=VXL_ARGS||args->gpu!=VXL_CNN_ARGS_GPU||args->length!=64||!args->storage)return -EINVAL;
  memcpy(out->args_words,args->storage,64);out->args_word_count=16;
  return vxl_cnn_args_check(e,id,out->args_words);
 }
 if(vxl_range_check(args->length,ao,12)||(ao&3))return -EINVAL;
 if(args->domain==VXL_PS){if(!args->storage)return -EINVAL;memcpy(out->args_words,(vxl_u8 *)args->storage+ao,12);}
 else {
  if((ao&15)||vxl_range_check(args->allocated,ao,16))return -EINVAL;
  rc=e->ops->transfer(e->context,args,ao,block,16,0);if(rc)return rc;
  memcpy(out->args_words,block,12);
 }
 out->args_word_count=3;return vxl_diagnostic_args(e,out->args_words);
}

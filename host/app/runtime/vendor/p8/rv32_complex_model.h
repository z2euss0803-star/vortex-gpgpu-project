/* Native instruction/shared-PC model, no caches/timing/WSPAWN/RTL claim. */
#ifndef RV32_COMPLEX_MODEL_H
#define RV32_COMPLEX_MODEL_H
static unsigned rv32_error;
static uint8_t rv32_written[0xa500000u/4u];
static uint32_t rv32_writes;
static int32_t rv32_sx(uint32_t x,unsigned bits){uint32_t sign=1u<<(bits-1);return (int32_t)((x&(sign-1))-(x&sign));}
static uint32_t rv32_load(uint32_t *mem,uint32_t a,unsigned kind){
 int ok=(a>=0x10010000u && a<0x10010040u) || (kind==0 && a>=CM_RAW && a<CM_RAW+32u*CM_ACTIVE_SLOTS) ||
 (kind==1 && ((a>=CM_PRETRI && a<CM_PRETRI+cm_sizes[8])||(a>=V3_TEX && a<V3_TEX+cm_sizes[2])||(a>=CM_BG && a<CM_BG+cm_sizes[9])||(a>=CM_DIR && a<CM_DIR+cm_sizes[6])||(a>=CM_REFS && a<CM_REFS+4u*cm_active_refs)));
 if(!ok || (a&3u)){rv32_error=1;return 0;}return mem[(a-0x10000000u)/4u];
}
static void rv32_store(uint32_t *mem,uint32_t a,uint32_t v,unsigned kind){
 int ok=(kind==0 && a>=CM_PROJECTED && a<CM_PROJECTED+32u*CM_ACTIVE_SLOTS) || (kind==1 && ((a>=FB64_BASE && a<FB64_BASE+cm_sizes[0])||(a>=V3_DEPTH && a<V3_DEPTH+cm_sizes[1])));
 if(!ok || (a&3u)){rv32_error=2;return;}
 uint32_t index=(a-0x10000000u)/4u;if(rv32_written[index]){rv32_error=3;return;}
 rv32_written[index]=1;rv32_writes++;mem[index]=v;
}
static int rv32_model_run(const uint32_t *code,uint32_t words,uint32_t *mem,unsigned kind,uint64_t *insns){
 rv32_error=rv32_writes=0;memset(rv32_written,0,sizeof(rv32_written));
 for(unsigned warp=0;warp<16u;warp++){
  uint32_t r[4][32]={{0}},pc=24u;unsigned retired=0,fenced=0;
  for(uint32_t step=0;step<30000000u;step++){
   if(pc&3u || pc/4u>=words){rv32_error=4;break;}
   uint32_t word=code[pc/4u],op=word&127u,rd=(word>>7)&31u,f3=(word>>12)&7u,rs1=(word>>15)&31u,rs2=(word>>20)&31u,f7=word>>25,next=pc+4u;
   *insns+=4u;
   if(op==0x0bu && f3==0u){if(!fenced)rv32_error=5;retired=1;break;}
   if(op==0x0fu){fenced=1;pc=next;continue;}
   if(op==0x63u){
    uint32_t imm=((word>>31)<<12)|(((word>>7)&1u)<<11)|(((word>>25)&63u)<<5)|(((word>>8)&15u)<<1);unsigned taken[4];
    for(unsigned lane=0;lane<4u;lane++){
     if(f3==0u)taken[lane]=r[lane][rs1]==r[lane][rs2];
     else if(f3==7u)taken[lane]=r[lane][rs1]>=r[lane][rs2];
     else if(f3==4u)taken[lane]=(int32_t)r[lane][rs1]<(int32_t)r[lane][rs2];
     else{rv32_error=6;break;}
    }
    if(rv32_error)break;
    for(unsigned lane=0;lane<3u;lane++)if(taken[lane]!=taken[3])rv32_error=7;
    if(rv32_error)break;
    if(taken[3])next=pc+(uint32_t)rv32_sx(imm,13);
    pc=next;continue;
   }
   if(op==0x6fu){
    uint32_t imm=((word>>31)<<20)|(((word>>12)&255u)<<12)|(((word>>20)&1u)<<11)|(((word>>21)&1023u)<<1);
    for(unsigned lane=0;lane<4u;lane++)if(rd)r[lane][rd]=next;
    next=pc+(uint32_t)rv32_sx(imm,21);pc=next;continue;
   }
   for(unsigned lane=0;lane<4u;lane++){
    uint32_t *q=r[lane],a=q[rs1],b=q[rs2],value=0,write=1;
    if(op==0x73u && f3==2u){uint32_t csr=word>>20;if(csr==0x340u)value=0x10010000u;else if(csr==0xf14u)value=4u*warp+lane;else rv32_error=8;}
    else if(op==0x37u)value=word&0xfffff000u;
    else if(op==0x03u && f3==2u)value=rv32_load(mem,a+(uint32_t)rv32_sx(word>>20,12),kind);
    else if(op==0x23u && f3==2u){uint32_t imm=((word>>25)<<5)|((word>>7)&31u);rv32_store(mem,a+(uint32_t)rv32_sx(imm,12),b,kind);write=0;}
    else if(op==0x13u){uint32_t sh=(word>>20)&31u;
     if(f3==0u)value=a+(uint32_t)rv32_sx(word>>20,12);
     else if(f3==7u)value=a&(uint32_t)rv32_sx(word>>20,12);
     else if(f3==1u)value=a<<sh;
     else if(f3==5u)value=(word&(1u<<30))?(uint32_t)((int32_t)a>>sh):a>>sh;
     else rv32_error=9;
    }else if(op==0x33u){
     if(f7==1u && f3==0u)value=(uint32_t)((uint64_t)a*b);
     else if(f7==1u && f3==5u)value=b?a/b:UINT32_MAX;
     else if(f7==32u && f3==0u)value=a-b;
     else if(!f7 && f3==0u)value=a+b;
     else if(!f7 && f3==3u)value=a<b;
     else if(!f7 && f3==4u)value=a^b;
     else if(!f7 && f3==6u)value=a|b;
     else if(!f7 && f3==7u)value=a&b;
     else rv32_error=10;
    }else rv32_error=11;
    if(rv32_error)break;
    if(write && rd)q[rd]=value;
    q[0]=0;
   }
   if(rv32_error)break;
   pc=next;
  }
  if(rv32_error)return (int)rv32_error;
  if(!retired)return 12;
 }
 return rv32_writes==(kind==0?8u*CM_ACTIVE_SLOTS:2u*CM_PIXELS)?0:13;
}
#endif

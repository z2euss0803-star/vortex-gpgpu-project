from dataclasses import dataclass
from pathlib import Path
import struct
WIDTH,HEIGHT=1280,720
IMAGE_BYTES=WIDTH*HEIGHT*4
BG=(7,16,30)
SLOT_BASE=0x20000000
SLOT_SIZE=0x400000
SLOT_COUNT=33
ADDRESSES=[SLOT_BASE+i*SLOT_SIZE+64 for i in range(SLOT_COUNT)]
ACTIVE,STAGE=ADDRESSES[:2]
SMOOTH,CORE,HALO,MEAN=ADDRESSES[29:33]
CACHES=[ADDRESSES[i] for i in (2,*range(11,17),3,4,*range(17,23),5,*range(23,29))]
SCRATCH=ADDRESSES[6:10]
TABLE=ADDRESSES[10]
PAL_Y=TABLE
PAL_8=TABLE+0x1c00
BAYER_ADDR=TABLE+0x1d00
GAUSS7=TABLE+0x1d40
GAUSS9=TABLE+0x1d80
THEME=TABLE+0x1e00
XDOG_TABLE=TABLE+0x2000
OPS=('gray','blur3','sobel_poster','gaussian_h','gaussian_v','xdog_color','neon_core','neon_combine','pixel_mean','pixel_bayer')
BAYER=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5))
@dataclass(frozen=True)
class Rect:
    x:int;y:int;w:int;h:int
@dataclass(frozen=True)
class Params:
    style:int;preset:int=0;block:int=0
@dataclass(frozen=True)
class PreparedImage:
    rgba:bytes;rect:Rect;source_sha256:str
def validate(w,h,rect,p):
    if (w,h) not in ((1280,720),(256,144)):raise ValueError('unsupported canvas')
    if min(rect.x,rect.y)<0 or min(rect.w,rect.h)<1 or rect.x+rect.w>w or rect.y+rect.h>h:raise ValueError('invalid rect')
    if p.style not in range(5) or p.preset not in range(7):raise ValueError('invalid style or preset')
    if p.style in (0,2) and p.preset:raise ValueError('unsupported preset')
    if (p.style==4 and p.block not in (2,4,8)) or (p.style!=4 and p.block):raise ValueError('invalid block')
def write_header():
    p=Path(__file__).with_name('image_contract.h')
    defs={'IMAGE_SLOT_COUNT':SLOT_COUNT,'IMAGE_SMOOTH':SMOOTH,'IMAGE_CORE':CORE,'IMAGE_HALO':HALO,'IMAGE_MEAN':MEAN,'IMAGE_WIDTH':WIDTH,'IMAGE_HEIGHT':HEIGHT,'IMAGE_BYTES':IMAGE_BYTES,'IMAGE_SLOT_BASE':SLOT_BASE,'IMAGE_SLOT_SIZE':SLOT_SIZE,'IMAGE_ACTIVE':ACTIVE,'IMAGE_STAGE':STAGE,'IMAGE_TABLE':TABLE,'IMAGE_PAL_Y':PAL_Y,'IMAGE_PAL_8':PAL_8,'IMAGE_BAYER':BAYER_ADDR,'IMAGE_GAUSS7':GAUSS7,'IMAGE_GAUSS9':GAUSS9,'IMAGE_XDOG_TABLE':XDOG_TABLE,'IMAGE_THEME':THEME,'IMAGE_BG':0xff1e1007}
    s='#ifndef IMAGE_CONTRACT_H\n#define IMAGE_CONTRACT_H\n#include <stdint.h>\n'
    s+=''.join(f'#define {k} UINT32_C(0x{v:08x})\n' for k,v in defs.items())
    s+='typedef struct {uint32_t x,y,w,h;} image_rect;\ntypedef struct {uint32_t style,preset,block;} image_params;\n'
    s+='typedef struct {uint32_t src0,src1,src2,dst,width,height,rect_x,rect_y,rect_w,rect_h,table,param0,param1,param2,param3,flags;} image_job;\n'
    s+='enum image_op {'+','.join('IMAGE_'+x.upper()+'='+str(i) for i,x in enumerate(OPS))+'};\n'
    s+='#define IMAGE_GRAY_Q8 1u\n_Static_assert(sizeof(image_job)==64,"image job ABI");\n#endif\n'
    p.write_text(s,encoding='utf-8')
if __name__=='__main__':write_header()

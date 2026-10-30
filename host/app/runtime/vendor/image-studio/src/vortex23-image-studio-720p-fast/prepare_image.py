from pathlib import Path
import hashlib
import numpy as np
from PIL import Image,ImageOps
from contract import WIDTH,HEIGHT,BG,Rect,PreparedImage
def prepare_image(path):
    p=Path(path);digest=hashlib.sha256(p.read_bytes()).hexdigest()
    with Image.open(p) as source:
        image=ImageOps.exif_transpose(source).convert('RGBA')
        rgba=np.array(image,dtype=np.uint32);alpha=rgba[:,:,3:4]
        rgb=((rgba[:,:,:3]*alpha+np.array(BG,dtype=np.uint32)*(255-alpha)+127)//255).astype(np.uint8)
        sw,sh=image.size;scale=min(WIDTH/sw,HEIGHT/sh)
        w=max(1,min(WIDTH,int(sw*scale+0.5)));h=max(1,min(HEIGHT,int(sh*scale+0.5)))
        small=np.array(Image.fromarray(rgb).resize((w,h),Image.Resampling.LANCZOS))
    x,y=(WIDTH-w)//2,(HEIGHT-h)//2
    canvas=np.empty((HEIGHT,WIDTH,4),np.uint8);canvas[:]=[*BG,255];canvas[y:y+h,x:x+w,:3]=small
    return PreparedImage(canvas.tobytes(),Rect(x,y,w,h),digest)

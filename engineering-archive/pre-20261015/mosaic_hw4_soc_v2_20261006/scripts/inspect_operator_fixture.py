from pathlib import Path
import collections,json,struct
n=Path(__file__).resolve().parents[2]/'fast_style_vortex_20261002/build/xsim_ops_axi_order_v5'
mem={}
for line in (n/'memory.hex').read_text().splitlines():
    key,value=line.split();mem[int(key,16)*4]=int(value,16)
ranges=[]
for a in sorted(mem):
    if ranges and a==ranges[-1][1]:ranges[-1][1]+=4
    else:ranges.append([a,a+4])
print('RANGES',[(hex(a),hex(b),b-a) for a,b in ranges])
for i in range(10):
    a=0x10010000+256*i;b=b''.join(struct.pack('<I',mem[a+4*j]) for j in range(32))
    q=struct.unpack('<8Q16I',b)
    print('JOB',i,[hex(x) for x in q[:8]],q[8:])
image=Path(__file__).resolve().parents[2]/'mosaic_board_v1_20261004/assets/gpu_image.bin'
b=image.read_bytes();errors=[(hex(0x80000000+i),mem.get(0x80000000+i),struct.unpack_from('<I',b,i)[0]) for i in range(0,len(b),4) if mem.get(0x80000000+i,0)!=struct.unpack_from('<I',b,i)[0]]
print('IMAGE_DIFFS',len(errors),errors[:6])
print('GUARDS_HEAD',(n/'guards.hex').read_text().splitlines()[:6]);print('EXPECTED_HEAD',(n/'expected.hex').read_text().splitlines()[:4])

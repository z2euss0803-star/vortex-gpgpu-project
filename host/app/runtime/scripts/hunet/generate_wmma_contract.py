import json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
cases=json.loads((root/'generated/hunet/wmma.json').read_text())
lines=['struct vxl_hunet_wmma_region {vxl_u64 address;vxl_u32 bytes;vxl_u8 sha[32];};',
       'static const struct vxl_hunet_wmma_region vxl_hunet_wmma_regions[2][3]={']
for c in cases:
 lines+=['{']
 for name,a in [('matrix.bin',0x88000040),('a.f16',0x88000100),('b.f16',0x88002000)]:
  f=c['files'][name];lines+=['{0x%xULL,%du,{%s}},'%(a,f['bytes'],','.join('0x%02x'%v for v in bytes.fromhex(f['sha256'])))]
 lines+=['},']
lines+=['};']
(root/'generated/hunet/wmma_contract.h').write_text('\n'.join(lines)+'\n',newline='\n')

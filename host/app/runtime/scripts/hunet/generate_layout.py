"""Generate only the frozen v5 owned slots/guards, never expand their span."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def generate(root=ROOT):
 m=json.loads((root/'contracts/hunet_v5.json').read_text(encoding='utf-8'))
 slots=[r for r in m['graph']['layout'] if r.get('guard')]
 if len(slots)!=106:raise ValueError('GUARD_COUNT')
 lines=['#ifndef VXL_HUNET_LAYOUT_GENERATED_H','#define VXL_HUNET_LAYOUT_GENERATED_H',
        'struct vxl_hunet_slot { vxl_u64 start,bytes; vxl_u32 writable; };',
        'static const struct vxl_hunet_slot vxl_hunet_slots[106]={']
 for s in slots:
  if s['start']%64 or s['bytes']%64:raise ValueError('SLOT_ALIGNMENT')
  lines.append('{0x%xULL,%du,%du},'%(s['start'],s['bytes'],s['name'] not in ['input','matrix_descriptors']))
 lines+=['};','#endif']
 dest=root/'generated/hunet/layout.h';dest.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
 return slots
if __name__=='__main__':generate()

import json
from pathlib import Path
r=Path(__file__).resolve().parents[2]
c=json.loads((r/'generated/hunet/catalog.json').read_text())
lines=['struct vxl_hunet_stage {vxl_u32 last_sequence,gpu,bytes;};','static const struct vxl_hunet_stage vxl_hunet_stages[100]={']
lines+=['{%du,0x%xu,%du},'%(s['last_sequence'],s['gpu'],s['bytes']) for s in c['stages']];lines+=['};']
(r/'generated/hunet/stages.h').write_text('\n'.join(lines)+'\n',newline='\n')

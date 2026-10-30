"""Compact the independently generated native diagnostic images for A53."""
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
cases=json.loads((r/'kernel/extended_cases.json').read_text())
code_words=max(c['kernel_words'] for c in cases)
fields=[('code',0,code_words),('args',0x10000,7),('a',0x20000,128),
        ('b',0x20400,64),('u',0x23000,16),('v',0x23100,16),
        ('texture',0x24000,40),('gold',None,144)]
assert all(c['kernel_words']==code_words for c in cases)
text='#ifndef EXTENDED_VECTORS_H\n#define EXTENDED_VECTORS_H\n#include <stdint.h>\n'
text+='typedef struct { uint32_t mode,expected_error;\n'
for name,addr,n in fields:text+=f' uint32_t {name}[{n}];\n'
text+='} extended_vector;\nstatic const extended_vector ext_vectors[10] = {\n'
for c in cases:
 image=[int(s,16) for s in (r/f"kernel/image_{c['case']}.hex").read_text().split()]
 gold=[int(s,16) for s in (r/f"kernel/gold_{c['case']}.hex").read_text().split()]
 text+=f"{{ .mode={c['mode']}U, .expected_error={c['expected_error']}U,\n"
 for name,addr,n in fields:
  values=gold if addr is None else image[addr//4:addr//4+n]
  assert len(values)==n
  text+=f' .{name}={{'+','.join(f'0x{v:08x}U' for v in values)+'},\n'
 text+='},\n'
text+='};\n#endif\n'
(r/'sw/extended_vectors.h').write_text(text)
print('HOST_VECTORS_READY',len(cases))

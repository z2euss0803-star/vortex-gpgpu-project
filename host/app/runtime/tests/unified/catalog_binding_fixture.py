"""Two individually valid C entries with one ambiguous user program key."""
import pathlib,struct,sys
from scripts.unified.build_catalog import build_catalog
raw=pathlib.Path(sys.argv[1]).read_bytes();fields=list(struct.unpack_from('<16I',raw,64));digests=[raw[128+32*i:160+32*i] for i in range(8)];schema=raw[fields[11]:fields[11]+fields[12]]
entry=dict(fields=fields,digests=digests,schema=schema)
with pathlib.Path(sys.argv[2]).open('xb') as f:f.write(build_catalog([entry,entry]))

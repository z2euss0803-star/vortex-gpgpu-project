import pathlib,sys,struct,json,hashlib
from scripts.unified.build_catalog import build_catalog
raw=pathlib.Path(sys.argv[1]).read_bytes();fields=list(struct.unpack_from('<16I',raw,64));digests=[raw[128+32*i:160+32*i] for i in range(8)];schema=raw[fields[11]:fields[11]+fields[12]]
p=json.loads(pathlib.Path('contracts/unified/providers.json').read_text())['providers'][0]
for i,key in [(3,'hardware_contract'),(4,'bit_sha256'),(5,'xsa_sha256')]:digests[i]=bytes.fromhex(p[key])
digests[7]=digests[0] # exact CODE already authenticated by the input catalog
with pathlib.Path(sys.argv[2]).open('xb') as f:f.write(build_catalog([dict(fields=fields,digests=digests,schema=schema)]))

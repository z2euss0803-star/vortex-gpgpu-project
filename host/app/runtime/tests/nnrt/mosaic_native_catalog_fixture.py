"""Actual native Mosaic bytes, synthetic host-provider identity; no device IO."""
import hashlib,json,pathlib,sys
from scripts.unified.build_catalog import build_catalog
from scripts.unified.mosaic_program import selected_directory
def main():
 if len(sys.argv) not in (2,3) or (len(sys.argv)==3 and sys.argv[2]!='legacy-v1'):raise ValueError('explicit legacy-v1 selector or default current v2 required')
 root=pathlib.Path('data/nnrt/programs/mosaic-int16-v1') if len(sys.argv)==3 else selected_directory('.');family=json.loads((root/'family.json').read_text());code=(root/'code.bin').read_bytes();ro=(root/'rodata.bin').read_bytes();schema=(root/'args-schema.bin').read_bytes();sha=lambda b:hashlib.sha256(b).digest()
 for name,raw in [('code',code),('rodata',ro),('schema',schema)]:assert sha(raw).hex()==family[name+'_sha256']
 assert len(code)==65536 and len(ro)==6307840 and len(schema)==1536
 fields=[1,1,65568,0,0,len(code),len(code),len(ro),0x80000000,0x80100040,128,0,0,0,1,0]
 digests=[sha(code),sha(ro),sha(schema),bytes([1])*32,bytes([2])*32,bytes([3])*32,bytes(32),sha(b'host-controlled-Mosaic-no-hardware-admission')]
 raw=build_catalog([dict(fields=fields,digests=digests,schema=schema)]);p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('xb') as f:f.write(raw)
 print(json.dumps(dict(scope='ACTUAL_CODE_RO_SCHEMA_HOST_PROVIDER_NOT_BOARD',sha256=sha(raw).hex(),gpu_raw_cycles=None)))
if __name__=='__main__':main()

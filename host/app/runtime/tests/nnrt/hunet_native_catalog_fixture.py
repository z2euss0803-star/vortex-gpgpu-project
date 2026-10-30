"""Actual frozen CODE/schema in a HOST provider fixture. Never GPU execution."""
import hashlib,json,pathlib,sys
from scripts.unified.build_catalog import build_catalog
from scripts.unified.hunet_program import selected_directory

def main():
 root=selected_directory('.');family=json.loads((root/'family.json').read_text());code=(root/'code.bin').read_bytes();schema=(root/'args-schema.bin').read_bytes();sha=lambda b:hashlib.sha256(b).digest()
 assert len(code)==65536 and sha(code).hex()==family['code_sha256']
 assert sha(schema).hex()==family['schema_sha256']=='79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89'
 fields=[1,1,65552,0,0,len(code),len(code),0,0x80000000,0x80100040,128,0,0,0,1,0]
 digests=[sha(code),sha(b''),sha(schema),bytes([1])*32,bytes([2])*32,bytes([3])*32,bytes(32),sha(b'host-controlled-HUNet-load-not-board')]
 raw=build_catalog([dict(fields=fields,digests=digests,schema=schema)]);p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('xb') as f:f.write(raw)
 print(json.dumps(dict(scope='FROZEN_CODE_SCHEMA_HOST_CATALOG_NOT_HW_ADMISSION',sha256=sha(raw).hex(),code_sha256=sha(code).hex(),schema_sha256=sha(schema).hex(),gpu_raw_cycles=None)))
if __name__=='__main__':main()

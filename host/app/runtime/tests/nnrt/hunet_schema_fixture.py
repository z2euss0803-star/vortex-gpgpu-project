"""Driver protocol fixture only: dummy code is NOT a GPU program/result."""
import hashlib,pathlib,struct,sys
from scripts.unified.hunet_schema import make_schema
def main():
 sha=lambda b:hashlib.sha256(b).digest();schema=make_schema();fields=(len(schema)-64)//64 if schema else 0
 e=struct.pack('<16I',1,1,0x10010,0,0,64,64,0,0x80000000,0x80100040,128,576,len(schema),fields,1,0)+sha(bytes(64))+sha(b'')+sha(schema)+bytes([1])*32+bytes([2])*32+bytes([3])*32+bytes(32)+sha(b'hunet-schema-fixture')+bytes(192)
 eid=sha(e+schema);e=e[:256]+eid+e[288:];payload=e+schema
 raw=struct.pack('<4I',0x434c5856,1,64+len(payload),1)+sha(payload)+struct.pack('<4I',512,0,0,0)+payload
 p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
if __name__=='__main__':main()

"""Independent schema2 scalar-sum fixture; zeros are NOT a runnable GPU kernel."""
import pathlib,struct,hashlib,sys
sha=lambda b:hashlib.sha256(b).digest()
fields=[struct.pack('<16I',1,4*i,0,0,0,0,0,0,0,0,0,0,0xffffffff,0,0,0) for i in range(4)]
fields.append(struct.pack('<16I',4,0,1,4,8,0,0,0,0,0,0,0,0,0,0,0))
schema=struct.pack('<16I',0x534c5856,2,64+64*len(fields),16,len(fields),0x10000000,0,0,0,0,0,1,0,0,0,0)+b''.join(fields)
entry=struct.pack('<16I',1,4,0x10001,0,0,64,64,0,0x10000000,0x10010000,16,576,len(schema),len(fields),1,0)+sha(bytes(64))+sha(b'')+sha(schema)+bytes([1])*32+bytes([2])*32+bytes([3])*32+bytes(32)+sha(b'generic-sum-fixture')+bytes(192)
eid=sha(entry+schema);entry=entry[:256]+eid+entry[288:];payload=entry+schema
raw=struct.pack('<4I',0x434c5856,1,64+len(payload),1)+sha(payload)+struct.pack('<4I',512,0,0,0)+payload
p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)

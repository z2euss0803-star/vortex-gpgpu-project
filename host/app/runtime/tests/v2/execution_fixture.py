import runpy,pathlib,sys,struct,hashlib
runpy.run_path('tests/v2/catalog_fixture.py',run_name='__main__')
p=pathlib.Path(sys.argv[1]);b=bytearray(p.read_bytes())
for offset,value in [(64+160,'1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d'),(64+192,'94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1'),(64+224,'02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94')]:b[offset:offset+32]=bytes.fromhex(value)
struct.pack_into('<4I',b,576+28,0x10002000,1024,0x100ff000,4096)
s=bytes(b[576:]);b[192:224]=hashlib.sha256(s).digest();b[320:352]=bytes(32);b[320:352]=hashlib.sha256(bytes(b[64:576])+s).digest();b[16:48]=hashlib.sha256(b[64:]).digest();p.write_bytes(b)
if 'p8' in p.name:
 struct.pack_into('<3I',b,64,2,5,10);struct.pack_into('<I',b,64+56,2);struct.pack_into('<I',b,576+44,2)
 for off,value in [(224,'22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012'),(256,'faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc'),(288,'08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536')]:b[off:off+32]=bytes.fromhex(value)
 s=bytes(b[576:]);b[192:224]=hashlib.sha256(s).digest();b[320:352]=bytes(32);b[320:352]=hashlib.sha256(bytes(b[64:576])+s).digest();b[16:48]=hashlib.sha256(b[64:]).digest();p.write_bytes(b)

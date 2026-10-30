import pathlib,runpy,sys,struct,hashlib
runpy.run_path('tests/v2/catalog_fixture.py',run_name='__main__')
p=pathlib.Path(sys.argv[1]);raw=bytearray(p.read_bytes())
struct.pack_into('<4I',raw,576+7*4,0x10002000,64,0x100ff000,64)
schema=bytes(raw[576:]);raw[64+128:64+160]=hashlib.sha256(schema).digest();raw[320:352]=bytes(32)
raw[320:352]=hashlib.sha256(bytes(raw[64:576])+schema).digest();raw[16:48]=hashlib.sha256(raw[64:]).digest();p.write_bytes(raw)

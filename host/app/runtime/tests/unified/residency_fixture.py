import pathlib,sys,struct
from scripts.unified.build_catalog import build_catalog
raw=pathlib.Path(sys.argv[1]).read_bytes();f=list(struct.unpack_from('<16I',raw,64));d=[raw[128+i*32:160+i*32] for i in range(8)];s=raw[f[11]:f[11]+f[12]]
a=dict(fields=f,digests=d,schema=s);g=f[:];g[2]=65553;dd=d[:];dd[0]=bytes([0xab])*32
b=dict(fields=g,digests=dd,schema=s);pathlib.Path(sys.argv[2]).write_bytes(build_catalog([a,b]))

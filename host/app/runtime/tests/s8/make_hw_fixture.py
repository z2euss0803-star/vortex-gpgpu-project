"""Generate TEST_ONLY CPU memory banks; not device execution results."""
from pathlib import Path
import struct
from scripts.s8.layout import initial_image,guard_byte,FIXED

def generate(out):
    out=Path(out);out.parent.mkdir(parents=True,exist_ok=True)
    blob=bytearray(b'S8HWFIX1')
    names=('CODE','ARGS','A','B','OUT','IDENT','CTRL','STACK')
    for region,name in enumerate(names,1):
        begin,end=FIXED['regions'][name]
        if region in (3,4,5):data=b''.join(initial_image(region,s,0) for s in range(16))
        elif region==8:data=b''.join(initial_image(8,h) for h in range(63,-1,-1))+bytes(guard_byte(8,0,1,j) for j in range(64))
        else:data=initial_image(region,0)
        if region==6:
            data=bytearray(data)
            for hart in range(64):
                struct.pack_into('<8I',data,64+hart*32,hart//16,(hart%16)//4,hart%4,hart,0xffff0000-hart*8192-256,0x80002000+hart*16,0x53540000^hart,0x544c0000^hart)
            for core in range(4):struct.pack_into('<8I',data,0x840+core*32,0x31433853,1,core,2,1,0,16,0x454e4f44)
            struct.pack_into('<8I',data,0x8c0,0x31473853,1,64,2,1,15,64,0x454e4f44)
            data[0x8e0]=1;data[0x8f0]=0x55
        assert len(data)==end-begin
        blob+=struct.pack('<QI',begin,len(data))+data
    crt=bytearray(initial_image(9,0))
    for core in range(4):crt[8+core*256:12+core*256]=bytes(4)
    blob+=struct.pack('<QI',0x10002000,4096)+crt
    out.write_bytes(blob)

if __name__=='__main__':generate(Path('build/s8/hw-fixture.bin'))

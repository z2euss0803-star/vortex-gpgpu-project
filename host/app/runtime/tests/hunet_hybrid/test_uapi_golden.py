import pathlib,struct,sys
want=struct.pack('<4I16s4I4Q32s16s',0x42435948,1,128,0,b'\x0a'+b'\0'*15,
                 2,296,8,1,0x1122334455667788,0x2233445566778899,1572864,1572864,
                 b'\xa5'+b'\0'*31,b'\0'*16)
assert pathlib.Path(sys.argv[1]).read_bytes()==want
print('HYBRID_C_PYTHON_GOLDEN_128_PASS')

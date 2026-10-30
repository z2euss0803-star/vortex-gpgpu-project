import pathlib,struct,sys
p=pathlib.Path(sys.argv[1])/'hunet-uapi-golden.bin'
want=struct.pack('<4I4Q32s48s',0x45534c48,1,128,0,0x1122334455667788,2,3,4,b'\xa5'+b'\0'*31,b'\0'*48)
assert p.read_bytes()==want,'C_PYTHON_UAPI_WIRE_MISMATCH'
print('C_PYTHON_GOLDEN_WIRE_PASS')

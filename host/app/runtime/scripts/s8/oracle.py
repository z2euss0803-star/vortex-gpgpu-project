"""Independent Python integer reference. No device output is trusted as oracle."""
from pathlib import Path
import hashlib,json
from scripts.s8.catalog import VECTOR_SHA

def matmul(a: list,b: list) -> list:
    k=len(a)
    if k<1 or k>4096 or len(b)!=k:raise ValueError('K outside controlled range')
    for matrix in (a,b):
        for row in matrix:
            if len(row)!=8 or any(type(v) is not int or not -32768<=v<=32767 for v in row):
                raise ValueError('expected K rows of 8 signed INT16')
    return [sum(a[t][i]*b[t][j] for t in range(k)) for i in range(8) for j in range(8)]

def unpack48(raw: bytes) -> list:
    if not isinstance(raw,(bytes,bytearray,memoryview)) or len(raw)!=384:raise ValueError('raw result must be 384 bytes')
    return [int.from_bytes(raw[i:i+6],'little',signed=True) for i in range(0,384,6)]

def positive_inputs(case_id: int,core: int=0,warp: int=0) -> tuple:
    if type(core) is not int or type(warp) is not int or not 0<=core<4 or not 0<=warp<4:
        raise ValueError('invalid core/warp')
    if case_id in range(101,106):
        data=(Path(__file__).resolve().parents[2]/'contracts/s8/native_vectors.json').read_bytes()
        if hashlib.sha256(data).hexdigest()!=VECTOR_SHA:raise ValueError('frozen input identity mismatch')
        row=json.loads(data)['cases'][case_id-101]
        return row['a'],row['b']
    if case_id in (110,111,112,113):
        k,value={110:(1,0),111:(3,1),112:(3,-1),113:(4096,-32768)}[case_id]
        return [[value]*8 for _ in range(k)],[[value]*8 for _ in range(k)]
    if case_id==114:
        return ([[32767 if (t+i)%2==0 else -32768 for i in range(8)] for t in range(4096)],
                [[32767 if (t+j)%2==1 else -32768 for j in range(8)] for t in range(4096)])
    if case_id in (115,120):
        tag=1 if case_id==115 else 4*core+warp+1
        return [[tag*(i+1) for i in range(8)] for _ in range(3)], [[(t+1)*(j+1) for j in range(8)] for t in range(3)]
    raise ValueError('not a controlled positive case')

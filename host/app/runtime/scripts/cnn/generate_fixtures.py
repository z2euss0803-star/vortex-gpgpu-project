"""Independent integer mathematical reference and immutable-by-hash fixtures.

This program produces CPU reference data, never GPU execution evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

CASES = ('main', 'zero', 'negative', 'positive', 'checker')

def checked(value):
    if not -(1 << 31) <= value < (1 << 31):
        raise OverflowError('INT32 accumulator overflow')
    return value

def reference(input64, weights):
    if len(input64) != 64 or any(len(weights[k]) != n for k,n in
        (('conv',18),('conv_bias',2),('fc',36),('fc_bias',2))):
        raise ValueError('Fixed CNN tensor shape mismatch')
    z = []
    for c in range(2):
        for y in range(6):
            for x in range(6):
                value = weights['conv_bias'][c]
                for ky in range(3):
                    for kx in range(3):
                        value = checked(value + input64[(y+ky)*8+x+kx]*weights['conv'][c*9+ky*3+kx])
                z.append(checked(value))
    r = [max(0,v) for v in z]
    p = [max(r[c*36+(y*2+dy)*6+x*2+dx] for dy in range(2) for dx in range(2))
         for c in range(2) for y in range(3) for x in range(3)]
    output = []
    for o in range(2):
        value = weights['fc_bias'][o]
        for j in range(18):
            value = checked(value + p[j]*weights['fc'][o*18+j])
        output.append(checked(value))
    return dict(z=z,r=r,p=p,y=output)

def fixture_input(case_name):
    if case_name == 'main': return [(5*i+1)%9-4 for i in range(64)]
    if case_name == 'zero': return [0]*64
    if case_name == 'negative': return [-4]*64
    if case_name == 'positive': return [4]*64
    if case_name == 'checker': return [4 if (i//8+i%8)%2==0 else -4 for i in range(64)]
    raise ValueError('Unknown fixed fixture')

def guarded(values):
    padded = ((len(values)*4+63)//64)*16
    words = [0x55aa55aa]*16 + [v&0xffffffff for v in values]
    words += [0xc0def00d]*(padded-len(values)) + [0xaa55aa55]*16
    return struct.pack('<'+'I'*len(words), *words)

def generate_fixture(case_name, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    x = fixture_input(case_name)
    weights = {'conv':[(3*i+2)%7-3 for i in range(18)],'conv_bias':[-1,2],
               'fc':[(5*i+3)%7-3 for i in range(36)],'fc_bias':[2,-2]}
    result = reference(x, weights)
    source = dict(x=x,wconv=weights['conv'],bconv=weights['conv_bias'],
                  wfc=weights['fc'],bfc=weights['fc_bias'])
    manifest = dict(contract='cnn-i32-8x8-v1',scope='CPU_REFERENCE_NOT_GPU',case=case_name,tensors=[])
    for name in ('x','wconv','bconv','z','r','p','wfc','bfc','y'):
        expected = result[name] if name in result else source[name]
        initial = [0xdeadbeef]*len(expected) if name in result else expected
        initial_blob, expected_blob = guarded(initial), guarded(expected)
        filename = name+'.bin'
        expected_file = name+'.expected.bin' if name in result else filename
        (out_dir/filename).write_bytes(initial_blob)
        (out_dir/expected_file).write_bytes(expected_blob)
        manifest['tensors'].append(dict(name=name,elements=len(expected),payload_bytes=len(expected)*4,
            object_bytes=len(initial_blob),file=filename,expected_file=expected_file,
            sha256=hashlib.sha256(initial_blob).hexdigest(),expected_sha256=hashlib.sha256(expected_blob).hexdigest()))
    (out_dir/'manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode('utf-8'))
    return manifest

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',required=True)
    args = parser.parse_args()
    for case in CASES: generate_fixture(case,Path(args.out)/case)
    print('CPU_REFERENCE_FIXTURES_5_CASES_GENERATED_NOT_GPU')

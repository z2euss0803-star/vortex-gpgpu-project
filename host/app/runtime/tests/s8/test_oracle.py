from pathlib import Path
import hashlib,importlib,json,struct,unittest
ROOT=Path(__file__).resolve().parents[2]

def packed(values):
    if len(values)!=64 or any(v<-(1<<47) or v>=(1<<47) for v in values):raise ValueError('signed48 range')
    return b''.join((v&((1<<48)-1)).to_bytes(6,'little') for v in values)

class OracleTests(unittest.TestCase):
    def api(self):
        try:return importlib.import_module('scripts.s8.oracle')
        except ModuleNotFoundError:self.fail('independent S8 integer oracle missing')

    def test_sign_crossword_and_extrema(self):
        api=self.api();values=[0,-1,-(1<<47),(1<<47)-1,0x123456789abc,-0x123456789abc]+[0]*58
        self.assertEqual(api.unpack48(packed(values)),values)
        with self.assertRaises(ValueError):api.unpack48(b'\0'*383)
        with self.assertRaises(ValueError):packed([1<<47]*64)
        with self.assertRaises(ValueError):packed([-(1<<47)-1]*64)

    def test_frozen_320_and_extreme_k(self):
        api=self.api();source=ROOT/'contracts/s8/native_vectors.json'
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),'c1040e4976018155577a4afab6fb53f31ab5e220473d8f286d49cb32912b9c22')
        cases=json.loads(source.read_text())['cases']
        out=ROOT/'build/s8';out.mkdir(parents=True,exist_ok=True)
        blob=bytearray(b'VXLREF1\0'+struct.pack('<I',5))
        for case in cases:
            self.assertEqual(api.matmul(case['a'],case['b']),case['oracle'])
            blob+=struct.pack('<I',case['k'])
            for name in ('a','b'):
                for row in case[name]:blob+=struct.pack('<8h',*row)
            blob+=struct.pack('<64q',*case['oracle'])
        (out/'reference.bin').write_bytes(blob)
        self.assertEqual(api.matmul([[-32768]*8]*4096,[[-32768]*8]*4096),[4398046511104]*64)

    def test_shapes_range_and_all_slot_tags(self):
        api=self.api();digests=set()
        for c in range(4):
            for w in range(4):
                a,b=api.positive_inputs(120,c,w)
                self.assertEqual(api.matmul(a,b),[6*(4*c+w+1)*(i+1)*(j+1) for i in range(8) for j in range(8)])
                digests.add(hashlib.sha256(struct.pack('<24h',*(v for row in a for v in row))).digest())
        self.assertEqual(len(digests),16)
        for a,b in [([],[]),([[0]*7],[[0]*8]),([[32768]*8],[[0]*8]),([[True]*8],[[0]*8]),([[0]*8]*4097,[[0]*8]*4097)]:
            with self.assertRaises(ValueError):api.matmul(a,b)

if __name__=='__main__':unittest.main()

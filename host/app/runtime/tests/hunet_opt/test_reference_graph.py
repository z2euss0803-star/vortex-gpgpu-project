import tempfile,unittest
from pathlib import Path
from scripts.hunet_opt.reference import checked_asset,validate_stages,stage_shapes,ROOT
import json,copy
class Reference(unittest.TestCase):
    def test_identity_and_shape_are_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'input';p.write_bytes(b'1234')
            with self.assertRaises(ValueError):checked_asset(Path(tmp),dict(path='input',bytes=4,sha256='0'*64,shape=[1],dtype='FP32'))
            with self.assertRaises(ValueError):checked_asset(Path(tmp),dict(path='../input',bytes=4,sha256='0'*64,shape=[1],dtype='FP32'))
        with self.assertRaises(ValueError):validate_stages([dict(stage=0,bytes=4,shape=[1])])
    def test_exact_hundred_stage_shape_identity(self):
        frozen=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
        rows=[dict(stage=i,bytes=s['bytes'],gpu=s['gpu'],shape=stage_shapes()[i],dtype='FP32') for i,s in enumerate(frozen)]
        validate_stages(rows)
        bad=copy.deepcopy(rows);bad[41]['shape']=[1,1,48]
        with self.assertRaises(ValueError):validate_stages(bad)
if __name__=='__main__':unittest.main()

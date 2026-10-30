import unittest,struct,hashlib,copy,tempfile
from pathlib import Path
try:
    from scripts.mosaic import assets
except ImportError:
    assets=None

def layers():
    result=[]
    for i in range(16):
        # Two rows, K9 padded to32; signed endpoints and every padding position.
        values=[-32767,32767,0,1,-1,2,-2,3,-3]+[0]*23
        p=dict(co=2,ci=1,k=3,norm=int(i<15),q16=struct.pack('<64h',*(values+values)),
               coeff=struct.pack('<4f',.125,-.5,.25,.5),sA=struct.pack('<f',.0625))
        if i<15:p.update(norm_gamma=struct.pack('<2f',1.,.75),norm_beta=struct.pack('<2f',0.,-.25))
        result.append(p)
    return result

SOURCES={k:hashlib.sha256(k.encode()).hexdigest() for k in ('model','selected','source_old','checkpoint')}

class Assets(unittest.TestCase):
    def api(self):
        self.assertIsNotNone(assets,'versioned native INT16 asset implementation is missing')
        return assets
    def build(self):
        a=self.api();blob,manifest=a.build_assets(layers(),SOURCES)
        self.assertIsInstance(blob,bytes);return a,blob,manifest
    def rebound(self,a,data,manifest):
        data=bytearray(data);data[176:208]=hashlib.sha256(data[256:]).digest()
        m=copy.deepcopy(manifest);m['sha256']=hashlib.sha256(data).hexdigest();m['bytes']=len(data)
        return bytes(data),m
    def test_native_q16_equals_frozen_selected_values(self):
        a,blob,m=self.build();decoded=a.load_assets(blob,m)
        self.assertEqual(len(decoded),16)
        for expected,actual in zip(layers(),decoded):
            for name in ('q16','coeff','sA','norm_gamma','norm_beta'):
                if name in expected:self.assertEqual(expected[name],actual[name])
        self.assertEqual(m['source_sha256'],SOURCES)
    def test_deterministic_and_independent_magic(self):
        a,blob,m=self.build();self.assertEqual((blob,m),a.build_assets(layers(),SOURCES))
        self.assertEqual(blob[:8],b'MS8AST1\0');self.assertEqual(struct.unpack_from('<4I',blob,8),(1,256,16,384))
        with self.assertRaises(ValueError):a.load_assets(b'W16APKG\0'+blob[8:],m)
    def test_wrong_endian_scale_and_truncation_rejected(self):
        a,blob,m=self.build()
        for bad in (blob[:-1],blob+b'\0',blob[:8]+struct.pack('>I',1)+blob[12:]):
            with self.assertRaises(ValueError):a.load_assets(bad,m)
        bad=layers();bad[0]['sA']=struct.pack('<f',0.)
        with self.assertRaises(ValueError):a.build_assets(bad,SOURCES)
        bad=layers();bad[0]['coeff']=struct.pack('<4f',float('nan'),0.,1.,0.)
        with self.assertRaises(ValueError):a.build_assets(bad,SOURCES)
    def test_production_minus32768_and_nonzero_padding_rejected(self):
        a=self.api()
        for offset,value in ((0,-32768),(2*9,1)):
            bad=layers();p=bytearray(bad[0]['q16']);struct.pack_into('<h',p,offset,value);bad[0]['q16']=bytes(p)
            with self.assertRaises(ValueError):a.build_assets(bad,SOURCES)
    def test_overlap_overflow_shape_reserved_and_segment_hash_rejected(self):
        a,blob,m=self.build()
        for offset,raw in ((256+64+8,struct.pack('<Q',2**64-64)),
                           (256+64+16,struct.pack('<Q',2**64-1)),
                           (256+4,struct.pack('<I',3)),(256+32,b'\x01'),
                           (256+64+32,b'\xff'),(208,b'\x01')):
            bad=bytearray(blob);bad[offset:offset+len(raw)]=raw;data,rebound=self.rebound(a,bad,m)
            with self.assertRaises(ValueError):a.load_assets(data,rebound)
    def test_tampered_bytes_and_wrong_source_are_rejected(self):
        a,blob,m=self.build();bad=bytearray(blob);bad[-1]^=1
        with self.assertRaises(ValueError):a.load_assets(bad,m)
        wrong=copy.deepcopy(m);wrong['source_sha256']['model']='00'*32
        with self.assertRaises(ValueError):a.load_assets(blob,wrong)
    def test_nonzero_unused_segment_and_extra_layer_rejected(self):
        a,blob,m=self.build();bad=bytearray(blob);bad[256+15*384+64+3*64]=1;data,rebound=self.rebound(a,bad,m)
        with self.assertRaises(ValueError):a.load_assets(data,rebound)
        with self.assertRaises(ValueError):a.build_assets(layers()+[layers()[0]],SOURCES)
    def test_holdout_not_recalibrated(self):
        a=self.api();self.assertTrue(callable(getattr(a,'export_assets',None)))
        # Frozen sources only; synthetic equal payloads remain equal regardless
        # of destination path. Export does not accept input-image calibration.
        with tempfile.TemporaryDirectory() as temp:
            out=Path(temp)/'new'
            with self.assertRaises(ValueError):a.export_assets(Path(temp)/'missing-source.json',out)
            self.assertFalse(out.exists())
    def test_source_identity_covers_mosaic_asset_config(self):
        from scripts.demo.build_aarch64 import software_identity
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);p=root/'config/mosaic/model-v1.json';p.parent.mkdir(parents=True)
            p.write_bytes(b'{"model":"a"}');before=software_identity(root)
            p.write_bytes(b'{"model":"b"}');self.assertNotEqual(before,software_identity(root))

if __name__=='__main__':unittest.main()

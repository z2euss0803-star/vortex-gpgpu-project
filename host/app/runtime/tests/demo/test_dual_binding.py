import hashlib,struct,unittest
from scripts.demo.dual_binding import rebind_hunet,pending_hunet_gate,source_digest,inventory_source
import pathlib,tempfile
class DualBinding(unittest.TestCase):
    def old(self):return struct.pack('<8I',0x44424c48,3,512,4,9,15,0,0)+b''.join(bytes([i])*32 for i in range(1,11))+bytes(32)+b''.join(bytes([i])*32 for i in range(12,15))+bytes(32)
    def test_only_cpu_and_source_change_old_gpu_and_model_preserved(self):
        old=self.old();self.assertEqual(len(old),512)
        b=rebind_hunet(old,'ab'*32,'cd'*32)
        for i in range(512):
            if 96<=i<128:self.assertEqual(b[i],0xab)
            elif 256<=i<288:self.assertEqual(b[i],0xcd)
            else:self.assertEqual(b[i],old[i])
    def test_never_inherits_numeric_admission(self):
        b=rebind_hunet(self.old(),'ab'*32,'cd'*32);g=pending_hunet_gate(b)
        self.assertEqual(len(g),128);self.assertEqual(g[:8],b'VXFUSG1\0');self.assertEqual(g[8:16],bytes(8))
        self.assertEqual(g[16:48],hashlib.sha256(b).digest());self.assertEqual(g[48:],bytes(80))
    def test_bad_identity_or_source_cycle_rejected(self):
        for old in (bytes(512),self.old()[:-1],self.old()[:8]+struct.pack('<I',384)+self.old()[12:]):
            with self.assertRaises(ValueError):rebind_hunet(old,'ab'*32,'cd'*32)
        for name in ('../core/a.c','/core/a.c','driver/vxl_mosaic_release.h','generated/demo/mosaic_release_template.h'):
            with self.assertRaises(ValueError):source_digest({name:dict(bytes=1,sha256='ab'*32)})
        a=source_digest({'core/a.c':dict(bytes=1,sha256='ab'*32)})
        self.assertNotEqual(a,source_digest({'core/a.c':dict(bytes=2,sha256='ab'*32)}))
    def test_source_inventory_ignores_only_generated_cycle_and_build_evidence(self):
        workspace=(pathlib.Path(__file__).resolve().parents[2]/'build/demo').resolve();workspace.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='binding-inventory-',dir=workspace) as d:
            p=pathlib.Path(d)
            self.assertEqual(p.resolve().parent,workspace)
            for name,value in [('core/a.c',b'a'),('generated/demo/ascii.h',b'font'),('generated/demo/mosaic_release_template.h',b'cycle'),('evidence/raw.log',b'error'),('build/a.o',b'obj'),('config/mosaic/a.json',b'{}')]:
                f=p/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(value)
            r=inventory_source(p)
            self.assertEqual(set(r),{'core/a.c','generated/demo/ascii.h','config/mosaic/a.json'})
            before=source_digest(r);(p/'core/a.c').write_bytes(b'b');self.assertNotEqual(before,source_digest(inventory_source(p)))
if __name__=='__main__':unittest.main()

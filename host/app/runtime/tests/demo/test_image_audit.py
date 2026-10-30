import hashlib,pathlib,tempfile,unittest
from scripts.demo.audit_image import payload_errors,load_errors,library_errors,hardware_errors
class ImageAudit(unittest.TestCase):
    def test_selected_bit_and_xsa_must_still_match(self):
        root=pathlib.Path(__file__).resolve().parents[2]/'build/demo'
        with tempfile.TemporaryDirectory(dir=str(root)) as d:
            p=pathlib.Path(d)/'bit';p.write_bytes(b'frozen')
            digest=hashlib.sha256(b'frozen').hexdigest()
            e=dict(bit=str(p),xsa=str(p),bit_sha256=digest,xsa_sha256=digest)
            self.assertEqual(hardware_errors(e),[])
            p.write_bytes(b'drift');self.assertEqual(len(hardware_errors(e)),2)
    def test_missing_program12_and_wrong_binding_rejected(self):
        files={'usr/bin/vxl_demo':b'cpu','usr/share/codex-demo/mosaic/program12/code.bin':b'rv32','usr/share/codex-demo/hunet/binding-fused.bin':b'bound'}
        expected={n:dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest()) for n,b in files.items()}
        self.assertEqual(payload_errors(files,expected),[])
        bad=dict(files);del bad['usr/share/codex-demo/mosaic/program12/code.bin'];self.assertTrue(payload_errors(bad,expected))
        bad=dict(files);bad['usr/share/codex-demo/hunet/binding-fused.bin']=b'other';self.assertTrue(payload_errors(bad,expected))
    def test_load_ranges_overlap_rejected(self):
        self.assertEqual(load_errors(40*1024**2,200*1024**2),[])
        self.assertTrue(load_errors(0x10000000,200*1024**2));self.assertTrue(load_errors(40*1024**2,0x20000000))
    def test_wrong_architecture_even_with_matching_library_hash_refused(self):
        raw=b'\x7fELF\x02\x01'+bytes(12)+(62).to_bytes(2,'little')+bytes(44)
        self.assertTrue(library_errors({'usr/lib/libdrm.so':raw},{'libdrm.so':dict(path='usr/lib/libdrm.so',sha256=hashlib.sha256(raw).hexdigest())}))
if __name__=='__main__':unittest.main()

import importlib.util,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
def module():
 s=importlib.util.spec_from_file_location('test_fused_package',ROOT/'scripts/hunet_fused/package_release.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
class ReleaseTests(unittest.TestCase):
 def test_any_bound_asset_mutation_rejected(self):
  m=module()
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);names=['program9/code.bin','descriptors.bin','jobs.bin','layout.json','cpu/gelu.elf','cpu/libm.sha256','cpu-contract.json','base/model.onnx','base/inputs/000.f32','source.tar.gz','base/hardware/top.bit']
   records={}
   for n in names:
    p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(n.encode());records[n]=dict(bytes=p.stat().st_size,sha256=m.sha(p.read_bytes()))
   m.verify_payload(root,records)
   for n in names:
    p=root/n;b=p.read_bytes();p.write_bytes(bytes([b[0]^1])+b[1:])
    with self.subTest(asset=n),self.assertRaises(ValueError):m.verify_payload(root,records)
    p.write_bytes(b)
   (root/'extra').write_bytes(b'new')
   with self.assertRaises(ValueError):m.verify_payload(root,records)
 def test_missing_actual_cpu_and_source_rejected(self):
  m=module()
  with tempfile.TemporaryDirectory() as d:self.assertEqual(m.verify_fused_release(pathlib.Path(d))['integrity'],'FAIL')
if __name__=='__main__':unittest.main()

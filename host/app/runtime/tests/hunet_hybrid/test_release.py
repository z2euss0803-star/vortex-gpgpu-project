import hashlib,importlib.util,json,shutil,struct,tarfile,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('hybrid_package',ROOT/'scripts/hunet_hybrid/package_release.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class Release(unittest.TestCase):
 def test_fixture_alias_binding_and_tamper(self):
  base=ROOT.parent/'vortex_linux_backend_hunet_v5_20261011/release/hunet-v5-9b3c4d5'
  with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
   d=Path(d);cpu=d/'TEST_FIXTURE';cpu.write_bytes(b'TEST_FIXTURE_NOT_CROSS_ELF')
   def fake_package(root,out,commit):
    shutil.copytree(base,out);return json.loads((out/'release.json').read_text())
   # This is an immutable historical release fixture. Its CPU role contract
   # must describe that fixture's archived platform, rather than the selected
   # platform of the current checkout. This adapter is confined to this test.
   with tarfile.open(base/'source.tar.gz') as archive:
    model=json.loads(archive.extractfile('contracts/hunet_v5.json').read())
   role=mod.freeze.freeze_contract(ROOT)
   historical=dict(role,profile=model['profile'],identities=mod.freeze.identities(model))
   # The fixture already injects its historical source archive and CPU bytes;
   # supply that same archived commit, independently of a clean tar's .git.
   fixture_commit=json.loads((base/'release.json').read_text())['source_commit'].encode()
   with patch.object(mod.legacy,'git',return_value=fixture_commit),patch.object(mod.legacy,'package_release',side_effect=fake_package),patch.object(mod.freeze,'freeze_contract',return_value=historical):
    r=mod.package_hybrid(ROOT,d/'release',cpu,test_fixture=True)
   dst=d/'release';self.assertEqual(mod.verify_hybrid_release(dst)['integrity'],'PASS')
   self.assertEqual(r['scope'],'TEST_FIXTURE_HYBRID_NOT_CROSS')
   self.assertEqual((dst/'program8/code.bin').read_bytes(),(dst/'base/program6/code.bin').read_bytes())
   b=(dst/'binding-hybrid.bin').read_bytes();self.assertEqual(len(b),384)
   self.assertEqual(struct.unpack('<8I',b[:32]),(0x44424c48,2,384,4,8,7,0,0))
   self.assertEqual(b[256:288],hashlib.sha256(cpu.read_bytes()).digest())
   for name in ['cpu/gelu.elf','cpu-contract.json','cpu-stages.json','program8/code.bin','base/model.onnx','base/inputs/000.f32']:
    p=dst/name;orig=p.read_bytes();p.write_bytes(orig+b'X')
    self.assertEqual(mod.verify_hybrid_release(dst)['integrity'],'FAIL',name);p.write_bytes(orig)
   (dst/'binding-hybrid.bin').write_bytes((dst/'base/binding.bin').read_bytes())
   self.assertEqual(mod.verify_hybrid_release(dst)['integrity'],'FAIL')
   if historical!=role:
    with patch.object(mod.legacy,'git',return_value=fixture_commit),patch.object(mod.legacy,'package_release',side_effect=fake_package):
     mod.package_hybrid(ROOT,d/'mixed-platform',cpu,test_fixture=True)
    mixed=mod.verify_hybrid_release(d/'mixed-platform')
    self.assertEqual(mixed['integrity'],'FAIL')
    self.assertIn('BASELINE_IDENTITY_CHANGED',mixed['reason'])
 def test_production_rejects_fixture_and_dirty_source(self):
  with tempfile.TemporaryDirectory(dir=ROOT/'build') as d:
   cpu=Path(d)/'bad';cpu.write_bytes(b'TEST_FIXTURE')
   with self.assertRaisesRegex(ValueError,'CPU_AARCH64_ELF'):mod.package_hybrid(ROOT,Path(d)/'out',cpu)
if __name__=='__main__':unittest.main()

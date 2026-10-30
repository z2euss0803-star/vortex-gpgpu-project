import hashlib,importlib.util,json,pathlib,tempfile,unittest
R=pathlib.Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('w4_boot_gate',R/'scripts/hunet_fused/image.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class W4Boot(unittest.TestCase):
 def test_actual_firmware_and_imported_xsa_must_match_package_inputs(self):
  self.assertTrue(hasattr(m,'checked_boot_files'),'actual candidate firmware and XSA are not verified')
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);project=root/'env_backend';image=project/'images/linux';image.mkdir(parents=True);release=root/'release';release.mkdir()
   imported=project/'project-spec/hw-description/system.xsa';imported.parent.mkdir(parents=True);imported.write_bytes(b'W4-XSA')
   names=dict(fsbl='zynqmp_fsbl.elf',pmufw='pmufw.elf',atf='bl31.elf',uboot='u-boot.elf')
   for k,n in names.items():(image/n).write_bytes(k.encode())
   sh=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
   manifest=dict(bit_sha256='a'*64,xsa_sha256=sh(imported));(release/'release-fused.json').write_text(json.dumps(manifest))
   integration=dict(boot_policy='REBUILD_MATCHED_W4',retained_boot_sha256='c'*64)
   rec=dict(scope='MATCHED_W4_BOOT_PACKAGE_V1',exit=0,boot_sha256='d'*64,**manifest,inputs={k:sh(image/n) for k,n in names.items()})
   self.assertEqual(m.checked_boot_files(project,release,integration,rec),'d'*64)
   for p in [image/n for n in names.values()]+[imported]:
    original=p.read_bytes();p.write_bytes(b'WRONG')
    with self.assertRaises(ValueError):m.checked_boot_files(project,release,integration,rec)
    p.write_bytes(original)
   (image/'pmufw.elf').unlink()
   with self.assertRaises((ValueError,OSError)):m.checked_boot_files(project,release,integration,rec)
 def test_new_platform_requires_natural_matching_package_receipt(self):
  self.assertTrue(hasattr(m,'checked_boot_sha'),'new platform must not reuse parent BOOT identity')
  manifest=dict(bit_sha256='a'*64,xsa_sha256='b'*64)
  integration=dict(boot_policy='REBUILD_MATCHED_W4',retained_boot_sha256='c'*64)
  with self.assertRaises(ValueError):m.checked_boot_sha(integration,manifest,None)
  rec=dict(scope='MATCHED_W4_BOOT_PACKAGE_V1',exit=0,boot_sha256='d'*64,bit_sha256='a'*64,xsa_sha256='b'*64,inputs={k:'e'*64 for k in ('fsbl','pmufw','atf','uboot')})
  self.assertEqual(m.checked_boot_sha(integration,manifest,rec),'d'*64)
  for key,value in [('exit',1),('bit_sha256','f'*64),('xsa_sha256','f'*64),('boot_sha256','invalid')]:
   bad=dict(rec);bad[key]=value
   with self.assertRaises(ValueError):m.checked_boot_sha(integration,manifest,bad)
if __name__=='__main__':unittest.main()

from scripts.unified.hunet_program import selected_name as selected_hunet_name
"""Production firmware packaging, with no CPU marker programs or GPU claims."""
import hashlib,json,pathlib,struct,unittest
from scripts.unified.bind_kernel import firmware_inputs,builtin_makefile,identity_dtsi,build_kernel,require_cross_inputs,copy_project_metadata
from scripts.unified.cross_common import snapshot
from scripts.unified.mosaic_program import selected_name
import tempfile

ROOT=pathlib.Path(__file__).resolve().parents[2]

class FirmwareBinding(unittest.TestCase):
 def test_real_project_marker_copied_but_build_components_images_not_reused(self):
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);old=base/'parent';out=base/'private';old.mkdir();out.mkdir()
   marker=b'[project]\narchitecture = aarch64\n'
   (old/'config.project').write_bytes(marker)
   for name in ('project-spec','.petalinux','build','components','images'):
    (old/name).mkdir();(old/name/'sentinel').write_bytes(name.encode())
   copy_project_metadata(old,out)
   self.assertTrue((out/'config.project').is_file(),'PetaLinux2020.1 requires its real config.project marker')
   self.assertEqual((out/'config.project').read_bytes(),marker)
   for name in ('project-spec','.petalinux'):self.assertEqual((out/name/'sentinel').read_bytes(),name.encode())
   for name in ('build','components','images'):self.assertFalse((out/name).exists())
 def cross_fixture(self,root):
  for name in ('nnrt/operator.cpp','device/vxnn/entry.cpp','data/nnrt/programs/demo/code.bin','docs/note.md'):
   p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'original')
  original=snapshot(root)
  return dict(natural_exit=0,target='rv32',target_executed=False,source_before=original,source_after=original,gpu_raw_cycles=None)
 def test_cross_metadata_and_model_only_changes_do_not_recompile_runtime(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp);receipt=self.cross_fixture(root);(root/'docs/note.md').write_bytes(b'new documentation')
   model=root/'data/nnrt/packages/new-model.json';model.parent.mkdir(parents=True);model.write_bytes(b'new network')
   require_cross_inputs(receipt,snapshot(root),'rv32')
 def test_cross_gpu_source_and_program_bytes_cannot_hide_in_metadata_changes(self):
  for name in ('device/vxnn/entry.cpp','data/nnrt/programs/demo/code.bin','nnrt/operator.cpp'):
   with self.subTest(path=name),tempfile.TemporaryDirectory() as temp:
    root=pathlib.Path(temp);receipt=self.cross_fixture(root);(root/name).write_bytes(b'different code')
    with self.assertRaises(ValueError):require_cross_inputs(receipt,snapshot(root),'rv32')
 def test_failed_or_changed_during_cross_receipt_is_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp);receipt=self.cross_fixture(root)
   for changed in (dict(receipt,natural_exit=1),dict(receipt,gpu_raw_cycles=123),dict(receipt,source_after={}),dict(receipt,target='aarch64-s8'),dict(receipt,target_executed=True)):
    with self.subTest(changed=changed),self.assertRaises(ValueError):require_cross_inputs(changed,snapshot(root),'rv32')
 def test_s8_admits_exact_three_nn_and_ten_image_families(self):
  row=firmware_inputs(ROOT,1)
  self.assertEqual(row.get('scope'),'PRODUCTION_BOOT_FIRMWARE_INPUTS_NOT_GPU_EXECUTED')
  self.assertEqual([(x['namespace'],x['logical_program']) for x in row.get('entries',[])],[(1,65537),(1,65552),(1,65568)]+[(3,512+i) for i in range(10)])
  self.assertEqual(row.get('watchdog'),4000000000)
  self.assertIsNone(row.get('gpu_raw_cycles'))
 def test_s8_payload_and_schemas_keep_declared_versioned_family_bytes(self):
  row=firmware_inputs(ROOT,1)
  for entry,name in zip(row.get('entries',[]),('int16-family-v1',selected_hunet_name(ROOT),selected_name(ROOT))):
   family=ROOT/'data/nnrt/programs'/name
   expected=(family/'code.bin').read_bytes()+((family/'rodata.bin').read_bytes() if name.startswith('mosaic') else b'')
   self.assertEqual(entry['payload'],expected)
   self.assertEqual(entry['schema'],(family/'args-schema.bin').read_bytes())
  self.assertEqual(len(row.get('entries',[])),13)
 def test_policy_retirement_and_real_stack_guard_are_both_preserved(self):
  row=firmware_inputs(ROOT,1);raw=row.get('execution',b'')
  self.assertEqual(len(raw),64+13*256)
  header=struct.unpack_from('<4I',raw)
  self.assertEqual(header,(0x454c5856,2,len(raw),13))
  self.assertEqual(raw[16:48],hashlib.sha256(row['catalog']).digest())
  for i in range(3):
   policy=struct.unpack_from('<56I',raw,64+256*i+32)
   self.assertEqual(list(policy[:6]),[4000000000,0xffffffff,8,256,0,4])
   self.assertEqual(policy[18],3)
   guards=[list(policy[19+5*j:24+5*j]) for j in range(3)]
   self.assertEqual(guards,[[0x10002000,0xfc0,64,64,1],[0xfff00000,0,64,64,1],[0xfff00000,0x70000,64,8192,64]])
   self.assertEqual(policy[39],0)
  for i in range(3,13):
   policy=struct.unpack_from('<56I',raw,64+256*i+32)
   self.assertEqual(list(policy[:6]),[4000000000,0xffffffff,0,0,0,0])
   self.assertEqual(policy[39],1)
 def test_p8_preserves_original_catalog_policy_and_program_bytes(self):
  row=firmware_inputs(ROOT,2);original=ROOT/'data/p8/programs'
  self.assertEqual(row.get('catalog'),(original/'catalog.bin').read_bytes())
  self.assertEqual(row.get('execution'),(original/'execution-v2.bin').read_bytes())
  self.assertEqual([(x['namespace'],x['logical_program']) for x in row.get('entries',[])],[(2,9),(2,10),(2,11),(2,12)])
  for entry in row['entries']:self.assertEqual(entry['payload'],(original/(entry['entry_id']+'.bin')).read_bytes())
 def test_provider_selection_is_explicit(self):
  for flag in (True,0,3,'1',None):
   with self.subTest(flag=flag),self.assertRaises(ValueError):firmware_inputs(ROOT,flag)

 def test_builtin_conversion_keeps_every_original_object_and_local_dialect(self):
  raw=(ROOT/'driver/Makefile').read_bytes();bound=builtin_makefile(raw)
  self.assertIn(b'obj-$(CONFIG_VXL_LINUX) += vxl_linux.o',bound)
  self.assertIn(b'-I$(srctree)/drivers/misc/vxl/include -Werror',bound)
  self.assertIn(b'-std=gnu11 -Wno-declaration-after-statement',bound)
  expected=raw.replace(b'obj-m += vxl_linux.o',b'obj-$(CONFIG_VXL_LINUX) += vxl_linux.o').replace(b'-I$(src)/../include -Werror',b'-I$(srctree)/drivers/misc/vxl/include -Werror')
  self.assertEqual(bound,expected)
 def test_identity_overlay_declares_exact_five_hashes_for_one_provider(self):
  declaration=json.loads((ROOT/'contracts/unified/providers.json').read_text())['providers'][1]
  text=identity_dtsi(declaration,'ab'*32,'cd'*32)
  self.assertEqual(text.count('-sha256 = ['),5)
  self.assertIn('compatible = "vortex,w4-graphics-p8-v1";',text)
  self.assertIn('vortex,compute-v2;',text)
  self.assertIn('vortex,catalog-sha256 = ['+' '.join(['ab']*32)+'];',text)
  self.assertIn('vortex,execution-sha256 = ['+' '.join(['cd']*32)+'];',text)

 def test_kernel_build_rejects_unfrozen_inputs_before_creating_output(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp);output=root/'build/new-kernel'
   env=root/'env.json';env.write_text(json.dumps(dict(root=str(root),source_lock_sha256='00'*32)))
   lock=root/'lock.json';lock.write_text('{}')
   with self.assertRaises(ValueError):build_kernel(env,1,lock,output)
   self.assertFalse(output.exists())

if __name__=='__main__':unittest.main()

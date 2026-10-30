"""Archive member admission fixtures, not genuine compiled driver evidence."""
import pathlib,struct,tempfile,unittest
from scripts.unified.bind_kernel import builtin_bundle_inputs,component_continuation,device_tree_recipe
class BuiltinObjectAdmission(unittest.TestCase):
 def test_actual_parent_dt_bridge_and_explicit_user_include_survive(self):
  parent=b'FILESEXTRAPATHS_prepend := "${THISDIR}/files:"\nSRC_URI += "file://system-user.dtsi"\nexport PETALINUX\ndo_configure_append () {\n script="${PETALINUX}/etc/hsm/scripts/petalinux_hsm_bridge.tcl"\n eval xsct -sdx -nodisp ${script} -a "soc_mapping"\n}\n'
  got=device_tree_recipe(parent)
  self.assertTrue(got.startswith(parent),'the real parent soc_mapping bridge must not be clobbered')
  self.assertEqual(got.count(b'petalinux_hsm_bridge.tcl'),1)
  self.assertIn(b'install -m 0644 ${WORKDIR}/system-user.dtsi ${DT_FILES_PATH}/system-user.dtsi',got)
  self.assertIn(b'install -m 0644 ${WORKDIR}/unified-identity.dtsi ${DT_FILES_PATH}/unified-identity.dtsi',got)
  self.assertIn(b'/include/ "system-user.dtsi"',got)
  for bad in [b'',parent.replace(b'soc_mapping',b'unknown'),parent+b'\n# VX_UNIFIED_DTG_RECIPE_V1\n']:
   with self.subTest(bad=bad[-30:]),self.assertRaises(ValueError):device_tree_recipe(bad)
 def test_component_continuation_never_promotes_a_failed_compile_or_changed_source(self):
  old=dict(scope='ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE',natural_exit=1,error="FileNotFoundError(2, 'No such file or directory')",artifacts={'vmlinux':{},'Image':{}},source_before={'explicit':'same'},commands=[dict(natural_exit=0,argv=['bash','-c',cmd]) for cmd in ['petalinux-config --get-hw-description=x --silentconfig','petalinux-build -c kernel','petalinux-build -c device-tree']])
  after=dict(scope='ACTUAL_UNCHANGED_SOURCE_AFTER_ORIGINAL_COMPONENT_BUILD',natural_exit=0,snapshot={'explicit':'same'},missing_monolithic_object=True,actual_builtin_archive=True)
  self.assertTrue(component_continuation(old,after),'only successful real components may continue into a new independent artifact audit')
  import copy
  for index in range(3):
   bad=copy.deepcopy(old);bad['commands'][index]['natural_exit']=1
   with self.assertRaises(ValueError):component_continuation(bad,after)
  bad=dict(after,snapshot={'explicit':'changed'})
  with self.assertRaises(ValueError):component_continuation(old,bad)
  with self.assertRaises(ValueError):component_continuation(dict(old,error='compiler error'),after)
 def fixture(self,base):
  driver=base/'drivers/misc/vxl/driver';(driver/'v2').mkdir(parents=True)
  header=bytearray(64);header[:7]=b'\x7fELF\x02\x01\x01';struct.pack_into('<HH',header,16,1,183)
  names=['vxl_drv.o','v2/startup_linux.o','shared_v2_owner.o']
  rows=[]
  for name in names:p=driver/name;p.write_bytes(header+name.encode());rows.append(str(p))
  return rows
 def test_complete_actual_member_paths_are_returned_not_guessed_monolithic_object(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp);rows=self.fixture(root);got=builtin_bundle_inputs(root,rows);self.assertEqual([r['path'] for r in got],rows);self.assertTrue(all(len(r['sha256'])==64 for r in got))
 def test_duplicates_escape_missing_lifecycle_and_wrong_elf_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   root=pathlib.Path(temp);rows=self.fixture(root)
   for bad in [rows+rows[:1],rows[:-1],['/unrelated/driver.o']+rows,[]]:
    with self.subTest(bad=bad),self.assertRaises(ValueError):builtin_bundle_inputs(root,bad)
   pathlib.Path(rows[0]).write_bytes(b'not the compiled ELF')
   with self.assertRaises(ValueError):builtin_bundle_inputs(root,rows)
if __name__=='__main__':unittest.main()

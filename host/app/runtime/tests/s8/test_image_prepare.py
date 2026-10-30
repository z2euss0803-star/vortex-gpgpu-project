import importlib,json,os,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
class Prepare(unittest.TestCase):
 @unittest.skipIf(os.name=='nt','Native Ubuntu symlink/import boundary; Windows lacks symlink privilege')
 def test_native_2020_1_import_has_hw_destination_and_external_log(self):
  api=importlib.import_module('scripts.s8.image')
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);old=root/'old/env_backend';release=root/'release';release.mkdir();project=root/'new/env_backend';project.parent.mkdir()
   (old/'project-spec/configs').mkdir(parents=True);(old/'project-spec/configs/config').write_text('CONFIG_TEST=y\n');(old/'project-spec/configs/rootfs_config').write_text('CONFIG_old=y\n')
   (old/'.petalinux').mkdir();(old/'.petalinux/metadata').write_text('PETALINUX_VER=2020.1\n')
   (old/'project-spec/meta-user/conf').mkdir(parents=True);(old/'project-spec/meta-user/conf/user-rootfsconfig').write_text('CONFIG_legacy_disabled\n')
   for name in ('downloads','sstate-cache'):(old/'build'/name).mkdir(parents=True)
   for name in ('payload','manifest.json','vxl_s8_release.h','source.tar.gz'):(release/name).write_bytes(b'TEST_ONLY')
   (release/'hardware').mkdir();(release/'hardware/system.xsa').write_bytes(b'TEST_ONLY_XSA')
   public=root/'authorized_keys';public.write_bytes(b'ssh-rsa TEST_ONLY\n');env=root/'env.json';env.write_text(json.dumps(dict(parent_project=str(old),authorized_key_file=str(public),petalinux_settings='/TEST_ONLY/settings.sh')))
   manifest=dict(files={'payload':{}},source_sha256='1'*64,binding_sha256='2'*64,digests=dict(bit='3'*64,xsa='4'*64))
   def native_import(settings,args,cwd,log):
    self.assertTrue((Path(cwd)/'project-spec/hw-description').is_dir(),'2020.1 pushd failure leaves cwd at project; require destination before tool')
    packages=(Path(cwd)/'project-spec/meta-user/conf/user-rootfsconfig')
    self.assertTrue(packages.is_file(),'2020.1 rootfs parser requires custom package declarations')
    for symbol in ('CONFIG_legacy_disabled','CONFIG_vxl-runtime','CONFIG_codex-firstboot'):self.assertIn(symbol,packages.read_text().splitlines())
    self.assertEqual(Path(log).parent,project.parent,'tool evidence must survive project cleanup')
   with patch.object(api,'verify_release',return_value=manifest),patch.object(api,'cache_sources',return_value=[old/'build/downloads',old/'build/sstate-cache']),patch.object(api,'_resource',return_value={'scope':'TEST_ONLY'}),patch.object(api,'_shell',side_effect=native_import):
    r=api._prepare(release,project,env)
    self.assertEqual(r['state'],'PREPARED_NOT_BUILT')
 def test_recipe_templates_and_static_driver_dialect_are_frozen_inputs(self):
  api=importlib.import_module('scripts.s8.image')
  paths={'kernel':'linux-xlnx_%.bbappend','apps':'vxl-runtime.bb','firstboot':'codex-firstboot.bb','dt':'system-user.dtsi'}
  for key,name in paths.items():
   self.assertEqual(api.recipes()[key],(api.ROOT/'petalinux/s8'/name).read_text())
  self.assertIn('-std=gnu11',api._kernel_make((api.ROOT/'driver/Makefile').read_text()))
 def test_missing_s8_xsa_and_wait_do_not_launch_build(self):
  api=importlib.import_module('scripts.s8.image')
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);release=root/'release';release.mkdir();(release/'receipt.json').write_text(json.dumps(dict(state='WAIT_HW_EXPORT',exit=2,missing=['bit','xsa'])))
   env=root/'env.json';env.write_text('{}')
   with patch.object(api.subprocess,'run') as run:
    self.assertEqual(api.prepare(release,root/'project',env)['state'],'WAIT_HW_EXPORT')
    self.assertEqual(api.build(release,root/'build-project',env)['state'],'WAIT_HW_EXPORT')
    run.assert_not_called();self.assertFalse((root/'project').exists())
 def test_build_refuses_resource_refusal(self):
  api=importlib.import_module('scripts.s8.image')
  self.assertFalse(api.resource_ok(dict(host='z2euss-virtual-machine',uid=1000,heavy=[],coordination=True,memory=4<<30,disk=1<<30)))
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);env=root/'env.json';env.write_text('{}')
   with patch.object(api,'image_lock'),patch.object(api,'verify_release',return_value={}),patch.object(api,'_resource',side_effect=ValueError('RESOURCE_REFUSED:test')),patch.object(api.subprocess,'run') as run:
    with self.assertRaisesRegex(ValueError,'RESOURCE_REFUSED'):api.build(root/'release',root/'project',env)
    run.assert_not_called();self.assertFalse((root/'project').exists())
 def test_image_lock_requires_explicit_coordination(self):
  api=importlib.import_module('scripts.s8.image')
  with self.assertRaises(ValueError):api.lock_config({})
  with self.assertRaises(ValueError):api.lock_config(dict(coordination_owner='C',coordination_file='C-marker'))
 def test_offline_cache_requires_owned_parent_and_all_sources(self):
  api=importlib.import_module('scripts.s8.image')
  with tempfile.TemporaryDirectory() as tmp:
   old=Path(tmp)/'codex_linux_backend_hunet_v5_TEST/env_backend';old.mkdir(parents=True)
   (old.parent/'integration.json').write_text(json.dumps(dict(scope='HW4_HUNET_FUSED_ISOLATED_IMAGE_BUILD',manifest=dict(profile=4))))
   with self.assertRaises(ValueError):api.cache_sources(old)
   for name in ('downloads','sstate-cache'):(old/'build'/name).mkdir(parents=True)
   self.assertEqual(len(api.cache_sources(old)),2)
   (old.parent/'integration.json').write_text(json.dumps(dict(scope='C_UNRELATED',manifest=dict(profile=4))))
   with self.assertRaises(ValueError):api.cache_sources(old)
if __name__=='__main__':unittest.main()

import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))


class PrepareImage(unittest.TestCase):
    def test_gpu_program_package_keeps_host_arch_qa(self):
        import fnmatch
        import re
        recipe=self.module().recipes('ab'*20,'12'*16)['apps']
        # Only the two immutable RV32 device ELFs may bypass host architecture QA.
        assignments=dict(re.findall(r'^([A-Za-z_${}0-9-]+)\s*(?:\+?=|=\+)\s*"([^"]*)"$',recipe,re.M))
        self.assertIn('${PN}-gpu-programs',assignments.get('PACKAGES','').split())
        self.assertIn('PACKAGES =+ "${PN}-gpu-programs"',recipe)
        self.assertIn('${PN}-gpu-programs',assignments.get('RDEPENDS_${PN}','').split())
        patterns=assignments['FILES_${PN}-gpu-programs'].split()
        self.assertEqual(patterns,['${datadir}/codex-hunet-v5/program6/kernel.elf',
                                   '${datadir}/codex-hunet-v5/program7/kernel.elf'])
        for name in ['${bindir}/vxl_hunet','${libdir}/libvortex-hunet.so',
                     '${datadir}/codex-hunet-v5/program6/code.bin']:
            self.assertFalse(any(fnmatch.fnmatchcase(name,p) for p in patterns))
        self.assertEqual(assignments['INSANE_SKIP_${PN}-gpu-programs'].split(),['arch'])
        self.assertNotIn('arch',assignments['INSANE_SKIP_${PN}'].split())
        self.assertNotIn('allarch',recipe)

    def test_shared_cache_accepts_only_own_hw4_candidate_family(self):
        import json
        m=self.module();tmp=ROOT/'build/test-tmp';tmp.mkdir(exist_ok=True,parents=True)
        with tempfile.TemporaryDirectory(dir=tmp) as d:
            root=Path(d);old=root/'cnn/env_backend';old.mkdir(parents=True)
            own=root/'codex_linux_backend_hunet_v5_previous';own.mkdir()
            (own/'integration.json').write_text(json.dumps(dict(scope='HW4_HUNET_ISOLATED_IMAGE_BUILD',manifest=dict(profile=4))))
            for name in ['downloads','sstate-cache']:(own/'env_backend/build'/name).mkdir(parents=True)
            caches,mode=m.cache_directories(old,root/'codex_linux_backend_hunet_v5_next',own)
            self.assertEqual(mode,'OWN_HW4_SHARED');self.assertEqual(len(caches),2)
            for bad in [old.parent,root/'codex_linux_backend_hunet_v5_next',root.parent/'outside']:
                self.assertRaises(ValueError,m.cache_directories,old,root/'codex_linux_backend_hunet_v5_next',bad)
            value=json.loads((own/'integration.json').read_text());value['manifest']['profile']=1;(own/'integration.json').write_text(json.dumps(value))
            self.assertRaises(ValueError,m.cache_directories,old,root/'codex_linux_backend_hunet_v5_next',own)

    def test_offline_cache_check_has_no_external_mirror(self):
        self.assertIn('SSTATE_MIRRORS = ""',self.module().offline_settings())
        self.assertIn('BB_NO_NETWORK = "1"',self.module().offline_settings())

    def module(self):
        path=ROOT/'scripts/hunet/prepare_image.py'
        self.assertTrue(path.is_file(),'isolated HW4 image preparer missing')
        spec=importlib.util.spec_from_file_location('hunet_prepare_image',path)
        value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

    def test_parent_overlap_and_existing_candidate_rejected(self):
        m=self.module();tmp=ROOT/'build/test-tmp';tmp.mkdir(exist_ok=True,parents=True)
        with tempfile.TemporaryDirectory(dir=tmp) as d:
            base=Path(d);old=base/'parent/env_backend';old.mkdir(parents=True)
            m.validate_paths(old,base/'codex_linux_backend_hunet_v5_fixture')
            for candidate in (old.parent,old/'nested',base.parent/'outside',base/'bad-name'):
                self.assertRaises(ValueError,m.validate_paths,old,candidate)
            new=base/'codex_linux_backend_hunet_v5_exists';new.mkdir()
            self.assertRaises(ValueError,m.validate_paths,old,new)

    def test_source_binding_and_original_boot_not_reused(self):
        m=self.module();recipes=m.recipes('ab'*20,'12'*16)
        kernel=recipes['kernel'];apps=recipes['apps']
        self.assertIn('subdir=vxl-source',kernel)
        self.assertIn('vxl_hunet_source.h',kernel)
        self.assertIn('$(CONFIG_VXL_LINUX)',kernel)
        for name in ('vxl_hunet_copy','vxl_hunet_wmma','libvortex-hunet.so'):
            self.assertIn(name,apps)
        self.assertIn('HUNET_BINDING_HEADER',apps)
        self.assertIn('subdir=hunet-release',apps)
        self.assertIn('md5='+('12'*16),apps)
        runner=(ROOT/'scripts/hunet/prepare_image.sh').read_text(encoding='utf-8')
        self.assertIn('petalinux-package --boot',runner)
        self.assertIn('--fpga',runner)
        self.assertNotIn('shutil.copy2',runner)


if __name__=='__main__':unittest.main()

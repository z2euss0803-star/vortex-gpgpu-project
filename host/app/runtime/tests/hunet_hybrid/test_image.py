import copy,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
from hybrid_image import recipes_hybrid,hybrid_image_errors
from hybrid_build import ROOT as SOURCE_ROOT
class HybridImageTests(unittest.TestCase):
 def test_recipe_installs_exact_cpu_and_builtin_header(self):
  r=recipes_hybrid('1'*40,'2'*32)
  self.assertIn('file://vxl_hybrid_release.h',r['kernel']);self.assertIn('${S}/drivers/misc/vxl/driver/',r['kernel']);self.assertIn('build/libvortex-hybrid.so',r['apps']);self.assertIn('cpu/gelu.elf ${D}${bindir}/vxl_hunet_hybrid',r['apps']);self.assertIn('base/program6/kernel.elf',r['apps']);self.assertIn('program8/kernel.elf',r['apps'])
 def test_missing_or_old_image_content_fails(self):
  rel=dict(cpu_binary_sha256='1'*64,binding_sha256='2'*64,program_sha256='3'*64)
  good=dict(hybrid_binding_sha256='2'*64,cpu_elf_sha256='1'*64,program8_code_sha256='3'*64,hybrid_builtin_symbols=['vxl_hybrid_enter_cpu','vxl_hybrid_begin_cpu','vxl_hybrid_commit_cpu','vxl_hybrid_get_cpu_evidence','vxl_hybrid_end'],runtime_libm_sha256='4'*64,cross_libm_sha256='4'*64,hybrid_library_source_bound=True)
  self.assertEqual(hybrid_image_errors(good,rel),[])
  for key,value in [('hybrid_binding_sha256','5'*64),('cpu_elf_sha256',None),('program8_code_sha256','6'*64),('hybrid_builtin_symbols',[]),('runtime_libm_sha256','7'*64),('hybrid_library_source_bound',False)]:
   r=copy.deepcopy(good);r[key]=value;self.assertTrue(hybrid_image_errors(r,rel),key)
if __name__=='__main__':unittest.main()

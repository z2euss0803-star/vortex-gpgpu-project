import copy,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet'))
from cross_audit import audit_hybrid_cross,audit_hybrid_kernel,HYBRID_OBJECTS
class HybridCrossTests(unittest.TestCase):
 def fixture(self):
  release=dict(source_commit='1'*40,source_digest='2'*64,binding_sha256='3'*64,cpu_binary_sha256='4'*64)
  elf=dict(source_commit='1'*40,source_digest='2'*64,machine='AArch64',interpreter='/lib/ld-linux-aarch64.so.1',sha256='5'*64,needed=['libc.so.6'],required_versions=['GLIBC_2.17'])
  r=dict(scope='HUNET_HYBRID_AARCH64_CROSS',exit=0,source_commit='1'*40,source_digest='2'*64,binding_sha256='3'*64,compiler=dict(target='aarch64-xilinx-linux',version='9.2.0',gcc_sha256='6'*64,gxx_sha256='7'*64),rootfs_libraries=['libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2'],runtime_versions=['GLIBC_2.17'],elfs={n:copy.deepcopy(elf) for n in ['libvortex-hunet.so','vxl_hunet','vxl_hunet_copy','vxl_hunet_wmma','libvortex-hybrid.so','vxl_hunet_hybrid']},verified_source_snapshot=True)
  r['elfs']['vxl_hunet_hybrid'].update(source_commit=None,source_digest=None,sha256='4'*64,needed=['libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2'],binding_kind='RELEASE_SHA_AND_VERIFIED_SOURCE_SNAPSHOT')
  return release,r
 def test_cross_complete_and_mutations(self):
  rel,r=self.fixture();self.assertEqual(audit_hybrid_cross(r,rel)['integrity'],'PASS')
  cases=[];x=copy.deepcopy(r);del x['elfs']['vxl_hunet_hybrid'];cases.append(x)
  for key,value in [('binding_sha256','8'*64),('verified_source_snapshot',False)]:x=copy.deepcopy(r);x[key]=value;cases.append(x)
  for field,value in [('machine','X86-64'),('sha256','9'*64),('needed',['libc.so.6']),('required_versions',['GLIBC_9.0'])]:x=copy.deepcopy(r);x['elfs']['vxl_hunet_hybrid'][field]=value;cases.append(x)
  for x in cases:self.assertEqual(audit_hybrid_cross(x,rel)['integrity'],'FAIL',x)
 def test_missing_kernel_cpu_object_or_old_binding(self):
  rel,r=self.fixture();k=dict(scope='PRELIMINARY_KERNEL_CROSS',exit=0,source_commit=rel['source_commit'],source_digest=rel['source_digest'],kernel_release='5.4.0-xilinx-v2020.1',vermagic='5.4.0-xilinx-v2020.1 SMP',arch='AArch64',compiler='GCC 9.2.0',W=1,Werror=True,source_bound=True,config=['CONFIG_ZONE_DMA32=y','CONFIG_64BIT=y','CONFIG_ARM64=y'],config_sha256='a'*64,symvers_sha256='b'*64,module_sha256='c'*64,source_tree='/source',kernel_build='/build',hybrid_binding_sha256=rel['binding_sha256'],hybrid_objects=list(HYBRID_OBJECTS),hybrid_symbols=['vxl_hybrid_enter_cpu','vxl_hybrid_begin_cpu','vxl_hybrid_commit_cpu','vxl_hybrid_get_cpu_evidence','vxl_hybrid_end'])
  self.assertEqual(audit_hybrid_kernel(k,rel)['integrity'],'PASS')
  for key,value in [('hybrid_objects',[]),('hybrid_binding_sha256','d'*64),('source_digest','e'*64),('hybrid_symbols',[])]:x=copy.deepcopy(k);x[key]=value;self.assertEqual(audit_hybrid_kernel(x,rel)['integrity'],'FAIL')
if __name__=='__main__':unittest.main()

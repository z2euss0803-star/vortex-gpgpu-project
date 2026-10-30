"""Metadata mutation tests only; fixture executions are never real NN evidence."""
import copy,hashlib,json,unittest
from scripts.unified.extension_audit import audit_extension
def digest(x):return hashlib.sha256(x.encode()).hexdigest()
def fixture():
 files={'driver/v2/ioctl.c':digest('ioctl'),'core/control.c':digest('legacy-control'),'core/v2/catalog.c':digest('catalog'),'include/uapi/vxl_compute_v2.h':digest('abi'),'data/nnrt/programs/catalog.bin':digest('program-directory'),'unified/model_bridge.cpp':digest('before')}
 before={'files':files,'hardware':dict(provider=1,contract=digest('S8'),bit=digest('S8bit'),xsa=digest('S8xsa'),compatible=digest('vortex,hw4-s8-demo-v1')),'operators':['quantize_i16','conv_i16','relu','add'],'programs':{'1:65568':digest('old-program')}}
 after=copy.deepcopy(before);after['files']['unified/model_bridge.cpp']=digest('new-user-model-bridge')
 package={'id':digest('network-B'),'operators':['quantize_i16','conv_i16','relu','add'],'hardware':copy.deepcopy(before['hardware']),'programs':copy.deepcopy(before['programs'])}
 manifest={'before_files':copy.deepcopy(before['files']),'after_files':copy.deepcopy(after['files']),'new_model':package['id'],'execution':{'state':'NOT_RUN','gpu_raw_cycles':None}}
 return before,after,[package],manifest
class ExtensionAudit(unittest.TestCase):
 def test_existing_operators_new_user_network_source_audit_only(self):
  r=audit_extension(*fixture());self.assertEqual(r['state'],'SOURCE_AND_OPERATOR_EXTENSION_AUDIT_ONLY');self.assertEqual(r['new_models'],1);self.assertIsNone(r['gpu_raw_cycles']);self.assertFalse(r['actual_network_execution_verified']);self.assertFalse(r['board_verified'])
 def reject(self,mutate):
  args=list(fixture());mutate(*args)
  with self.assertRaises(ValueError):audit_extension(*args)
 def test_driver_change(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('driver/v2/ioctl.c',digest('changed')))
 def test_core_removal(self):self.reject(lambda b,a,p,r:a['files'].pop('core/v2/catalog.c'))
 def test_uapi_change(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('include/uapi/vxl_compute_v2.h',digest('changed')))
 def test_new_driver_file(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('driver/new_model.c',digest('new')))
 def test_catalog_change(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('data/nnrt/programs/catalog.bin',digest('changed')))
 def test_new_operator(self):self.reject(lambda b,a,p,r:p[0]['operators'].append('unregistered'))
 def test_operator_registry_change(self):self.reject(lambda b,a,p,r:a['operators'].append('unregistered'))
 def test_same_numeric_program_id_other_code(self):self.reject(lambda b,a,p,r:p[0]['programs'].__setitem__('1:65568',digest('other-code')))
 def test_foreign_bit(self):self.reject(lambda b,a,p,r:p[0]['hardware'].__setitem__('bit',digest('P8')))
 def test_foreign_provider(self):self.reject(lambda b,a,p,r:p[0]['hardware'].__setitem__('provider',2))
 def test_stale_source_manifest(self):self.reject(lambda b,a,p,r:r['after_files'].__setitem__('unified/model_bridge.cpp',digest('wrong')))
 def test_offline_cycles_cannot_be_fabricated(self):self.reject(lambda b,a,p,r:r['execution'].__setitem__('gpu_raw_cycles',1))
 def test_build_cannot_replace_network_execution(self):
  b,a,p,r=fixture();r['execution']['state']='BUILD_PASS';out=audit_extension(b,a,p,r);self.assertFalse(out['actual_network_execution_verified']);self.assertEqual(out['execution_state'],'BUILD_PASS')
 def test_path_traversal(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('../driver/escape.c',digest('escape')))
 def test_digest_shape(self):self.reject(lambda b,a,p,r:p[0].__setitem__('id','PASS'))
 def test_malformed_operator_type(self):self.reject(lambda b,a,p,r:b.__setitem__('operators',[{'not':'an operator'}]))
 def test_missing_catalog_coverage(self):
  def mutate(b,a,p,r):
   for x in (b['files'],a['files'],r['before_files'],r['after_files']):x.pop('data/nnrt/programs/catalog.bin')
  self.reject(mutate)
 def test_missing_cycle_scope(self):self.reject(lambda b,a,p,r:r['execution'].pop('gpu_raw_cycles'))
 def test_foreign_xsa(self):self.reject(lambda b,a,p,r:p[0]['hardware'].__setitem__('xsa',digest('foreign')))
 def test_foreign_compatible(self):self.reject(lambda b,a,p,r:p[0]['hardware'].__setitem__('compatible',digest('vortex,w4-graphics-p8-v1')))
 def test_legacy_core_change(self):self.reject(lambda b,a,p,r:a['files'].__setitem__('core/control.c',digest('changed')))
if __name__=='__main__':unittest.main()

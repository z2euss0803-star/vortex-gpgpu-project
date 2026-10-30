"""R10 exact NN installation ID policy; HOST checks, no target execution."""
import ast,copy,hashlib,json,pathlib,unittest
from scripts.unified import audit_image as audit
from scripts.unified.bind_kernel import firmware_inputs
from scripts.unified.cross_common import provider
ROOT=pathlib.Path(__file__).resolve().parents[2]
NINE=['hunet','hunet-gn9','hunet-gn16','mosaic','mosaic-profile','mosaic-smoke','mosaic-smoke-profile','mosaic-b1024','mosaic-b4096']
ORIGINAL_AST_SHA='b35a754a3b69bc5b9e287e9ffc69129f7926a5af1654038dab25b57d4fb19150'
class ExactJointNnIds(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.fw=firmware_inputs(ROOT,4,mosaic_variant='reuse-v7');p=provider(ROOT,4);cls.hardware=dict(hardware_contract=p['hardware_contract'],bit=p['bit_sha256'],xsa=p['xsa_sha256'],compatible=hashlib.sha256(p['compatible'].encode()).hexdigest(),provider_flags=4)
 def files(self,ids):
  fw=self.fw;files={'usr/share/vxgpu/catalog.bin':fw['catalog'],'lib/firmware/vxl/catalog-v1.bin':fw['catalog'],'lib/firmware/vxl/execution-v1.bin':fw['execution']}
  for row in fw['entries']:files['lib/firmware/vxl/programs/'+row['entry_id']+'.bin']=row['payload']
  doc=dict(version=2,hardware=self.hardware,nn=[dict(id=name) for name in ids],catalog=dict(path='/usr/share/vxgpu/catalog.bin',sha256=hashlib.sha256(fw['catalog']).hexdigest()),image={'HOST_POLICY_FIXTURE_NOT_EXECUTED':True},p8={'HOST_POLICY_FIXTURE_NOT_EXECUTED':True},mosaic_export={'HOST_POLICY_FIXTURE_NOT_EXECUTED':True})
  raw=json.dumps(doc).encode();files['etc/vxgpud/installation.json']=raw;files['etc/vxgpud/installation.sha256']=(hashlib.sha256(raw).hexdigest()+'\n').encode();files['etc/vxgpud/config.json']=json.dumps(dict(backend_mode='compute-v2',**self.hardware)).encode();return files
 def errors(self,files):return audit.joint_payload_errors(lambda n:files.get(n,b''))
 def test_complete_explicit_nine_models_admitted_with_actual_firmware(self):
  self.assertEqual(self.errors(self.files(NINE)),[],'complete explicit nine IDs must pass unchanged actual16 firmware checks')
 def test_original_two_model_baseline_still_admitted(self):self.assertEqual(self.errors(self.files(['hunet','mosaic'])),[])
 def test_missing_duplicate_unknown_and_nearby_variant_rejected(self):
  for ids in [NINE[:-1],NINE+[NINE[0]],NINE+['foreign'],NINE[:-1]+[NINE[0]],['hunet','mosaic','mosaic-smoke'],['hunet','hunet']]:
   with self.subTest(ids=ids):self.assertTrue(self.errors(self.files(ids)))
 def test_pure_predicate_exact_sets_types_and_order(self):
  function=getattr(audit,'joint_nn_ids_valid',None);self.assertTrue(callable(function),'explicit pure ID helper required')
  for ids in [NINE,list(reversed(NINE)),['hunet','mosaic'],['mosaic','hunet']]:self.assertTrue(function(ids))
  for ids in [[],NINE[:-1],NINE+[NINE[0]],NINE+['foreign'],['hunet','hunet'],['hunet',True],None,{},tuple(NINE),'hunet,mosaic']:self.assertFalse(function(ids))
 def test_existing_binary_hardware_config_and_seal_checks_still_reject(self):
  for name in ['usr/share/vxgpu/catalog.bin','lib/firmware/vxl/execution-v1.bin','etc/vxgpud/installation.sha256',next(n for n in self.files(NINE) if n.startswith('lib/firmware/vxl/programs/'))]:
   files=self.files(NINE);raw=bytearray(files[name]);raw[16 if name=='lib/firmware/vxl/execution-v1.bin' else len(raw)//2]^=1;files[name]=bytes(raw)
   with self.subTest(name=name):self.assertTrue(self.errors(files))
  files=self.files(NINE);config=json.loads(files['etc/vxgpud/config.json']);config['bit']='00'*32;files['etc/vxgpud/config.json']=json.dumps(config).encode();self.assertTrue(self.errors(files))
 def test_entire_original_auditor_ast_changes_only_exact_id_predicate(self):
  tree=ast.parse(pathlib.Path(audit.__file__).read_text());helpers=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='joint_nn_ids_valid'];self.assertEqual(len(helpers),1);tree.body=[n for n in tree.body if n not in helpers]
  class Restore(ast.NodeTransformer):
   count=0
   def visit_UnaryOp(self,node):
    if isinstance(node.op,ast.Not) and isinstance(node.operand,ast.Call) and isinstance(node.operand.func,ast.Name) and node.operand.func.id=='joint_nn_ids_valid':
     self.count+=1;source=node.operand.args[0];generator=ast.GeneratorExp(elt=source.elt,generators=source.generators);return ast.Compare(left=ast.Call(func=ast.Name(id='sorted',ctx=ast.Load()),args=[generator],keywords=[]),ops=[ast.NotEq()],comparators=[ast.List(elts=[ast.Constant('hunet'),ast.Constant('mosaic')],ctx=ast.Load())])
    return self.generic_visit(node)
  restore=Restore();tree=restore.visit(tree);self.assertEqual(restore.count,1);self.assertEqual(hashlib.sha256(ast.dump(tree,include_attributes=False).encode()).hexdigest(),ORIGINAL_AST_SHA)
if __name__=='__main__':unittest.main()

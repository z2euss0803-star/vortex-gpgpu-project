"""Controller evidence rules: transport success cannot invent physical PASS."""
import copy,json,pathlib,tempfile,unittest,hashlib,os
from scripts.unified.joint_loop import classify,poll,ServiceError,Board,run
from scripts.unified.joint_conversion import verify_conversion,identity,wire

class JointController(unittest.TestCase):
 def test_applied_host_and_missing_physical_cycles_never_claim_board(self):
  source=dict(scope='OFFLINE',cycle_valid=False,raw_cycles=None,numeric_class='NOT_EVALUATED');display=dict(applied=True,gpu_raw_cycles=None,clock_domain='HOST_MONOTONIC')
  self.assertFalse(classify(source,display)['physical_execution_and_presentation'])
  source.update(scope='REAL_BOARD',cycle_valid=True,raw_cycles=123,numeric_class='KNOWN_NUMERIC_DEVIATION');display.update(gpu_raw_cycles=456,clock_domain='LINUX_MONOTONIC')
  result=classify(source,display);self.assertTrue(result['physical_execution_and_presentation']);self.assertFalse(result['neural_numeric_pass'])
  for key,value in [('applied',False),('gpu_raw_cycles',None),('clock_domain','HOST_MONOTONIC')]:
   bad=dict(display);bad[key]=value;self.assertFalse(classify(source,bad)['physical_execution_and_presentation'])
 def test_expected_package_comes_from_independent_conversion_receipt(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);producer=dict(boot_id='boot',client_id='b',request_id='original');result_sha='11'*32;tensor_sha='22'*32;package='33'*32
   meta=dict(package_id=package,source=dict(result_sha256=result_sha,tensor_sha256=tensor_sha),lods=[dict(name='lod512.rgba',sha256='44'*32),dict(name='lod2048.rgba',sha256='55'*32)])
   p=root/'manifest.json';p.write_bytes(wire(meta));p.chmod(0o400)
   proof=dict(schema='s8-joint-trusted-b-conversion-v1',natural_exit=0,producer=producer,source_result_sha256=result_sha,source_tensor_sha256=tensor_sha,tools_before={'frozen':'same'},tools_after={'frozen':'same'},gpu_raw_cycles=None,package_id=package,pillow={'version':'8.4.0'},files={'manifest.json':identity(p)},lod_sha256={x['name']:x['sha256'] for x in meta['lods']})
   self.assertEqual(verify_conversion(proof,root,producer,result_sha,tensor_sha),package)
   # Mutate only the pending package to an internally consistent new identity;
   # this must never rewrite or supply the trusted external expectation.
   p.chmod(0o600);meta['package_id']='66'*32;meta['lods'][0]['sha256']='77'*32;p.write_bytes(wire(meta));p.chmod(0o400)
   with self.assertRaises(ValueError):verify_conversion(proof,root,producer,result_sha,tensor_sha)
   p.chmod(0o600)

class ReadOnlyStatusAndFailureEvidence(unittest.TestCase):
 def test_existing_result_not_complete_is_never_resubmitted(self):
  class Existing:
   def __init__(self):self.calls=[]
   def rpc(self,command,*args,**kwargs):
    self.calls.append(command)
    if command=='mode-status':return dict(current='nn',switching=False)
    if command=='status':return dict(status='RUNNING')
    return {}
  root=pathlib.Path(__file__).resolve().parents[2]/'build/joint-reuse-tests';root.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(root)) as temp:
   peer=Existing()
   with self.assertRaises(ValueError):run(peer,temp,dict(boot_id='boot',client_id='c',request_id='existing'),1,reuse_existing=True)
   self.assertNotIn('submit',peer.calls)
   receipt=json.loads((pathlib.Path(temp)/'loop-receipt.json').read_text());self.assertEqual(receipt['natural_exit'],1)
   (pathlib.Path(temp)/'loop-receipt.json').chmod(0o600)
 def test_accepted_texture_status_busy_then_applied_never_resubmits(self):
  calls=[]
  def query():
   calls.append('READ_ONLY_TEXTURE_STATUS')
   if len(calls)==1:raise ServiceError(dict(code=11,category='NATIVE_P8_SERVICE',detail='scene worker busy'))
   return dict(phase='APPLIED',applied=True)
  result=poll(query,lambda x:x['phase']=='APPLIED',2,'accepted texture')
  self.assertTrue(result['applied']);self.assertEqual(calls,['READ_ONLY_TEXTURE_STATUS','READ_ONLY_TEXTURE_STATUS'])
 def test_unknown_status_error_is_not_retried(self):
  calls=[]
  def query():calls.append('STATUS');raise ServiceError(dict(code=5,detail='original error'))
  with self.assertRaises(ServiceError):poll(query,lambda x:True,2,'unknown status')
  self.assertEqual(calls,['STATUS'])
 def test_ssh_timeout_keeps_partial_raw_bytes_and_unknown_receipt(self):
  import subprocess
  from unittest import mock
  with tempfile.TemporaryDirectory() as d:
   board=Board('board',None,'/run/vxgpud/control.sock',d)
   with mock.patch('scripts.unified.joint_loop.subprocess.run',side_effect=subprocess.TimeoutExpired(['ssh'],1,output=b'partial stdout',stderr=b'partial stderr')):
    with self.assertRaises(subprocess.TimeoutExpired):board.command(['true'],timeout=1)
   self.assertEqual((pathlib.Path(d)/'0001-stdout.bin').read_bytes(),b'partial stdout')
   self.assertEqual((pathlib.Path(d)/'0001-stderr.bin').read_bytes(),b'partial stderr')
   receipt=json.loads((pathlib.Path(d)/'0001-receipt.json').read_bytes());self.assertTrue(receipt['timeout']);self.assertIsNone(receipt['natural_exit']);self.assertEqual(receipt['remote_outcome'],'UNKNOWN')

if __name__=='__main__':unittest.main()

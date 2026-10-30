import copy,hashlib,json,pathlib,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_fused'))
from rtl_check import validate_receipt,normal_log_result
import rtl_check
class RTLReceipt(unittest.TestCase):
 def test_raw_artifacts_are_required_and_rehashed(self):
  # Synthetic transport/schema fixture, never recorded as actual RTL evidence.
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)
   def put(name,b):
    p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);return hashlib.sha256(b).hexdigest()
   def write_receipt(r):(root/'receipt.json').write_text(json.dumps(r))
   inputs={'kernel.elf':put('kernel.elf',b'fixture-elf'),'rtl/core.sv':put('rtl/core.sv',b'fixture-rtl')}
   raw=json.dumps(inputs,sort_keys=True).encode();input_sha=put('raw-inputs.json',raw)
   r=dict(scope='ACTUAL_HW4_RTL_EXECUTION',phase='simulation',result='FUSED_MULTICORE_RTL_PASS',elf_sha256=inputs['kernel.elf'],rtl_sha256=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest(),input_manifest_path='raw-inputs.json',input_manifest_sha256=input_sha,cores=4,warps=4,lanes=4,natural_exit=0,fatal=False,timeout=False,cases=[],compile=[])
   for name in ('xvlog','xvhdl','xelab'):
    log=name+'.console.log';r['compile'].append(dict(name=name,exit=0,log=log,log_sha256=put(log,b'fixture compile completed\r\n')))
   for name in sorted(rtl_check.REQUIRED):
    fault=name=='memory_fault';log=name+'/xsim.log';wave=name+'/fused.vcd'
    text=b'FUSED_TB_RESET_BOOTSTRAP_RELEASED\r\n'+(b'FUSED_MEMORY_FAULT_INJECTED\r\nFatal: RTL_CYCLE_TIMEOUT\r\n' if fault else b'FUSED_MULTICORE_RTL_PASS job=0 cycles=12 barrier_mask=15\r\n')
    if name=='reuse_b':text+=b'FUSED_TWO_LAUNCH_REUSE_PASS\r\n'
    r['cases'].append(dict(name=name,exit=0,semantic_pass=not fault,barrier_mask=15,output_equal=not fault,log=log,log_sha256=put(log,text),wave=wave,wave_sha256=put(wave,b'$enddefinitions $end\n'),assertion_errors=0,reset_bootstrap_released=True,launches=2 if name=='reuse_b' else 1,fault_observed=fault,expected_rejection=fault))
   write_receipt(r)
   with self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
   case_inputs=[]
   for c in r['cases']:
    name=c['name'];case_inputs.extend([name+'/memory.txt',name+'/gold.txt',name+'/options.txt','xsim-'+name+'-command.json'])
    if name=='reuse_b':case_inputs.extend([name+'/second/memory.txt',name+'/second/gold.txt'])
   for name in case_inputs:inputs[name]=put(name,b'fixture case input/command\n')
   raw=json.dumps(inputs,sort_keys=True).encode();r['input_manifest_sha256']=put('raw-inputs.json',raw);r['rtl_sha256']=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
   write_receipt(r);rtl_check.validate_artifacts(root)
   for name in case_inputs:
    p=root/name;saved=p.read_bytes();p.write_bytes(b'changed')
    with self.subTest(input=name),self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
    p.write_bytes(saved)
   c=next(x for x in r['cases'] if x['name']=='one_descriptor');p=root/c['log'];saved=p.read_bytes();p.write_bytes(saved+b'Error: runtime error: counter overflow\n')
   with self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
   c['log_sha256']=hashlib.sha256(p.read_bytes()).hexdigest();write_receipt(r)
   with self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
   p.write_bytes(saved);c['log_sha256']=hashlib.sha256(saved).hexdigest();write_receipt(r)
   wave=root/c['wave'];original=wave.read_bytes();wave.write_bytes(b'changed')
   with self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
   wave.write_bytes(original);(root/'rtl/core.sv').write_bytes(b'changed')
   with self.assertRaises(ValueError):rtl_check.validate_artifacts(root)
 def test_exact_xsim_runtime_assertions_are_failures(self):
  for message in ('ERROR: Could not open VCD file','Error: runtime error: counter overflow','Error: runtime error: counter underflow','Fatal: assertion','assertion failed','RTL_CYCLE_TIMEOUT'):
   with self.subTest(message=message),self.assertRaises(ValueError):normal_log_result(message+'\nFUSED_MULTICORE_RTL_PASS job=0 cycles=12 barrier_mask=15')
  self.assertTrue(normal_log_result('FUSED_MULTICORE_RTL_PASS job=0 cycles=12 barrier_mask=15'))
 def test_requires_actual_elf_four_cores_and_natural_exit(self):
  # A schema fixture is never saved as hardware evidence.
  good=dict(scope='ACTUAL_HW4_RTL_EXECUTION',elf_sha256='a'*64,rtl_sha256='b'*64,
   cores=4,warps=4,lanes=4,natural_exit=0,phase='simulation',
   cases=[dict(name=n,exit=0,semantic_pass=True,barrier_mask=15,output_equal=True,log_sha256='c'*64,wave_sha256='d'*64,assertion_errors=0,reset_bootstrap_released=True)
    for n in ('one_descriptor','32_descriptors','idle_cores','delayed_cores','backpressure',
              'tail','reuse_b','max_grid','memory_fault')],fatal=False,timeout=False)
  for case in good['cases']:
   if case['name']=='reuse_b':case['launches']=2
   if case['name']=='memory_fault':case.update(semantic_pass=False,fault_observed=True,expected_rejection=True)
  validate_receipt(good)
  for key,value in [('elf_sha256',None),('rtl_sha256',''),('cores',1),('natural_exit',None),
                    ('phase','elaboration'),('fatal',True),('timeout',True)]:
   bad=copy.deepcopy(good);bad[key]=value
   with self.assertRaises(ValueError):validate_receipt(bad)
  for key,value in [('barrier_mask',7),('output_equal',False),('semantic_pass',False),('exit',None),('assertion_errors',1),('reset_bootstrap_released',False),('log_sha256',''),('wave_sha256','')]:
   bad=copy.deepcopy(good);bad['cases'][0][key]=value
   with self.assertRaises(ValueError):validate_receipt(bad)
  bad=copy.deepcopy(good);bad['cases'].pop()
  with self.assertRaises(ValueError):validate_receipt(bad)
if __name__=='__main__':unittest.main()

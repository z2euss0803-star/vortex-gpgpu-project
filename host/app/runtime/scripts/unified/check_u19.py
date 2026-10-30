"""U19 typed events and policy tests; synthetic counters never physical evidence."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','adversarial-red','audit-red','boundary-red','normal','sanitize','thread','cross')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U19_UBUNTU_EVENT_AND_POLICY_CONTRACTS_NOT_GPU_OR_APPLICATION_PERFORMANCE',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=list(map(str,argv));begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4500:],flush=True)
  assert p.returncode==expected,'natural exit '+str(p.returncode)
  assert not any(s in text for s in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  elfs=[out/'test_unified_timing',out/'test_optimization',out/'test_evidence_adversarial',out/'test_original_event_timing'];run(cmd+elfs+['-j'+str(jobs)])
  for elf in elfs:
   raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='red':
   assert 'U19_TIMING_ASSERT complete bound event serialization required' in run([elfs[0]],1)
   assert 'U19_OPT_ASSERT default all-false baseline must admit' in run([elfs[1]],1)
   rec['functional_red']='BOUND_EVIDENCE_AND_SINGLE_VARIABLE_POLICY_MISSING'
  elif mode=='adversarial-red':
   for case,assertion in [('duplicate-counter','same GPU job raw counter must not be added twice'),('pending-lifetime','pending exact evidence and writer must survive consumer destruction'),('weighted','bounded nonpreemptive weighted ordering required')]:
    assert 'U19_TIMING_ASSERT '+assertion in run([elfs[2],case],1)
   rec['functional_red']='DUPLICATED_COUNTER_PENDING_EVENT_LIFETIME_AND_WEIGHTED_ORDER'
  elif mode=='audit-red':
   fixture=out/'event.jsonl';run([elfs[0],fixture]);os.environ['U19_EVENT_FIXTURE']=str(fixture)
   text=run(['python3','-m','unittest','tests.unified.test_performance_audit.PerformanceAudit.test_roundtrip','-v'],1);assert 'U19_AUDIT_ASSERT bound-event groups required' in text;rec['functional_red']='STRICT_BOUND_EVENT_AUDIT_MISSING'
  elif mode=='boundary-red':
   for case,assertion in [('constant-alias','same immutable reference cannot claim contradictory content'),('zero-parent','serialized parent event zero must be rejected')]:assert 'U19_TIMING_ASSERT '+assertion in run([elfs[2],case],1)
   fixture=out/'event.jsonl';run([elfs[0],fixture]);os.environ['U19_EVENT_FIXTURE']=str(fixture)
   text=run(['python3','-m','unittest','tests.unified.test_performance_audit.PerformanceAudit.test_configured_estimate_overflow','-v'],1);assert 'U19_AUDIT_ASSERT configured estimate overflow must be refused' in text;rec['functional_red']='CONTRADICTORY_CONSTANT_RANGE_ZERO_PARENT_CONFIGURED_ESTIMATE_OVERFLOW'
  elif mode=='cross':
   needed=set();versions=set()
   for elf in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   run(cmd+['test-unified-evidence','test-unified-optimization','-j1'])
   for case in ('duplicate-counter','pending-lifetime','weighted','constant-concurrent','constant-alias','zero-parent'):run([elfs[2],case])
   fixture=out/'event.jsonl';run([elfs[0],fixture]);os.environ['U19_EVENT_FIXTURE']=str(fixture);run(['python3','-m','unittest','tests.unified.test_performance_audit','-v'])
   os.environ['NNRT_TIMING_TEST_BINARY']=str(elfs[3]);run(['python3','-m','unittest','tests.nnrt.test_event_timing','tests.nnrt.test_evidence','-v'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

"""P8 frozen control migration gate, never GPU/render/DP acceptance."""
import json,os,pathlib,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread','frame-red','frame-normal','frame-sanitize','adapter-red','adapter-normal','adapter-sanitize','policy-red','policy-normal','policy-sanitize','paused-red');out=pathlib.Path('build')/name;out.mkdir(exist_ok=False);rec=dict(scope='P8_CONTROL_STATE_SOFTWARE_ONLY',mode=mode,gpu_raw_cycles=None,commands=[])
 def run(argv,expected=0):
  begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));text=p.stdout.decode('utf8','replace');print(text[-3000:],flush=True);assert p.returncode==expected;assert not any(x in text for x in ['ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:']);return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];
  if mode in ('sanitize','thread','frame-sanitize','adapter-sanitize','policy-sanitize'):cmd+=['SANITIZE='+('thread' if mode=='thread' else 'address,undefined')]
  target='test_p8_adapter' if mode=='paused-red' else 'test_p8_execution_policy' if mode.startswith('policy-') else 'test_p8_adapter' if mode.startswith('adapter-') else 'test_p8_frame' if mode.startswith('frame-') else 'test_p8_controls'
  run(cmd+[out/target,'-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);args=['data/p8'] if (mode.startswith('frame-') or mode.startswith('adapter-') or mode=='paused-red') else []
  if mode.startswith('policy-'):args=['data/p8/programs/catalog.bin','data/p8/programs/'+json.loads(pathlib.Path('data/p8/programs/manifest.json').read_text())['programs'][0]['entry_id']+'.bin','data/p8/programs/execution-v2.bin']
  text=run([out/target]+args,1 if mode in ('red','frame-red','adapter-red','policy-red','paused-red') else 0)
  if mode=='paused-red':assert 'U16_P8_ADAPTER_ASSERT paused frame cannot be admitted' in text;rec['functional_red']='COMPILED_PAUSED_FRAME_ADMISSION'
  elif mode=='policy-red':assert 'U16_POLICY_ASSERT native P8 global DONE and16384tile binding required' in text;rec['functional_red']='COMPILED_NATIVE_GLOBAL_POLICY_BINDING_MISSING'
  elif mode=='adapter-red':assert 'U16_P8_ADAPTER_ASSERT exact P8 adapter admission' in text;rec['functional_red']='COMPILED_NATIVE_P8_ADMISSION_MISSING'
  elif mode=='frame-red':assert 'U16_FRAME_ASSERT frozen frame assets must load' in text;rec['functional_red']='COMPILED_NATIVE_ASSETS_MISSING'
  elif mode=='red':assert 'U16_ASSERT startup controls must stay gated' in text;rec['functional_red']='COMPILED_FROZEN_STARTUP_GATE_MISSING'
  else:assert ('U16_NATIVE_GLOBAL_DONE_POLICY_HOST_REGISTER_FIXTURE_PASS_NOT_GPU_BOARD' if mode.startswith('policy-') else 'U16_P8_CATALOG_AND_PLAN_PASS_NOT_GPU_DP' if mode.startswith('adapter-') else 'U16_FROZEN_P8_FRAME_PREPARE_PASS_CPU_ONLY_NOT_GPU_DP' if mode.startswith('frame-') else 'U16_P8_FROZEN_CONTROL_STATE_PASS_HOST_NOT_GPU_DISPLAY') in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

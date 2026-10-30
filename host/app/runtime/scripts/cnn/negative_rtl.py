"""Independent non-release bad-tail kernel; expected real RTL rejection only."""
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess
from pathlib import Path
from generate_kernels import generate_program
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def bad_tail(code):
 if code!=generate_program(2):raise ValueError('Require canonical ID2 before deliberate mutation')
 words=[int.from_bytes(code[i:i+4],'little') for i in range(0,4096,4)]
 indexes=[i for i,w in enumerate(words) if w==0x0486bb93]
 if indexes!=[13,73]:raise ValueError('Unexpected canonical round/tail bounds')
 words[73]=0x0406bb93
 return b''.join(w.to_bytes(4,'little') for w in words)
def prepare(positive,out):
 try:result=json.loads((positive/'result.json').read_text())
 except (OSError,ValueError):raise ValueError('Require completed actual positive RTL receipt')
 if result.get('pass') is not True:raise ValueError('Positive RTL not passed')
 s=importlib.util.spec_from_file_location('positive_rtl_receipt',ROOT/'validation/h0/receipt.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 errors=m.verify(positive,result)
 if errors:raise ValueError('Positive receipt invalid: '+'; '.join(errors))
 compiled=positive
 if 'producer_result' in result:
  producer=Path(result['producer_result'])
  if sha(producer)!=result['producer_result_sha256']:raise ValueError('Positive producer identity')
  compiled=producer.parent
  if sha(compiled/'compiled-binding.json')!=result['compiled_binding_sha256']:raise ValueError('Positive compiled snapshot identity')
  for name,digest in json.loads((compiled/'compiled-binding.json').read_text())['artifacts'].items():
   if sha(compiled/name)!=digest:raise ValueError('Positive compiled artifact changed')
 out.mkdir(parents=True,exist_ok=False)
 shutil.copytree(str(compiled/'xsim.dir'),str(out/'xsim.dir'))
 for name in ('programs','fixtures'):shutil.copytree(str(positive/name),str(out/name))
 for path in compiled.iterdir():
  if path.is_file() and (path.suffix=='.mem' or path.name=='xsim.ini'):shutil.copyfile(str(path),str(out/path.name))
 (out/'outputs').mkdir();(out/'tmp').mkdir()
 (out/'programs/id2.bin').write_bytes(bad_tail((out/'programs/id2.bin').read_bytes()))
 stable=set(json.loads((compiled/'compiled-binding.json').read_text())['artifacts'])
 copied={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and (str(p.relative_to(out)) in stable or p.relative_to(out).parts[0] in ('programs','fixtures') or p.parent==out and (p.suffix=='.mem' or p.name=='xsim.ini'))}
 binding=dict(scope='NONCANONICAL_BAD_TAIL_TEST_NOT_RELEASE',positive_receipt=str(positive/'result.json'),positive_receipt_sha256=sha(positive/'result.json'),source_commit=result['source_commit'],mutation=dict(id=2,word_index=73,from_='0x0486bb93',to='0x0406bb93',expected_missing_output_indices=list(range(64,72))),copied_inputs=copied,board='NOT_RUN',sd='NOT_TOUCHED')
 (out/'negative-inputs.json').write_text(json.dumps(binding,indent=2)+'\n');return binding
def run(positive,out):
 binding=prepare(positive,out);env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64';env['TMP']=env['TEMP']=str(out/'tmp')
 others=subprocess.check_output(['powershell.exe','-NoProfile','-Command','@(Get-Process xsimk,xelab,vrfc -ErrorAction SilentlyContinue | Select-Object Id) | ConvertTo-Json -Compress']).decode().strip()
 if others:raise ValueError('Another simulator active; retain preparation, do not kill it')
 command=['G:/xilinx/Vivado/2020.1/bin/xsim.bat','cnn_h0','-runall','-onfinish','quit','-log','simulate_native.log']
 with (out/'simulate.log').open('w') as f:process=subprocess.run(command,cwd=str(out),env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT)
 log=(out/'simulate.log').read_text(errors='replace')
 rejected=process.returncode!=0 and 'CNN_FAIL OBJECT_GOLDEN_OR_GUARD index=80 value=deadbeef' in log and 'CNN_HOST_FAILED' in log and 'CNN_FULL_SOC_PASS' not in log
 changed=[name for name,digest in binding['copied_inputs'].items() if sha(out/name)!=digest]
 result=dict(scope='CNN_ACTUAL_RTL_EXPECTED_BAD_TAIL_REJECTION',pass_=rejected and not changed,exit=process.returncode,command=command,log_sha256=sha(out/'simulate.log'),input_changes=changed,source_commit=binding['source_commit'],mutation=binding['mutation'],board='NOT_RUN',h1='NOT_RUN')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 if not result['pass_']:raise ValueError('Expected real RTL tail failure not established')
 print('CNN_BAD_TAIL_ACTUAL_RTL_REJECTED',flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--positive',required=True);a.add_argument('--out',required=True);o=a.parse_args();run(Path(o.positive).resolve(),Path(o.out).resolve())

"""Strict post-run audit with separately bound compiler products; preserves producer receipts."""
import argparse,hashlib,importlib.util,json,shutil
from pathlib import Path
from freeze_rtl_artifacts import unexpected_changes
from generate_kernels import generate_program
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def program_errors(programs):
 return ['canonical program inventory'] if set(programs)!={2,3,4,5} else ['noncanonical ID%d'%i for i in (2,3,4,5) if programs[i]!=generate_program(i)]
def audit(wd,out):
 wd=wd.resolve();out=out.resolve()
 result=json.loads((wd/'result.json').read_text());inputs=json.loads((wd/'inputs.json').read_text());binding=json.loads((wd/'compiled-binding.json').read_text())
 if binding['scope']!='CNN_COMPILED_ARTIFACT_BINDING_NOT_EXECUTION_PASS' or sha(wd/'inputs.json')!=binding['inputs_sha256'] or result['source_commit']!=binding['source_commit']:raise ValueError('Compiled input identity')
 changes=[Path(p['path']) for p in inputs['inputs'] if sha(Path(p['path']))!=p['sha256']]
 if unexpected_changes(wd,changes) or set(map(str,changes))!=set(binding['compiler_owned_changes']):raise ValueError('Unbound input changes')
 for name,digests in binding['compiler_owned_changes'].items():
  item=next(p for p in inputs['inputs'] if p['path']==name)
  if item['sha256']!=digests['before'] or sha(Path(name))!=digests['after']:raise ValueError('Compiler product mismatch')
  item['sha256']=digests['after'];item['role']='GENERATED_COMPILER_ARTIFACT'
 for name,digest in binding['artifacts'].items():
  if sha(wd/name)!=digest:raise ValueError('Compiled snapshot changed '+name)
 for name,digest in binding['processes'].items():
  if sha(wd/(name+'.process.json'))!=digest:raise ValueError('Compiler process changed')
 errors=program_errors({i:(wd/'programs'/('id%d.bin'%i)).read_bytes() for i in (2,3,4,5)})
 if errors:raise ValueError('; '.join(errors))
 out.mkdir(parents=True,exist_ok=False)
 for name in ('fixtures','programs','outputs'):shutil.copytree(str(wd/name),str(out/name))
 for name in set(p['log'] for p in result['stages'].values())|{result['waveform']['path']}:shutil.copyfile(str(wd/name),str(out/name))
 (out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
 shutil.copyfile(str(wd/'compiled-binding.json'),str(out/'compiled-binding.json'))
 s=importlib.util.spec_from_file_location('positive_rtl_receipt',ROOT/'validation/h0/receipt.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 errors=m.verify(out,result)
 result.update(pass_=not errors,errors=errors,producer_result=str(wd/'result.json'),producer_result_sha256=sha(wd/'result.json'),compiled_binding_sha256=sha(wd/'compiled-binding.json'),input_role_correction='Only exact three compiler-owned library cache/index paths; all other inputs immutable',auditor_sha256=sha(Path(__file__)),board='NOT_RUN',h1='NOT_RUN')
 result['pass']=not errors
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 if errors:raise ValueError('; '.join(errors))
 print('CNN_FULL_SOC_RTL_INDEPENDENT_AUDIT_PASS_NOT_BOARD',flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--workdir',required=True);a.add_argument('--out',required=True);o=a.parse_args();audit(Path(o.workdir),Path(o.out))

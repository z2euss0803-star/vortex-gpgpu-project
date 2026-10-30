"""U20 aggregate Ubuntu gate. Serial test groups, separate sanitizer outputs."""
import pathlib,subprocess,json,sys,os,time,hashlib,re,shlex
from scripts.unified.check_u11 import sha,artifact

FOUNDATION=['test-v2-objects','test-v2-buffer-adapter','test-v2-program','test-v2-relocate','test-v2-catalog-driver','test-v2-private-init','test-v2-jobs','test-v2-batch','test-v2-close','test-v2-poll','test-v2-dispatch','test-v2-execution','test-v2-provider','test-v2-control','test-v2-predicates','test-unified-wire','test-unified-identity','test-unified-resources','test-unified-catalog-builder','test-unified-backend','test-unified-package','test-unified-registry','test-unified-graph','test-unified-memory-plan','test-unified-package-legacy','test-unified-cpu','test-unified-gelu','test-unified-scheduler','test-unified-cancel','test-unified-budget','test-unified-token-transfer','test-unified-scheduler-faults','test-unified-scheduler-alloc','test-unified-gelu-token','test-unified-int16','test-unified-int16-entry','test-unified-int16-registry','test-unified-extension-plan']

FOUNDATION+=['test-unified-image-fill','test-unified-backend-cookie-order','test-unified-cookie-history']

def source_identity():
 rows=[]
 for folder,dirs,files in os.walk('.'):
  dirs[:]=sorted(d for d in dirs if d not in ('build','evidence','.git','.superpowers','__pycache__'))
  for name in sorted(files):
   p=pathlib.Path(folder)/name
   if p.is_file():rows.append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
 raw=json.dumps(rows,sort_keys=True,separators=(',',':')).encode();return dict(files=len(rows),bytes=sum(x['bytes'] for x in rows),sha256=hashlib.sha256(raw).hexdigest())

def matrix():
 p=pathlib.Path('docs/unified/regression-matrix.json');m=json.loads(p.read_text());groups=m['groups'];assert m['version']==1 and m['gpu_raw_cycles'] is None and m['board']=='NOT_RUN' and m['test_runner_jobs']==1 and m['compiler_jobs_max']==2
 assert len(groups)==14 and len({g['id'] for g in groups})==14
 for g in groups:
  assert re.fullmatch('[a-z][a-z0-9-]{0,31}',g['id']) and set(g['modes'])<={'normal','sanitize','thread'} and 'normal' in g['modes'] and g['coverage']
  if g['module']:assert pathlib.Path(g['module'].replace('.','/')+'.py').is_file()
 assert all('thread' in next(g for g in groups if g['id']==name)['modes'] for name in ('foundation','stage','native-media','installed','p8-public','s8-joint','integer-extension','display','events'))
 return m,sha(p)

def main():
 name,mode=sys.argv[1:3];assert re.fullmatch('[A-Za-z0-9][A-Za-z0-9_-]{0,90}',name) and mode in ('normal','sanitize','thread')
 selected=sys.argv[3:];m,msha=matrix();assert not selected or all(x in {g['id'] for g in m['groups']} for x in selected)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);before=source_identity();record=dict(scope=m['scope'],mode=mode,matrix_sha256=msha,source_before=before,groups=[],commands=[],gpu_raw_cycles=None,board='NOT_RUN',image='NOT_BUILT',sd='NOT_TOUCHED',complete_matrix=not bool(selected))
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(record['commands']));start=time.monotonic()
  with log.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  text=log.read_text(errors='replace');row=dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log));record['commands'].append(row);(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(text[-2400:],flush=True)
  assert p.returncode==0,'aggregate child failed; first raw log retained: '+str(log)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','FATAL: ThreadSanitizer','runtime error:'))
  return text
 try:
  record['tools']={tool:run([tool,'--version']).splitlines()[0] for tool in ('gcc','g++')}
  record['host_machine']=run(['gcc','-dumpmachine']).strip();assert record['host_machine']=='x86_64-linux-gnu','host software gate must not execute target binaries'
  run([sys.executable,'-m','unittest','tests.unified.test_extension_audit','tests.unified.test_native_origin','tests.unified.test_software_matrix','-v'])
  for group in m['groups']:
   if mode not in group['modes'] or (selected and group['id'] not in selected):continue
   child_name=name+'/groups/'+group['id'];child=pathlib.Path('build')/child_name;assert not child.exists();print('U20_GROUP_START '+group['id']+' '+mode,flush=True);start=time.monotonic()
   if group['module']:
    child_mode=group.get('normal_mode','normal') if mode=='normal' else mode
    run([sys.executable,'-m',group['module'],child_name,child_mode]);p=child/'receipt.json';receipt=json.loads(p.read_text());assert receipt['natural_exit']==0 and 'gpu_raw_cycles' in receipt and receipt['gpu_raw_cycles'] is None
    row=dict(id=group['id'],receipt=str(p),receipt_sha256=sha(p),command_count=len(receipt['commands']),scope=receipt['scope'],elapsed_s=time.monotonic()-start)
   else:
    child.mkdir(parents=True);build=['make','-f','Makefile.unified','OUT='+str(child),'DEBUG_FLAGS=-g1']
    if mode!='normal':build+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
    planned=run(build+FOUNDATION+['-n','-j1']);executables=set()
    for line in planned.splitlines():
     try:args=shlex.split(line)
     except ValueError:continue
     if args and args[0] in ('cc','gcc','g++','c++') and '-o' in args:
      binary=args[args.index('-o')+1]
      if binary.startswith(str(child)+'/') and not binary.endswith('.o'):executables.add(binary)
    assert len(executables)>=20,'actual foundation compilation plan required'
    run(build+sorted(executables)+['-j'+str(jobs)]);run(build+FOUNDATION+['-j1']);row=dict(id=group['id'],scope='REAL_C_CORE_AND_REGISTERED_RUNTIME_HOST_TESTS',compiled_executables=len(executables),elapsed_s=time.monotonic()-start)
   assert source_identity()==before,'frozen source changed during aggregate gate'
   record['groups'].append(row);(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print('U20_GROUP_COMPLETE '+group['id']+' natural_exit=0',flush=True)
  expected=[g['id'] for g in m['groups'] if mode in g['modes'] and (not selected or g['id'] in selected)];assert [g['id'] for g in record['groups']]==expected
  record.update(natural_exit=0,source_after=source_identity(),selected_groups=expected);assert record['source_after']==before
  print('U20_AGGREGATE_'+mode.upper()+'_SOFTWARE_PASS_NOT_TARGET_IMAGE_OR_BOARD gpu_raw_cycles=null',flush=True)
 except BaseException as error:record.update(natural_exit=1,error=repr(error));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()

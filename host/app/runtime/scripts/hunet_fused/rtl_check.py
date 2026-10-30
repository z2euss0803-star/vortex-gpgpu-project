"""Matched HW4 XSim execution. All artifacts are per-run; no board actions."""
import argparse,hashlib,json,os,pathlib,re,shutil,struct,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
from audit_program import elf_info
REQUIRED={'one_descriptor','32_descriptors','idle_cores','delayed_cores','backpressure','tail','reuse_b','max_grid','memory_fault'}

def normal_log_result(log):
 if re.search(r'(?im)^\s*(?:Error:|Fatal:)|fatal|assertion failed|timeout',log) or 'FUSED_MULTICORE_RTL_PASS' not in log:raise ValueError('RTL_LOG_ASSERTION_OR_INCOMPLETE')
 return True
def validate_receipt(r):
 if r.get('scope')!='ACTUAL_HW4_RTL_EXECUTION' or r.get('phase')!='simulation':raise ValueError('NOT_RTL_EXECUTION')
 for key in ('elf_sha256','rtl_sha256'):
  if not re.fullmatch('[0-9a-f]{64}',r.get(key) or ''):raise ValueError('MISSING_IDENTITY')
 if [r.get(x) for x in ('cores','warps','lanes')]!=[4,4,4] or r.get('natural_exit')!=0 or r.get('fatal') or r.get('timeout'):raise ValueError('EXIT_OR_CONFIGURATION')
 if len(r['cases'])!=len(REQUIRED) or {x['name'] for x in r['cases']}!=REQUIRED:raise ValueError('INCOMPLETE_CASES')
 for case in r['cases']:
  if case['name']=='memory_fault':
   if case.get('exit') is None or not case.get('fault_observed') or not case.get('expected_rejection') or case.get('semantic_pass'):raise ValueError('FAULT_NOT_REJECTED')
  elif case.get('exit')!=0 or case.get('semantic_pass') is not True or case.get('barrier_mask')!=15 or case.get('output_equal') is not True:raise ValueError('CASE_FAILURE')
  if not re.fullmatch('[0-9a-f]{64}',case.get('log_sha256','')) or not re.fullmatch('[0-9a-f]{64}',case.get('wave_sha256','')) or case.get('assertion_errors')!=0 or case.get('reset_bootstrap_released') is not True:raise ValueError('RAW_LOG_WAVE_OR_RESET_BOOTSTRAP')
  if case['name']=='reuse_b' and case.get('launches')!=2:raise ValueError('REUSE_NOT_EXECUTED')
 return r
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def case_input_paths(name):
 files=[name+'/memory.txt',name+'/gold.txt',name+'/options.txt','xsim-'+name+'-command.json']
 if name=='reuse_b':files.extend([name+'/second/memory.txt',name+'/second/gold.txt'])
 return files

def fault_log_result(log):
 if 'FUSED_MEMORY_FAULT_INJECTED' not in log or not re.search(r'(?im)^\s*Fatal:\s*RTL_CYCLE_TIMEOUT\s*$',log) or 'FUSED_MULTICORE_RTL_PASS' in log:raise ValueError('FAULT_NOT_REJECTED')
 remaining=re.sub(r'(?im)^\s*Fatal:\s*RTL_CYCLE_TIMEOUT\s*$','',log)
 if re.search(r'(?im)^\s*(?:Error:|Fatal:)|assertion failed',remaining):raise ValueError('UNEXPECTED_FAULT_ASSERTION')
 return True

def validate_artifacts(directory):
 directory=pathlib.Path(directory).resolve()
 def raw(name,want):
  p=pathlib.PurePosixPath(name)
  if not isinstance(name,str) or '\\' in name or p.is_absolute() or '..' in p.parts or not p.parts:raise ValueError('RTL_ARTIFACT_PATH')
  target=directory.joinpath(*p.parts)
  if any(x.is_symlink() for x in [target]+list(target.parents) if x!=directory.parent) or not target.is_file():raise ValueError('RTL_ARTIFACT_MISSING_OR_LINK:'+name)
  data=target.read_bytes()
  if not data or not re.fullmatch('[0-9a-f]{64}',want or '') or hashlib.sha256(data).hexdigest()!=want:raise ValueError('RTL_ARTIFACT_HASH:'+name)
  return data
 r=json.loads((directory/'receipt.json').read_bytes());validate_receipt(r)
 if r.get('error') or r.get('result')!='FUSED_MULTICORE_RTL_PASS':raise ValueError('FAILED_RTL_RECEIPT')
 inputs=json.loads(raw(r['input_manifest_path'],r['input_manifest_sha256']))
 if not isinstance(inputs,dict) or inputs.get('kernel.elf')!=r['elf_sha256'] or hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()!=r['rtl_sha256'] or not any(n.endswith('.sv') for n in inputs):raise ValueError('RTL_INPUT_IDENTITY')
 for name,want in inputs.items():raw(name,want)
 compiles=r.get('compile',[])
 if len(compiles)!=3 or {c['name'] for c in compiles}!={'xvlog','xvhdl','xelab'}:raise ValueError('RTL_COMPILE_RECEIPTS')
 for c in compiles:
  text=raw(c['log'],c['log_sha256']).decode(errors='replace')
  if c.get('exit')!=0 or re.search(r'(?im)^\s*(?:Error:|Fatal:)|assertion failed',text):raise ValueError('RTL_COMPILE_FAILED')
 for c in r['cases']:
  if any(name not in inputs for name in case_input_paths(c['name'])):raise ValueError('RTL_CASE_INPUT_OR_COMMAND_MISSING')
  log=raw(c['log'],c['log_sha256']).decode(errors='replace');raw(c['wave'],c['wave_sha256'])
  if 'FUSED_TB_RESET_BOOTSTRAP_RELEASED' not in log:raise ValueError('RAW_RESET_BOOTSTRAP_MISSING')
  if c['name']=='memory_fault':fault_log_result(log)
  else:
   normal_log_result(log)
   if 'barrier_mask=15' not in log or (c['name']=='reuse_b' and 'FUSED_TWO_LAUNCH_REUSE_PASS' not in log):raise ValueError('RAW_COVERAGE_MISSING')
 return r
def fixtures(elf,out,descriptors=1,m=3,k=3,count=5,multiplier=1):
 b,_,loads,symbols=elf_info(elf);memory={};gold=[]
 def put(a,data):memory.update((a+i,x) for i,x in enumerate(data))
 for s in loads:put(s['start'],b[s['offset']:s['offset']+s['file_bytes']]+bytes(s['memory_bytes']-s['file_bytes']))
 n=descriptors*count;kp=(k+7)//8*8;np=(count+3)//4*4;mp=(m+7)//8*8
 x,w,bias,y,pool,ds,owner=0x85000040,0x80800000,0x80810000,0x850300c0,0x86c4df40,0x86d0df80,0x86d12e40
 inputs=[float(multiplier*(1+i%7)) for i in range(k*n)]
 put(x,struct.pack('<%df'%len(inputs),*inputs))
 weights=[float(1+r%3) if r<m and c<k else 0. for r in range(mp) for c in range(kp)]
 put(w,b''.join(struct.pack('<e',v) for v in weights));put(bias,struct.pack('<%df'%m,*([.25]*m)))
 packed=tiles=rows=0
 for d in range(descriptors):
  offset=d*count
  words=[m,n,k,mp,np,kp,offset,count,packed,rows,tiles,d*3+1,d*3+3,0,0,0]
  put(ds+64*d,struct.pack('<16I',*words))
  half=[inputs[r*n+offset+c] if r<k and c<count else 0. for r in range(kp) for c in range(np)]
  raw=b''.join(struct.pack('<e',v) for v in half)
  for i in range(0,len(raw),4):gold.append((pool+packed+i,struct.unpack_from('<I',raw,i)[0]))
  packed+=kp*np*2;rows+=kp;tiles+=(mp//8)*(np//4)
 stage=[0x53554648,1,128,15,1,3*descriptors,0,descriptors,x,w,bias,y,pool,packed,ds,owner,rows,tiles,
        k,m,1,n,1,n,1,1,0,0,tiles,1,1,0]
 put(0x10010040,struct.pack('<32I',*stage))
 for c in range(4):put(0x10002088+256*c,struct.pack('<I',0xffffffff))
 for r in range(m):
  for col in range(n):
   value=sum(inputs[q*n+col]*(1+r%3) for q in range(k))+.25
   gold.append((y+4*(r*n+col),struct.unpack('<I',struct.pack('<f',value))[0]))
 out.mkdir(parents=True,exist_ok=False)
 (out/'memory.txt').write_text(''.join('%08x %02x\n'%(a,v) for a,v in sorted(memory.items())))
 (out/'gold.txt').write_text(''.join('%08x %08x\n'%v for v in gold))
 return dict(pack_bytes=packed,tiles=tiles)
def run_rtl_check(elf,rtl_root,out):
 elf,rtl_root,out=map(pathlib.Path,(elf,rtl_root,out));out.mkdir(parents=True,exist_ok=False)
 hw=rtl_root.parents[1];config=json.loads((hw/'config/build.json').read_text())
 sources=[hw/p for p in config['sources']]+[hw/'rtl/VX_tex_unit.sv',hw/'rtl/gpgpu_tex_sampler.sv']
 ip=hw/'build/vivado/gpgpu_soc.srcs/sources_1/ip'
 models=[ip/n/'sim'/(n+'.vhd') for n in ('xil_fadd','xil_fmul','xil_fma','xil_fdiv','xil_fsqrt')]
 tb=ROOT/'tests/hunet_fused/rtl'
 manifest={str(p):sha(p) for p in sources+models+[hw/'config/build.json',elf,tb/'axi_memory.sv',tb/'tb_hunet_fused.sv',pathlib.Path(__file__)]}
 for directory in [hw/p for p in config['includes']]:
  for p in directory.rglob('*.vh'):manifest[str(p)]=sha(p)
 (out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
 raw_inputs={}
 for index,(name,want) in enumerate(sorted(manifest.items())):
  relative='kernel.elf' if pathlib.Path(name)==elf else 'inputs/%04d-'%index+pathlib.Path(name).name
  target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(name,target);raw_inputs[relative]=want
 (out/'raw-inputs.json').write_text(json.dumps(raw_inputs,sort_keys=True)+'\n')
 result=dict(scope='ACTUAL_HW4_RTL_EXECUTION',phase='preflight',elf_sha256=sha(elf),
  rtl_sha256=hashlib.sha256(json.dumps(raw_inputs,sort_keys=True).encode()).hexdigest(),input_manifest_path='raw-inputs.json',input_manifest_sha256=sha(out/'raw-inputs.json'),
  cores=4,warps=4,lanes=4,natural_exit=None,fatal=False,timeout=False,cases=[],compile=[])
 def bind_generated(names):
  for name in names:raw_inputs[name]=sha(out/name)
  (out/'raw-inputs.json').write_text(json.dumps(raw_inputs,sort_keys=True)+'\n')
  result['input_manifest_sha256']=sha(out/'raw-inputs.json')
  result['rtl_sha256']=hashlib.sha256(json.dumps(raw_inputs,sort_keys=True).encode()).hexdigest()
 env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64'
 bindir=pathlib.Path('G:/xilinx/Vivado/2020.1/bin')
 def run(name,args,timeout=600,tag=None,allow_failure=False):
  label=tag or name
  tokens=[str(bindir/(name+'.bat'))]+list(map(str,args))
  command=['cmd.exe','/d','/s','/c','"'+' '.join('"'+x+'"' for x in tokens)+'"']
  (out/(label+'-command.json')).write_text(json.dumps(command))
  bind_generated([label+'-command.json'])
  with (out/(label+'.console.log')).open('wb') as log:
   p=subprocess.Popen(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT)
   (out/(label+'-process.json')).write_text(json.dumps(dict(pid=p.pid,start=time.time(),command=command)))
   try:rc=p.wait(timeout=timeout)
   except subprocess.TimeoutExpired:
    result['timeout']=True
    # Preserve the live PID for inspection; do not claim a natural exit or restart.
    raise RuntimeError('RTL_PROCESS_STILL_RUNNING:'+str(p.pid))
  result['natural_exit']=rc
  if name!='xsim':result['compile'].append(dict(name=name,exit=rc,log=label+'.console.log',log_sha256=sha(out/(label+'.console.log'))))
  if rc and not allow_failure:raise RuntimeError(name+'_EXIT_'+str(rc))
  return (out/(label+'.console.log')).read_text(errors='replace')
 try:
  if os.name!='nt' or not (bindir/'xvlog.bat').is_file():raise RuntimeError('MATCHED_XSIM_NOT_AVAILABLE')
  inc=[hw/p for p in config['includes']]
  tb=ROOT/'tests/hunet_fused/rtl'
  defs=config['defines']+['SIMULATION']
  prj='sv xil_defaultlib '+ ' '.join('-i "'+p.as_posix()+'"' for p in inc)+' '+ ' '.join('-d "'+v+'"' for v in defs)+' \\\n'+ ' \\\n'.join('"'+p.as_posix()+'"' for p in sources+[tb/'axi_memory.sv',tb/'tb_hunet_fused.sv'])+'\n'
  (out/'gpu.prj').write_text(prj)
  (out/'ip.prj').write_text('vhdl xil_defaultlib '+' \\\n'.join('"'+p.as_posix()+'"' for p in models)+'\n')
  run('xvlog',['--relax','--prj','gpu.prj']);run('xvhdl',['--relax','--prj','ip.prj'])
  run('xelab',['--relax','--debug','typical','--mt','2','-L','floating_point_v7_1_10','-L','xil_defaultlib','-L','unisims_ver','-L','unimacro_ver','-L','secureip','xil_defaultlib.tb_hunet_fused','-s','fused'])
  result['phase']='simulation'
  # Each named case has its own actual simulator process and raw memory/gold.
  cases=[('one_descriptor',1,3,3,5,0,0),('idle_cores',1,1,1,1,0,0),
   ('tail',1,9,3,5,0,0),('32_descriptors',32,3,3,5,0,0),
   ('delayed_cores',1,3,3,5,1,0),('backpressure',32,9,3,5,1,0),
   ('reuse_b',1,3,3,5,1,0),('max_grid',1,96,1,4096,1,0),('memory_fault',1,3,3,5,1,1)]
  for name,nd,m,k,n,stall,fault in cases:
   directory=out/name;f=fixtures(elf,directory,nd,m,k,n)
   options=['--runall','--testplusarg','MEM='+str((directory/'memory.txt').as_posix()),'--testplusarg','GOLD='+str((directory/'gold.txt').as_posix()),'--testplusarg','PACK_BYTES='+str(f['pack_bytes']),'--testplusarg','STALL='+str(stall),'--testplusarg','FAULT='+str(fault)]
   if name=='reuse_b':
    second=directory/'second';fixtures(elf,second,nd,m,k,n,multiplier=2)
    options+=['--testplusarg','JOBS=2','--testplusarg','GOLD2='+(second/'gold.txt').as_posix(),'--testplusarg','OVERLAY='+(second/'memory.txt').as_posix()]
   if fault:options+=['--testplusarg','MAX_CYCLES=200000']
   option_file=directory/'options.txt';option_file.write_text('\n'.join(options)+'\n')
   bind_generated([p for p in case_input_paths(name) if not p.endswith('-command.json')])
   if (out/'fused.vcd').exists():raise RuntimeError('UNHARVESTED_WAVE')
   log=run('xsim',['fused','--runall','--onerror','quit','--onfinish','quit','--wdb',(directory/'fused.wdb').as_posix()]+[v for v in options if v!='--runall'],tag='xsim-'+name,allow_failure=bool(fault))
   wave=out/'fused.vcd'
   if not wave.is_file() or not wave.stat().st_size:raise RuntimeError('WAVE_NOT_CAPTURED')
   wave.rename(directory/'fused.vcd')
   (directory/'xsim.log').write_text(log)
   fatal=bool(re.search(r'(?im)^\s*Error:|fatal|assertion failed|timeout',log))
   semantic='FUSED_MULTICORE_RTL_PASS' in log
   case=dict(name=name,exit=result['natural_exit'],semantic_pass=semantic,barrier_mask=15 if 'barrier_mask=15' in log else 0,output_equal=semantic,launches=2 if 'FUSED_TWO_LAUNCH_REUSE_PASS' in log else 1,log='xsim-'+name+'.console.log',log_sha256=sha(out/('xsim-'+name+'.console.log')),wave=name+'/fused.vcd',wave_sha256=sha(directory/'fused.vcd'),assertion_errors=len(re.findall(r'(?im)^\s*Error:',log)),reset_bootstrap_released='FUSED_TB_RESET_BOOTSTRAP_RELEASED' in log)
   if fault:
    case.update(fault_observed='FUSED_MEMORY_FAULT_INJECTED' in log,expected_rejection='RTL_CYCLE_TIMEOUT' in log)
   result['cases'].append(case)
   if fault:
    fault_log_result(log)
   else:normal_log_result(log)
  result['natural_exit']=0 # suite completed; each process exit remains in its case
  if any(sha(p)!=v for p,v in manifest.items()):raise RuntimeError('COMPILED_INPUT_CHANGED')
  validate_receipt(result);result['result']='FUSED_MULTICORE_RTL_PASS'
  (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');validate_artifacts(out)
 except Exception as e:
  result.update(result='WAIT_RTL',error=str(e),deployable=False)
 finally:(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--elf',required=True);p.add_argument('--rtl-root',required=True);p.add_argument('--out',required=True);a=p.parse_args()
 r=run_rtl_check(a.elf,a.rtl_root,a.out);print(json.dumps(r));sys.exit(0 if r['result']=='FUSED_MULTICORE_RTL_PASS' else 1)

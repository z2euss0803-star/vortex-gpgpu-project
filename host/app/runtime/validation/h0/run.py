import argparse,datetime,json,os,re,shlex,subprocess,time
from pathlib import Path
from receipt import verify,sha
ROOT=Path(__file__).resolve().parents[2];XBIN=Path('G:/xilinx/Vivado/2020.1/bin')
def run(wd):
 state=wd/'pipeline-state.json';env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64'
 (wd/'tmp').mkdir();env['TMP']=env['TEMP']=str(wd/'tmp')
 stages={}
 def status(stage,**extra):state.write_text(json.dumps(dict(stage=stage,pid=os.getpid(),updated=datetime.datetime.now(datetime.timezone.utc).isoformat(),**extra),indent=2))
 def invoke(stage,args):
  log=wd/(stage+'.log')
  if log.exists():raise ValueError('Preserve earlier attempt')
  with log.open('w') as f:
   command=[args[0]]+args[1:] if args[0].endswith('.exe') else [str(XBIN/(args[0]+'.bat'))]+args[1:]
   p=subprocess.Popen(command,cwd=wd,env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT)
   while p.poll() is None:
    status(stage,child_pid=p.pid,log=str(log));time.sleep(3)
  stages[stage]=dict(exit=p.returncode,command=args,log=log.name,sha256=sha(log))
  (wd/(stage+'.process.json')).write_text(json.dumps(stages[stage],indent=2))
  if p.returncode!=0 or re.search(r'(?im)^(?:ERROR:|Error:|FATAL|Fatal:)',log.read_text(errors='replace')):raise RuntimeError(stage+' failed')
 try:
  while True:
   p=subprocess.run(['powershell.exe','-NoProfile','-Command','@(Get-Process xsimk,xelab,vrfc -ErrorAction SilentlyContinue | Select-Object Id,Name) | ConvertTo-Json -Compress'],capture_output=True,text=True,timeout=30)
   if p.returncode:raise RuntimeError('Cannot inspect other simulations')
   if not p.stdout.strip():break
   status('queued',other_processes=p.stdout.strip());time.sleep(10)
  invoke('compile',['xvlog','--relax','-L','xilinx_vip','-prj','cnn_tb.prj','-log','compile_native.log'])
  gcc='G:/xilinx/Vivado/2020.1/tps/mingw/6.2.0/win64.o/nt/bin/gcc.exe'
  common=[gcc,'-std=c11','-O2','-I.','-IG:/xilinx/Vivado/2020.1/data/xsim/include','-c']
  invoke('dpi-host',common+['cnn_host.c','-o','cnn_host.o'])
  invoke('dpi-backend',common+['dpi_backend.c','-o','dpi_backend.o'])
  (wd/'xsim.dir/work/xsc').mkdir(parents=True,exist_ok=True)
  invoke('dpi',['G:/xilinx/Vivado/2020.1/tps/mingw/6.2.0/win64.o/nt/bin/ar.exe','rcs','xsim.dir/work/xsc/dpi.a','cnn_host.o','dpi_backend.o'])
  original=ROOT.parent/'vortex_v23_dual_ddr_resetfix_20261001/build/vivado/gpgpu_soc.sim/sim_1/behav/xsim/elaborate.bat'
  line=next(s for s in original.read_text().splitlines() if s.startswith('call xelab '))
  args=shlex.split(line[5:]);libs=[]
  for i,v in enumerate(args):
   if v=='-L':libs+=['-L',args[i+1]]
  invoke('elaborate',['xelab','--O0','--relax','--mt','off','--debug','typical']+libs+['--snapshot','cnn_h0','--sv_lib','dpi','xil_defaultlib.tb_cnn_h0','xil_defaultlib.glbl','-log','elaborate_native.log'])
  invoke('simulate',['xsim','cnn_h0','-runall','-onfinish','quit','-log','simulate_native.log'])
  inputs=json.loads((wd/'inputs.json').read_text())
  result=dict(scope='CNN_H0_FULL_SOC_RTL',natural_exit=True,source_commit=inputs['source_commit'],programs=inputs['programs'],stages=stages,
   waveform=dict(path='cnn_h0.vcd',sha256=sha(wd/'cnn_h0.vcd')),
   outputs={p.name:sha(p) for p in sorted((wd/'outputs').glob('*.bin'))},board='NOT_RUN',h1='NOT_RUN')
  errors=verify(wd,result);result['errors']=errors;result['pass']=not errors
  (wd/'result.json').write_text(json.dumps(result,indent=2))
  if errors:raise RuntimeError('; '.join(errors))
  status('PASS',scope='actual PL with VIP host, not Linux/A53 ISA execution');print('CNN_H0_FULL_SOC_RTL_PASS',flush=True)
 except Exception as e:status('FAIL',error=str(e));raise
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--workdir',required=True);o=a.parse_args();run(Path(o.workdir).resolve())

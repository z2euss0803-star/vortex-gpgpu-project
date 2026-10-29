"""Run Vivado-generated XSim compilation natively (no Windows Script Host)."""
from pathlib import Path
import os,subprocess,shlex,shutil,sys,json
r=Path(__file__).resolve().parents[1]
wd=r/'build/vivado/gpgpu_soc.sim/sim_1/behav/xsim'
out=r/'evidence/gpu_rtl';out.mkdir(parents=True,exist_ok=True)
xbin=Path(r'G:\xilinx\Vivado\2020.1\bin')
tempdir=r/'build/tmp';tempdir.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64';env['TEMP']=env['TMP']=str(tempdir)
shutil.copyfile(r/'kernel/vecadd16.hex',wd/'vecadd16.hex')
vector_root=Path(os.environ.get('V23_SIM_VECTOR_ROOT',r/'kernel'))
assert vector_root.is_dir(), f'Missing vector directory: {vector_root}'
for f in vector_root.glob('image_*.hex'):shutil.copyfile(f,wd/f.name)
for f in vector_root.glob('gold_*.hex'):shutil.copyfile(f,wd/f.name)
# Vivado 2020.1 can prune TCU IP wrappers when its hierarchy cache predates
# the explicit Xilinx-model define. Compile the real generated models here.
vhdl_prj=wd/'tb_gpgpu_vhdl.prj'
project=vhdl_prj.read_text()
# Standalone vendor demo benches are unrelated to tb_gpgpu and can precede
# their own model in Vivado's manually managed source order.
project='\n'.join(line for line in project.splitlines() if '/demo_tb/' not in line)+'\n'
for ip in ['xil_fadd','xil_fmul']:
    model=r/f'build/vivado/gpgpu_soc.srcs/sources_1/ip/{ip}/sim/{ip}.vhd'
    assert model.is_file(), f'Missing generated vendor model: {model}'
    if f'/{ip}.vhd' not in project:
        project=project.replace('nosort',f'vhdl xil_defaultlib "{model.as_posix()}"\nnosort')
vhdl_prj.write_text(project)
commands=[]
def run(name,args):
    cmd=[str(xbin/(args[0]+'.bat'))]+args[1:];commands.append(cmd)
    (out/'commands.json').write_text(json.dumps(commands,indent=2))
    timeout_s=int(os.environ.get('V23_XSIM_TIMEOUT_S','7200'))
    try:
        result=subprocess.run(cmd,cwd=wd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=timeout_s)
    except subprocess.TimeoutExpired as exc:
        partial=exc.stdout or b''
        if isinstance(partial,bytes):partial=partial.decode('utf-8','replace')
        (out/(name+'.timeout.log')).write_text(partial,encoding='utf-8')
        print(name,f'timed out after {timeout_s} s; partial output saved',flush=True)
        raise
    (out/(name+'.log')).write_text(result.stdout,encoding='utf-8')
    print(name, 'exit=',result.returncode,flush=True)
    if result.returncode or any(s in result.stdout for s in ['ERROR:','Error:','FATAL','Fatal:']):
        print(result.stdout[-9000:]);sys.exit(1)
    return result.stdout
def verify_run():
    log=run('run',['xsim','tb_gpgpu_behav','-runall','-log','gpu_run.log'])
    print(log[-5000:])
    if 'GPGPU_REAL_RTL_PASS:' not in log:raise SystemExit('Missing actual GPU pass marker')
    if 'NATIVE_TCU_TEX_RTL_PASS:' not in log:raise SystemExit('Missing native TCU/TEX pass marker')
    print('GPU_SIMULATION_VERIFIED')
if os.environ.get('V23_XSIM_RUN_ONLY')=='1':
    assert (wd/'xsim.dir/tb_gpgpu_behav/xsimk.exe').is_file(), 'Missing compiled XSim snapshot'
    verify_run()
    sys.exit(0)
for bat in ['compile.bat','elaborate.bat']:
    for line in (wd/bat).read_text().splitlines():
        if line.startswith(('call xvlog ','call xvhdl ','call xelab ')):
            args=shlex.split(line[5:])
            if args[0]=='xelab':
                # Macros are already frozen in the compilation .prj. Passing
                # unquoted '=' definitions through old Windows batch loaders
                # splits them into spurious design unit names.
                clean=[];skip=False
                for arg in args:
                    if skip:skip=False;continue
                    if arg=='-d':skip=True;continue
                    clean.append(arg)
                args=clean
                if os.environ.get('V23_XELAB_O0')=='1':
                    # Retry the large four-core XSim elaboration without the
                    # generated script's incremental/debug/2-thread C build.
                    # The previous generated C compilation timed out; retain
                    # the original mode when this opt-in is absent.
                    lean=[];skip=False
                    for arg in args:
                        if skip:skip=False;continue
                        if arg in ('--debug','--mt'):
                            skip=True;continue
                        if arg=='--incr':continue
                        lean.append(arg)
                    args=lean[:1]+['--O0','--mt','off']+lean[1:]
                if os.environ.get('V23_XELAB_VERBOSE')=='1':
                    args=args[:1]+['--verbose','2']+args[1:]
            run(args[0],args)
verify_run()

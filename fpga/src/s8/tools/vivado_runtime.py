"""Private child runtime; never changes installed files or system environment."""
from pathlib import Path
import os,subprocess,json,datetime

def runtime_env(toolroot,evidence_dir=None):
    toolroot=Path(toolroot).resolve()
    if toolroot.as_posix().lower() not in ('g:/xilinx/vivado/2020.1','d:/xilinx/vivado/2020.1'):
        raise ValueError('unverified tool installation')
    env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64'
    setup=toolroot/'bin/setupEnv.bat'
    command=[env['COMSPEC'],'/d','/u','/c','call '+str(setup)+' && set']
    result=subprocess.run(command,env=env,capture_output=True)
    if evidence_dir is not None:
        evidence_dir=Path(evidence_dir)
        (evidence_dir/'bootstrap_stderr.bin').write_bytes(result.stderr)
        (evidence_dir/'bootstrap_exit.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'command':command,'exit_code':result.returncode,'stdout_policy':'private environment not persisted'},indent=2)+'\n')
    result.check_returncode();text=result.stdout.decode('utf-16-le')
    for line in text.splitlines():
        if '=' in line and not line.startswith('='):
            key,value=line.split('=',1);env[key]=value
    values={'RDI_PLATFORM':'win64','RDI_BUILD':'yes','TCL_LIBRARY':'tps/tcl/tcl8.5',
        'ISL_IOSTREAMS_RSA':'tps/isl','XILINX_VIVADO':'','XILINX_PLANAHEAD':'',
        'RDI_DATADIR':'data','RDI_BINDIR':'bin','RDI_LIBDIR':'lib/win64.o',
        'RDI_JAVAROOT':'tps/win64/jre9.0.4','SYNTH_COMMON':'scripts/rt/data',
        'RT_LIBPATH':'scripts/rt/data','RT_TCL_PATH':'scripts/rt/base_tcl/tcl'}
    for k,v in values.items():env[k]=v if k in ('RDI_PLATFORM','RDI_BUILD') else (toolroot/v).as_posix()
    paths=['bin','lib/win64.o','tps/win64/jre9.0.4/bin/server','tps/win64/jre9.0.4/bin',
        'tps/mingw/6.2.0/win64.o/nt/bin','tps/mingw/6.2.0/win64.o/nt/libexec/gcc/x86_64-w64-mingw32/6.2.0',
        'tps/win64/python-2.7.16','tps/win64/python-2.7.16/lib']
    env['PATH']=';'.join(str(toolroot/p) for p in paths)+';'+env.get('PATH','')
    vendor=toolroot/'tps/win64/python-2.7.16'
    for k in ('PYTHON','PYTHONHOME','RDI_PYTHON2','RDI_PYTHONHOME'):env[k]=str(vendor)
    env['RDI_PYTHON_LD_LIBPATH']=str(vendor/'lib')
    env['PYTHONPATH']=';'.join(str(vendor/p) for p in ('','bin','lib','Lib/lib-tk','Lib/site-packages'))
    env['RDI_PYTHONPATH']=env['PYTHONPATH'];env['RDI_OPT_EXT']='.o'
    return env

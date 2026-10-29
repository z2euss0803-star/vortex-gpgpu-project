"""One-shot HW4 compilation and affected integration baseline; preserve all stages."""
from pathlib import Path
import datetime, hashlib, json, os, re, shlex, subprocess, traceback
from prepare import ROOT, HW, VENDOR, sha, stage_ok, verify_files

EV=ROOT/'evidence'; WD=ROOT/'build/xsim'
def now(): return datetime.datetime.now().astimezone().isoformat()
def state(stage,status,**extra):
    (EV/'state.json').write_text(json.dumps(dict(updated=now(),pid=os.getpid(),stage=stage,status=status,**extra),indent=2))
def main():
    # Persistent exclusive claim: an exited or interrupted run is never restarted.
    with (EV/'baseline_start_claim.json').open('x') as f:
        json.dump(dict(pid=os.getpid(),started=now(),launcher_sha256=sha(__file__)),f,indent=2)
    manifest=json.loads((EV/'input_manifest.json').read_text())
    verify_files(manifest['files'])
    for name in ['run_baseline.py','audit_baseline.py']:
        assert (ROOT/'scripts'/name).is_file()
    (EV/'baseline_run_inputs.json').write_text(json.dumps(dict(
        input_manifest_sha256=sha(EV/'input_manifest.json'),
        launcher_sha256=sha(__file__),audit_sha256=sha(ROOT/'scripts/audit_baseline.py'),
        scope=manifest['scope']),indent=2))
    env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64'
    env['TEMP']=env['TMP']=str(ROOT/'build/tmp')
    def run(stage,args):
        log=EV/(stage+'.log')
        with log.open('x',encoding='utf-8') as stream:
            cmd=([args[0]] if Path(args[0]).is_absolute() else [str(VENDOR/'bin'/(args[0]+'.bat'))])+args[1:]
            p=subprocess.Popen(cmd,cwd=WD,env=env,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT)
            state(stage,'running',child_pid=p.pid,command=cmd,log=str(log))
            code=p.wait()
        text=log.read_text(errors='replace')
        record=dict(stage=stage,exit_code=code,finished=now(),command=cmd,log_sha256=sha(log),
                    error_fatal=len(re.findall(r'(?im)^\s*(?:ERROR\s*:|FATAL\b)',text)),
                    critical_warning=len(re.findall(r'(?im)^\s*CRITICAL WARNING',text)))
        with (EV/'stage_exits.jsonl').open('a') as f: f.write(json.dumps(record)+'\n')
        if not stage_ok(code,text): raise RuntimeError(f'{stage} failed; preserve {log}')
        verify_files(manifest['files'])
        return text
    try:
        inherited=json.loads((EV/'inherited_compile_library.json').read_text())
        verify_files(inherited['files'])
        run('compile_checker',['xvlog','--relax','-L','xilinx_vip','-prj','checker.prj','-log','checker_xvlog.log'])
        run('compile_tb',['xvlog','--relax','-L','axi_vip_v1_1_7','-L','smartconnect_v1_0','-L','zynq_ultra_ps_e_vip_v1_0_7','-L','xilinx_vip','-prj','baseline_tb.prj','-log','baseline_tb_xvlog.log'])
        original=HW/'build/vivado/gpgpu_soc.sim/sim_1/behav/xsim/elaborate.bat'
        line=next(s for s in original.read_text().splitlines() if s.startswith('call xelab '))
        args=shlex.split(line[5:]);libs=[]
        for i,v in enumerate(args):
            if v=='-L':libs+=['-L',args[i+1]]
        run('elaborate',['xelab','--O0','--relax','--mt','off','--debug','typical']+libs+
            ['--snapshot','hw4_shared_baseline_v4','-sv_lib','dpi','xil_defaultlib.tb_ps_host_c','xil_defaultlib.glbl','-log','elaborate.log'])
        snapshot=[dict(path=str(p),sha256=sha(p)) for p in sorted((WD/'xsim.dir').rglob('*')) if p.is_file() and p.suffix in ('.sdb','.vdb','.obj','.a','.exe')]
        with (EV/'expanded_snapshot.json').open('x') as f:json.dump(dict(files=snapshot),f,indent=2)
        log=run('simulate',['xsim','hw4_shared_baseline_v4','-runall','-onfinish','quit','-log','baseline_native.log'])
        required={'PS_PL_LOOPBACK_HOST_PASS':2,'SHARED_HOST_FIRST_RUN_PASS':1,
                  'SHARED_HOST_RESET_REPEAT_PASS':1,'SHARED_A53_LOGIC_FULL_SOC_PASS':1,
                  'LOOPBACK_STARTUP_RESET_PASS':1,'HW4_CHECKER_ARMED_PASS':1,'VECADD64_PASS':6,'CDMA_PARTIAL_PASS':2}
        for marker,count in required.items():
            assert log.count(marker)==count,(marker,log.count(marker),count)
        assert not re.search(r'(?im)^FAIL ',log)
        with (EV/'baseline_result.json').open('x') as f:
            json.dump(dict(status='tb_completed_independent_audit_pending',finished=now(),markers=required,
                           independent_audit_pending=True,whole_regression_pass=False),f,indent=2)
        state('baseline','tb_completed_independent_audit_pending')
    except Exception as exc:
        state('baseline','failed',error=str(exc),preserve_scene=True)
        with (EV/'baseline_failure.txt').open('x') as f:f.write(traceback.format_exc())
        raise

if __name__=='__main__': main()

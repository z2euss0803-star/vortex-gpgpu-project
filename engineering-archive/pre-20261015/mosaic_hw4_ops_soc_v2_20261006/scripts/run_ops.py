"""Exclusive single run. Completed baseline required; no hardware rebuild or board access."""
from pathlib import Path
import datetime,json,os,re,subprocess,traceback
from prepare_ops import ROOT,sha
from audit_ops import verify,require_armed_checker
EV=ROOT/'evidence';WD=ROOT/'build/xsim';VENDOR=Path('G:/xilinx/Vivado/2020.1')
BASE=ROOT.parent/'mosaic_hw4_soc_v4_20261006'
def now():return datetime.datetime.now().astimezone().isoformat()
def state(stage,status,**extra):
    (EV/'state.json').write_text(json.dumps(dict(updated=now(),pid=os.getpid(),stage=stage,status=status,**extra),indent=2))
def main():
    baseline=BASE/'evidence/baseline_audit.json';b=json.loads(baseline.read_text())
    assert b['pass_baseline'] is True
    m=json.loads((EV/'input_manifest.json').read_text())
    assert b['source_manifest_sha256']==m['source_manifest_sha256'] and b['bitstream_sha256']==m['bitstream_sha256']
    assert sha(baseline)==m['baseline_audit_sha256']
    verify(m['files']);verify(m['vendor_files']);verify(m['inherited_library'])
    for r in range(2):
        for j in range(10):
            assert not (WD/f'actual_round{r}_job{j}.hex').exists()
            assert not (WD/f'guards_round{r}_job{j}.hex').exists()
    with (EV/'start_claim.json').open('x') as f:json.dump(dict(pid=os.getpid(),started=now(),launcher_sha256=sha(__file__)),f,indent=2)
    with (EV/'run_inputs.json').open('x') as f:json.dump(dict(input_manifest_sha256=sha(EV/'input_manifest.json'),baseline_audit_sha256=sha(baseline),
        launcher_sha256=sha(__file__),auditor_sha256=sha(ROOT/'scripts/audit_ops.py')),f,indent=2)
    env=os.environ.copy();env['PROCESSOR_ARCHITECTURE']='AMD64';env['TEMP']=env['TMP']=str(ROOT/'build/tmp')
    def run(name,args):
        log=EV/(name+'.log');cmd=[str(VENDOR/'bin'/(args[0]+'.bat'))]+args[1:]
        with log.open('x',encoding='utf-8') as f:
            p=subprocess.Popen(cmd,cwd=WD,env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT)
            state(name,'running',child_pid=p.pid,command=cmd,log=str(log));code=p.wait()
        t=log.read_text(errors='replace')
        e=dict(stage=name,exit_code=code,finished=now(),command=cmd,log_sha256=sha(log),
            error_fatal_critical=len(re.findall(r'(?im)^\s*(?:ERROR\s*:|FATAL\b|CRITICAL WARNING)',t)))
        with (EV/'stage_exits.jsonl').open('a') as f:f.write(json.dumps(e)+'\n')
        assert code==0 and e['error_fatal_critical']==0,e
        verify(m['files']);verify(m['vendor_files'])
        return t
    try:
        run('compile_tb',['xvlog','--relax','-L','axi_vip_v1_1_7','-L','smartconnect_v1_0','-L','zynq_ultra_ps_e_vip_v1_0_7','-L','xilinx_vip','-prj','ops_tb.prj','-log','ops_xvlog.log'])
        run('compile_host',['xsc','--mt','off',(ROOT/'sw/mosaic_device.c').as_posix(),(ROOT/'sw/ops_host.c').as_posix()])
        libs=[]
        for lib in json.loads((EV/'library_names.json').read_text()):libs+=['-L',lib]
        run('elaborate',['xelab','--O0','--relax','--mt','off','--debug','typical']+libs+
            ['--snapshot','hw4_mosaic_soc_v2','-sv_lib','dpi','xil_defaultlib.tb_hw4_mosaic_soc','xil_defaultlib.glbl','-log','ops_elaborate.log'])
        frozen=[]
        for p in sorted((WD/'xsim.dir').rglob('*')):
            if p.is_file():frozen.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
        with (EV/'expanded_snapshot.json').open('x') as f:json.dump(dict(files=frozen),f,indent=2)
        with (EV/'snapshot_binding.json').open('x') as f:json.dump(dict(expanded_snapshot_sha256=sha(EV/'expanded_snapshot.json')),f)
        sim=run('simulate',['xsim','hw4_mosaic_soc_v2','-runall','-onfinish','quit','-log','ops_native.log'])
        require_armed_checker(sim)
        assert sim.count('MOSAIC_HW4_ACTUAL_SOC_TB_PASS')==1 and sim.count('MOSAIC_SOC_JOB_PASS')==20
        assert not re.search('MOSAIC_SOC_FAIL|HOST_DPI_TASK_DISABLED',sim)
        with (EV/'result.json').open('x') as f:json.dump(dict(status='tb_completed_independent_audit_pending',finished=now(),
            independent_audit_pending=True,board_pass=False),f,indent=2)
        state('operators','tb_completed_independent_audit_pending')
    except Exception as e:
        state('operators','failed',error=str(e),preserve_scene=True)
        with (EV/'failure.txt').open('x') as f:f.write(traceback.format_exc())
        raise
if __name__=='__main__':main()

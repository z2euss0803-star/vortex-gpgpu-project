"""Prepare a new HW4 run with the previously qualified simulation checker."""
from pathlib import Path
import datetime, hashlib, json, re, shutil
ROOT=Path(__file__).resolve().parents[1]
V1=ROOT.parent/'mosaic_hw4_soc_v1_20261006'
V2=ROOT.parent/'mosaic_hw4_soc_v2_20261006'
OLD=ROOT.parent/'vortex_v23_dual_ddr_loopback_20261001'
HW=ROOT.parent/'mosaic_hw_v4_20261005'
VENDOR=Path('G:/xilinx/Vivado/2020.1')
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def verify(rows):
    for e in rows:assert sha(e['path'])==e['sha256'],e['path']
def tokens(text):
    text=re.sub(r'(?ms)^\s*// BEGIN LOOPBACK_SIM_ONLY.*?^\s*// END LOOPBACK_SIM_ONLY[^\n]*\n','',text)
    text=re.sub(r'(?m)^\s*`ASSERT\([^\n]*\n','',text)
    return ' '.join(text.split())
def main():
    ev=ROOT/'evidence';ev.mkdir(exist_ok=False)
    wd=ROOT/'build/xsim';wd.mkdir(parents=True,exist_ok=False)
    (ROOT/'build/tmp').mkdir()
    m=json.loads((V1/'evidence/input_manifest.json').read_text());verify(m['files'])
    proof=json.loads((OLD/'evidence/pending_assertion_provenance/proof.json').read_text())
    test=json.loads((OLD/'evidence/pending_assertion_candidate_v3/result.json').read_text())
    assert test['pass'];verify(test['inputs'])
    original=V1/'source/hw4/build/compat_rtl/libs/VX_pending_size.sv'
    assert sha(original)==proof['original_sha256']
    candidate=Path(proof['candidate_path']);assert sha(candidate)==proof['candidate_sha256']
    platform=V1/'source/hw4/build/compat_rtl/VX_platform.vh'
    assert sha(platform)==test['inputs'][2]['sha256']
    assert tokens(original.read_text(encoding='utf-8'))==tokens(candidate.read_text(encoding='utf-8'))
    assert hashlib.sha256(tokens(original.read_text(encoding='utf-8')).encode()).hexdigest()==proof['functional_token_sha256']
    target=ROOT/'models/gpu_assertion/VX_pending_size.sv';target.parent.mkdir(parents=True)
    shutil.copyfile(candidate,target)
    shutil.copytree(V1/'tb',ROOT/'tb')
    adapter=ROOT/'tb/shared_host_adapter.svh'
    text=adapter.read_text()
    needle='int startup_clock_count=0;'
    assert text.count(needle)==1
    text=text.replace(needle,"wire ic_checker_armed=`IC_PENDING.g_size_gt1.g_wide_step.verification_reset_seen;\nwire dc_checker_armed=`DC_PENDING.g_size_gt1.g_wide_step.verification_reset_seen;\n"+needle)
    needle='  $display("LOOPBACK_STARTUP_RESET_PASS'
    assert text.count(needle)==1
    text=text.replace(needle,'  if(ic_checker_armed!==1 || dc_checker_armed!==1)\n    $fatal(1,"HW4_CHECKER_NOT_ARMED");\n  $display("HW4_CHECKER_ARMED_PASS ic=1 dc=1");\n'+needle)
    adapter.write_text(text)
    shutil.copytree(V1/'build/xsim/xsim.dir/xil_defaultlib',wd/'xsim.dir/xil_defaultlib')
    shutil.copytree(V2/'build/xsim/xsim.dir/work/xsc',wd/'xsim.dir/work/xsc')
    shutil.copyfile(V1/'build/xsim/xsim.ini',wd/'xsim.ini')
    for p in (V1/'build/xsim').glob('*.mem'):shutil.copyfile(p,wd/p.name)
    inherited=[]
    for p in sorted((wd/'xsim.dir').rglob('*')):
        if p.is_file():
            relative=p.relative_to(wd)
            src=(V2 if 'work' in relative.parts else V1)/'build/xsim'/relative
            assert sha(p)==sha(src)
            inherited.append(dict(path=str(p),sha256=sha(p),source=str(src)))
    (ev/'inherited_compile_library.json').write_text(json.dumps(dict(files=inherited,hardware_library_source=str(V1),old_r4_library_reused=False),indent=2))
    records=[]
    for base,stages in [(V1,('compile_verilog','compile_vhdl')),(V2,('compile_host',))]:
        exits=[json.loads(s) for s in (base/'evidence/stage_exits.jsonl').read_text().splitlines()]
        for name in stages:
            e=next(x for x in exits if x['stage']==name)
            assert e['exit_code']==0 and e['error_fatal']==0 and e['critical_warning']==0
            src=base/'evidence'/(name+'.log');assert sha(src)==e['log_sha256']
            shutil.copyfile(src,ev/src.name);records.append(dict(e,inherited_from=str(base),not_rerun=True))
    with (ev/'stage_exits.jsonl').open('x') as f:
        for e in records:f.write(json.dumps(e)+'\n')
    prj=V1/'build/xsim/hw4_vlog.prj'
    text=prj.read_text();lines=text.splitlines();index=next(i for i,line in enumerate(lines) if '"'+original.as_posix()+'"' in line)
    # The module belongs to a multiline prj group. Preserve the group's flags.
    start=index
    while start and not re.match(r'^(?:sv|verilog) xil_defaultlib',lines[start]):start-=1
    header=lines[start].rstrip(' \\')
    assert 'SIMULATION=' in header
    (wd/'checker.prj').write_text(header+' "'+target.as_posix()+'"\nnosort\n')
    model=V1/'models/ddr4'
    flags=f'-i "{model.as_posix()}" -i "{(ROOT/"tb").as_posix()}" -d "DDR4_8G_X16" -d "DDR4_833_Timing" -d "SILENT" -d "FIXED_2400"'
    (wd/'baseline_tb.prj').write_text(f'sv xil_defaultlib {flags} "{(ROOT/"tb/tb_ps_host_c.sv").as_posix()}"\nnosort\n')
    checker=dict(recorded=datetime.datetime.now().astimezone().isoformat(),
        scope='same functional HW4 RTL; simulation verification adapted after first effective reset',
        original_sha256=sha(original),candidate_sha256=sha(target),platform_sha256=sha(platform),
        functional_tokens_equal=True,functional_token_sha256=proof['functional_token_sha256'],
        qualification_result_sha256=sha(OLD/'evidence/pending_assertion_candidate_v3/result.json'),
        qualification_scenarios=test['scenarios'],qualification_not_rerun=True,
        hardware_changed=False,strict_zero_errors_unchanged=True)
    (ev/'checker_binding.json').write_text(json.dumps(checker,indent=2))
    files=list(m['files'])
    for d in [ROOT/'scripts',ROOT/'models',ROOT/'tb',ROOT/'tests']:
        for p in sorted(d.rglob('*')):
            if p.is_file():files.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
    for p in [wd/'checker.prj',wd/'baseline_tb.prj',wd/'xsim.ini',ev/'checker_binding.json',OLD/'evidence/pending_assertion_candidate_v3/result.json',OLD/'evidence/pending_assertion_provenance/proof.json']:
        files.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
    m['files']=files;m['simulation_checker_override']=checker;m['same_candidate_compile_inheritance']=True
    with (ev/'input_manifest.json').open('x') as f:json.dump(m,f,indent=2)
    print('HW4_SOC_V3_PREPARED',len(files),'input identities;',len(inherited),'private compiled identities')
if __name__=='__main__':main()

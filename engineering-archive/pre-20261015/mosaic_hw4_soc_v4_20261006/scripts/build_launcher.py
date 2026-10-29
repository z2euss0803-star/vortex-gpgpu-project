"""Create new, versioned launcher/auditor; do not mutate the failed run."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
V2=ROOT.parent/'mosaic_hw4_soc_v2_20261006'
def main():
    for name in ['prepare.py','audit_shared_wave.py']:
        with (ROOT/'scripts'/name).open('x') as f:f.write((V2/'scripts'/name).read_text())
    text=(V2/'scripts/run_baseline.py').read_text()
    start=text.index("        ar=VENDOR/")
    end=text.index("        original=HW/",start)
    text=text[:start]+'''        run('compile_checker',['xvlog','--relax','-L','xilinx_vip','-prj','checker.prj','-log','checker_xvlog.log'])
        run('compile_tb',['xvlog','--relax','-L','axi_vip_v1_1_7','-L','smartconnect_v1_0','-L','zynq_ultra_ps_e_vip_v1_0_7','-L','xilinx_vip','-prj','baseline_tb.prj','-log','baseline_tb_xvlog.log'])
'''+text[end:]
    text=text.replace('hw4_shared_baseline_v2','hw4_shared_baseline_v3')
    text=text.replace("'LOOPBACK_STARTUP_RESET_PASS':1", "'LOOPBACK_STARTUP_RESET_PASS':1,'HW4_CHECKER_ARMED_PASS':1")
    needle="        log=run('simulate'"
    assert text.count(needle)==1
    text=text.replace(needle,"        snapshot=[dict(path=str(p),sha256=sha(p)) for p in sorted((WD/'xsim.dir').rglob('*')) if p.is_file() and p.suffix in ('.sdb','.vdb','.obj','.a','.exe')]\n        with (EV/'expanded_snapshot.json').open('x') as f:json.dump(dict(files=snapshot),f,indent=2)\n"+needle)
    with (ROOT/'scripts/run_baseline.py').open('x') as f:f.write(text)
    text=(V2/'scripts/audit_baseline.py').read_text()
    text=text.replace("['compile_verilog','compile_vhdl','compile_host','elaborate','simulate']", "['compile_verilog','compile_vhdl','compile_host','compile_checker','compile_tb','elaborate','simulate']")
    needle="    module=ROOT/"
    assert text.count(needle)==1
    text=text.replace(needle,"    process=json.loads((ev/'process_exit.json').read_text(encoding='utf-8-sig'))\n    assert process['query_success'] and process['target_count']==0\n    import datetime\n    assert datetime.datetime.fromisoformat(process['sampled'])>datetime.datetime.fromisoformat(result['finished'])\n    verify_files(json.loads((ev/'expanded_snapshot.json').read_text())['files'])\n    checker=json.loads((ev/'checker_binding.json').read_text())\n    assert checker['functional_tokens_equal'] and not checker['hardware_changed'] and checker['strict_zero_errors_unchanged']\n    assert (ev/'simulate.log').read_text().count('HW4_CHECKER_ARMED_PASS ic=1 dc=1')==1\n"+needle)
    text=text.replace("waveform=wave,identity_count", "checker_binding_sha256=sha(ev/'checker_binding.json'),process_exit_sha256=sha(ev/'process_exit.json'),waveform=wave,identity_count")
    with (ROOT/'scripts/audit_baseline.py').open('x') as f:f.write(text)
    print('HW4_SOC_V3_LAUNCHER_CREATED_NOT_STARTED')
if __name__=='__main__':main()

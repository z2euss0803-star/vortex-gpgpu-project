"""Independent new-candidate boundary audit; baseline only, not network release."""
from pathlib import Path
import importlib.util,json
from prepare import ROOT,sha,verify_files,stage_ok

def main():
    ev=ROOT/'evidence'
    assert not (ev/'baseline_audit.json').exists(), 'Existing audit preserved; do not rerun'
    result=json.loads((ev/'baseline_result.json').read_text())
    assert result['status']=='tb_completed_independent_audit_pending'
    manifest=json.loads((ev/'input_manifest.json').read_text());verify_files(manifest['files'])
    exits=[json.loads(s) for s in (ev/'stage_exits.jsonl').read_text().splitlines()]
    assert [s['stage'] for s in exits]==['compile_verilog','compile_vhdl','compile_host','elaborate','simulate']
    for e in exits:
        log=ev/(e['stage']+'.log')
        assert sha(log)==e['log_sha256'] and stage_ok(e['exit_code'],log.read_text(errors='replace'))
        assert e['critical_warning']==0,e
    module=ROOT/'scripts/audit_shared_wave.py'
    spec=importlib.util.spec_from_file_location('wave',module);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    wave=m.audit(ROOT/'build/xsim/shared_host.vcd')
    assert wave['pass']
    with (ev/'baseline_audit.json').open('x') as f:
        json.dump(dict(pass_baseline=True,whole_regression_pass=False,
                       scope='New HW4 shared C baseline: actual PL buses and DDR, two runs across PL reset; no A53 ISA/FSBL/MMU/board',
                       waveform=wave,identity_count=len(manifest['files']),
                       source_manifest_sha256=manifest['source_manifest_sha256'],
                       bitstream_sha256=manifest['bitstream_sha256']),f,indent=2)
    print('HW4_BASELINE_AUDIT_PASS; Mosaic compiled operator integration still pending')
if __name__=='__main__':main()

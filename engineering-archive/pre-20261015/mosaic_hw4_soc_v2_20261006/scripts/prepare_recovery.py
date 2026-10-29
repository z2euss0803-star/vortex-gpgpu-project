"""Preserve failed v1; inherit unchanged HW4 compile stages into independent v2."""
from pathlib import Path
import datetime, hashlib, json, shutil

ROOT=Path(__file__).resolve().parents[1]
OLD=ROOT.parent/'mosaic_hw4_soc_v1_20261006'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def main():
    ev=ROOT/'evidence';ev.mkdir(exist_ok=False)
    assert not (ROOT/'build').exists()
    inputs=json.loads((OLD/'evidence/input_manifest.json').read_text())
    for e in inputs['files']:assert sha(e['path'])==e['sha256'],e['path']
    stages=[json.loads(s) for s in (OLD/'evidence/stage_exits.jsonl').read_text().splitlines()]
    assert [e['stage'] for e in stages]==['compile_verilog','compile_vhdl','compile_host']
    assert all(e['exit_code']==0 and e['error_fatal']==0 for e in stages[:2])
    assert stages[2]['exit_code']==1 and stages[2]['error_fatal']==1
    # Every source/result in the failed scene is bound, including generated libraries.
    scene=[dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p))
           for p in sorted(OLD.rglob('*')) if p.is_file()]
    with (ev/'v1_preserved_scene.json').open('x') as f:
        json.dump(dict(recorded=datetime.datetime.now().astimezone().isoformat(),files=scene,
            process_exit_sample='2026-10-06T13:50:21.7331099+08:00 successful Win32_Process query: no worker 27996 or compilation/simulation process',
            root_cause='xsc forwards -I to ar archive invocation; C object compilation completed; no elaboration/simulation',
            architecture_changed=False),f,indent=2)
    shutil.copytree(OLD/'build',ROOT/'build')
    shutil.copytree(OLD/'sw',ROOT/'sw')
    for name in ['prepare.py','audit_baseline.py','audit_shared_wave.py']:
        shutil.copyfile(OLD/'scripts'/name,ROOT/'scripts'/name)
    shutil.copyfile(OLD/'evidence/input_manifest.json',ev/'input_manifest.json')
    for e in stages[:2]:
        p=OLD/'evidence'/(e['stage']+'.log')
        assert sha(p)==e['log_sha256']
        shutil.copyfile(p,ev/p.name)
        e['inherited_from']=str(OLD);e['not_rerun']=True
    with (ev/'stage_exits.jsonl').open('x') as f:
        for e in stages[:2]:f.write(json.dumps(e)+'\n')
    lib=[]
    for p in sorted((ROOT/'build/xsim/xsim.dir').rglob('*')):
        if p.is_file():
            orig=OLD/'build/xsim'/p.relative_to(ROOT/'build/xsim')
            assert sha(p)==sha(orig)
            lib.append(dict(path=str(p),sha256=sha(p),source=str(orig)))
    with (ev/'inherited_compile_library.json').open('x') as f:
        json.dump(dict(source_hw4_manifest_sha256=inputs['source_manifest_sha256'],files=lib,
            old_hardware_library_reused=False,same_candidate_newly_compiled_v1_library=True,
            completed_stages_repeated=False),f,indent=2)
    print('RECOVERY_PREPARED',len(scene),'preserved scene files;',len(lib),'same-candidate library/object identities')
if __name__=='__main__':main()

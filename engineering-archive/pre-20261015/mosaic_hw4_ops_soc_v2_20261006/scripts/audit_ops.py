"""One independent end audit of actual SoC/raw values; no board claim."""
from pathlib import Path
import datetime,hashlib,json,math,re,struct
from prepare_ops import ROOT,sha
BASE=ROOT.parent/'mosaic_hw4_soc_v4_20261006'
HW=ROOT.parent/'mosaic_hw_v4_20261005'
def verify(entries):
    for e in entries:assert sha(e['path'])==e['sha256'],e['path']

def require_armed_checker(text):
    assert len(re.findall(r'(?m)^HW4_CHECKER_ARMED_PASS ic=1 dc=1\s*$',text))==1,'Both qualified checkers must arm exactly once'

def require_process_exit(exit_query,claim,start_query,snapshot,root,launcher):
    pid=claim['pid'];assert isinstance(pid,int) and pid>0
    assert exit_query['query_success'] is True and start_query['query_success'] is True
    for q in [start_query,exit_query]:assert q['worker_pid']==pid and q['snapshot']==snapshot
    for q in [start_query,exit_query]:assert q['tracking_scope']=='worker_snapshot_descendants'
    rows=[p for p in start_query['processes'] if p['ProcessId']==pid]
    assert len(rows)==1 and rows[0]['Name'].lower()=='python.exe'
    command=rows[0]['CommandLine'].replace('\\','/').lower()
    assert str(Path(root)/'scripts'/launcher).replace('\\','/').lower() in command
    created=datetime.datetime.fromisoformat(rows[0]['CreationDate'])
    started=datetime.datetime.fromisoformat(claim['started'])
    assert -5<=(started-created).total_seconds()<=60
    assert datetime.datetime.fromisoformat(start_query['sampled'])>=created
    assert datetime.datetime.fromisoformat(exit_query['sampled'])>=datetime.datetime.fromisoformat(start_query['sampled'])
    assert exit_query['target_count']==0
    assert exit_query['global_heavy_count']==0
    receipt=[p for p in exit_query['receipts'] if p['ProcessId']==pid and datetime.datetime.fromisoformat(p['CreationDate'])==created]
    assert len(receipt)==1
    assert not any(p['ProcessId']==pid or snapshot.lower() in (p.get('CommandLine') or '').lower() for p in exit_query['processes'])
    return dict(worker_pid=pid,creation_time=rows[0]['CreationDate'],snapshot=snapshot,command=rows[0]['CommandLine'])
def numeric(actual,expected):
    assert list(actual)==list(expected),'raw address order/count differs'
    diffs=[]
    for a,v in actual.items():
        x=struct.unpack('<f',struct.pack('<I',v))[0];y=struct.unpack('<f',struct.pack('<I',expected[a]))[0]
        assert math.isfinite(x) and math.isfinite(y),(a,v)
        d=abs(x-y);assert d<=0.02+0.0002*abs(y),(a,x,y,d)
        diffs.append(d)
    return dict(values=len(diffs),max_abs=max(diffs),rmse=math.sqrt(sum(d*d for d in diffs)/len(diffs)),exceedances=0)
def read_raw(p):
    rows=[tuple(int(v,16) for v in s.split()) for s in p.read_text().splitlines()]
    assert all(len(r)==2 for r in rows) and len({r[0] for r in rows})==len(rows),p
    return dict(rows)
def wave_audit(p,fixture):
    names=['phase','soc_round','soc_job','four_core_seen','resetn_observed','ready_observed','ddr_resetn_observed',
           'dma_jobs','gpu_rounds','hpm0_reads','hpm0_writes','hpm1_reads','hpm1_writes','hp_reads','hp_writes',
           'ddr_reads','ddr_writes','gpu_reads','gpu_writes','host_reads','host_writes','host_flushes','host_invalidates',
           'host_barriers','gpu_flush_issued','ps_flush_accepted']
    scope=[];ids={};values={};phases=set();reset=[];ready=[];header=True;digest=hashlib.sha256();last_time=0
    with p.open('rb') as f:
        for raw in f:
            digest.update(raw);line=raw.decode('ascii',errors='replace').strip();parts=line.split()
            if header:
                if line.startswith('$scope'):scope.append(parts[2])
                elif line.startswith('$upscope'):scope.pop()
                elif line.startswith('$var') and scope==['tb_hw4_mosaic_soc'] and parts[4] in names:ids[parts[3]]=parts[4]
                elif line.startswith('$enddefinitions'):header=False
                continue
            if not line:continue
            if line.startswith('#'):last_time=int(line[1:]);continue
            if line[0]=='$':continue
            if line[0] in 'bB' and len(parts)==2:bits,code=parts[0][1:],parts[1]
            elif line[0] in '01xXzZ':bits,code=line[0],line[1:]
            else:continue
            if code not in ids:continue
            key=ids[code];v=int(bits,2) if all(b in '01' for b in bits) else None;values[key]=v
            if v is None:continue
            if key=='phase':phases.add(v)
            if key=='resetn_observed' and (not reset or reset[-1]!=v):reset.append(v)
            if key=='ready_observed' and (not ready or ready[-1]!=v):ready.append(v)
    assert set(names)<=values.keys() and all(v is not None for v in values.values())
    assert {1,2,4,5,6,7}<=phases and values['phase']==6,phases
    assert reset[-4:]==[0,1,0,1] and ready[-4:]==[0,1,0,1],(reset,ready)
    assert values['dma_jobs']==fixture['expected_dma_jobs'] and values['gpu_rounds']==20
    assert values['soc_round']==1 and values['soc_job']==9 and values['four_core_seen']==15
    assert values['gpu_flush_issued']==values['ps_flush_accepted']>0
    assert all(values[n]>0 for n in names[9:]),values
    return dict(sha256=digest.hexdigest(),last_time_ps=last_time,reset=reset,ready=ready,phases=sorted(phases),counters=values)
def main():
    ev=ROOT/'evidence';wd=ROOT/'build/xsim';target=ev/'soc_audit.json'
    assert not target.exists(),'Existing audit preserved; never rerun'
    manifest=json.loads((ev/'input_manifest.json').read_text())
    run_inputs=json.loads((ev/'run_inputs.json').read_text())
    assert sha(ev/'input_manifest.json')==run_inputs['input_manifest_sha256']
    verify(manifest['files']);verify(manifest['vendor_files'])
    binding=json.loads((ev/'snapshot_binding.json').read_text())
    assert sha(ev/'expanded_snapshot.json')==binding['expanded_snapshot_sha256']
    expanded=json.loads((ev/'expanded_snapshot.json').read_text())
    verify([e for e in expanded['files'] if Path(e['path']).suffix.lower() in ['.dll','.exe','.a','.sdb','.vdb','.rlx','.mem','.obj']])
    assert sha(HW/'evidence/source_manifest.json')==manifest['source_manifest_sha256']
    assert sha(HW/'build/artifacts/top.bit')==manifest['bitstream_sha256']
    # Bind the unchanged completed candidate library at its immutable source.
    verify([dict(path=e['source'],sha256=e['sha256']) for e in manifest['inherited_library']])
    base=json.loads((BASE/'evidence/baseline_audit.json').read_text())
    assert base['pass_baseline'] is True and base['source_manifest_sha256']==manifest['source_manifest_sha256']
    assert sha(BASE/'evidence/baseline_audit.json')==run_inputs['baseline_audit_sha256']
    assert manifest['baseline_audit_sha256']==run_inputs['baseline_audit_sha256']
    closure=json.loads((BASE/'evidence/completion_binding.json').read_text())
    assert closure['pass_'] is True and closure['live_hw_sources_and_vendor_identities_verified'] is True
    assert closure['audit_sha256']==sha(BASE/'evidence/baseline_audit.json')
    assert sha(BASE/'evidence/completion_binding.json')==manifest['baseline_completion_binding_sha256']
    assert sha(BASE/'evidence/checker_binding.json')==base['checker_binding_sha256']
    assert manifest['simulation_checker_override']['functional_tokens_equal']
    assert not manifest['simulation_checker_override']['hardware_changed']
    result=json.loads((ev/'result.json').read_text());assert result['status']=='tb_completed_independent_audit_pending'
    exit_query=json.loads((ev/'process_exit.json').read_text(encoding='utf-8-sig'))
    process_identity=require_process_exit(exit_query,json.loads((ev/'start_claim.json').read_text()),
        json.loads((ev/'process_start.json').read_text(encoding='utf-8-sig')),'hw4_mosaic_soc_v2',ROOT,'run_ops.py')
    assert datetime.datetime.fromisoformat(exit_query['sampled'])>=datetime.datetime.fromisoformat(result['finished'])
    exits=[json.loads(s) for s in (ev/'stage_exits.jsonl').read_text().splitlines()]
    assert [e['stage'] for e in exits]==['compile_tb','compile_host','elaborate','simulate']
    for e in exits:
        log=ev/(e['stage']+'.log');t=log.read_text(errors='replace')
        assert sha(log)==e['log_sha256'] and e['exit_code']==0
        assert not re.search(r'(?im)^\s*(?:ERROR\s*:|FATAL\b|CRITICAL WARNING)',t),e
        assert not re.search(r'MOSAIC_SOC_FAIL|HOST_DPI_TASK_DISABLED',t),e
    sim=(ev/'simulate.log').read_text(errors='replace')
    require_armed_checker(sim)
    pairs=[(r,j) for r in range(2) for j in range(10)]
    assert [(int(r),int(j)) for r,j in re.findall(r'MOSAIC_SOC_JOB_START round=(\d+) job=(\d+)',sim)]==pairs
    assert [(int(r),int(j)) for r,j in re.findall(r'MOSAIC_SOC_JOB_PASS round=(\d+) job=(\d+)',sim)]==pairs
    done=re.findall(r'MOSAIC_SOC_DONE round=(\d+) job=(\d+) status=([0-9a-f]+) errors=([0-9a-f]+) pw=(\d+) pr=(\d+) elapsed=(\d+) jobs=(\d+)',sim)
    assert [(int(d[0]),int(d[1])) for d in done]==pairs
    for r,j,status,errors,pw,pr,elapsed,jobs in done:
        assert int(status,16)&2 and int(errors,16)==int(pw)==int(pr)==0 and int(jobs)==int(j)+1
    assert sim.count('MOSAIC_HW4_ACTUAL_SOC_TB_PASS')==1
    assert sim.count('MOSAIC_SOC_RESET_REPEAT_PASS')==1 and sim.count('MOSAIC_SOC_ROUND_PASS')==2
    assert [(int(r),int(j)) for r,j in re.findall(r'MOSAIC_SOC_PACK_ZERO round=(\d+) job=(\d+)',sim)]==[(r,j) for r in range(2) for j in [0,2,4,6]]
    fixture=json.loads((ev/'fixture.json').read_text());expected=[{} for _ in range(10)]
    for s in (ROOT/'assets/expected.hex').read_text().splitlines():
        j,a,v=(int(x,16) for x in s.split());assert a*4 not in expected[j];expected[j][a*4]=v
    guard_addresses=[int(s,16)*4 for s in (ROOT/'assets/guards.hex').read_text().splitlines()]
    metrics=[];raw_files=[]
    for r,j in pairs:
        output=wd/f'actual_round{r}_job{j}.hex';guards=wd/f'guards_round{r}_job{j}.hex'
        vals=read_raw(output);g=read_raw(guards)
        assert len(vals)==fixture['counts'][j]
        assert list(g)==guard_addresses and len(g)==544 and set(g.values())=={0x5aa59669}
        metrics.append(dict(round=r,job=j,**numeric(vals,expected[j])))
        for p in [output,guards]:raw_files.append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
    wave=wave_audit(wd/'hw4_mosaic_soc.vcd',fixture)
    for n in ['mosaic_device.c','mosaic_device.h','mosaic_job.h','mosaic_addresses.h']:
        assert sha(ROOT/'sw'/n)==sha(ROOT.parent/'mosaic_board_v1_20261004/sw'/n),n
    report=dict(pass_=True,scope='actual_ps_cdma_gpu_ddr_regression',
        source_manifest_sha256=manifest['source_manifest_sha256'],bitstream_sha256=manifest['bitstream_sha256'],
        xsa_sha256=manifest['xsa_sha256'],adapter_sha256=manifest['adapter_sha256'],source_count=305,
        audited=datetime.datetime.now().astimezone().isoformat(),baseline_audit_sha256=run_inputs['baseline_audit_sha256'],
        input_manifest_sha256=sha(ev/'input_manifest.json'),process_exit_query_sha256=sha(ev/'process_exit.json'),
        process_identity=process_identity,process_start_query_sha256=sha(ev/'process_start.json'),
        raw_files=raw_files,metrics=metrics,checks=49840,guard_observations=10880,waveform=wave,
        board_pass=False,a53_isa_executed=False,network_full_image_board_pass=False,
        limitations=['PS VIP/native C cache ownership contract model; no A53 ISA/FSBL/MMU or board',
                    'HW4 implementation warnings and external DDR reset output-delay gap retained',
                    'These operator fixtures do not execute the full 1280x960 sixteen-layer network'])
    report['pass']=report.pop('pass_')
    with target.open('x') as f:json.dump(report,f,indent=2)
    destination=HW/'evidence/hw4_soc_regression_audit_v2_20261006.json'
    with destination.open('x') as f:json.dump(dict(report,original_audit=str(target),original_audit_sha256=sha(target)),f,indent=2)
    print('HW4_ACTUAL_SOC_INDEPENDENT_AUDIT_PASS; board network verification remains pending')
if __name__=='__main__':main()

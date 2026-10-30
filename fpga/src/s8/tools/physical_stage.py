"""Serial, evidence-preserving native Vivado stages. No simulation or deployment."""
from pathlib import Path
import argparse,datetime,getpass,hashlib,json,os,platform,re,sys
if __package__ in (None,''):sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.resource_gate import resource_gate
from tools.process_runner import run_preserving,native_exit_verdict
from tools.vivado_runtime import runtime_env
ROOT=Path(__file__).resolve().parents[1]
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,data):p.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def source_identity():
    frozen=json.loads((ROOT/'evidence/source_rebase_manifest.json').read_text(encoding='utf-8'))
    checked={}
    for variant,key in [('baseline','baseline_files'),('candidate','candidate_files')]:
        for rel,expected in frozen[key].items():
            p=ROOT/variant/rel
            if not p.is_file() or sha(p)!=expected:raise ValueError('Frozen source drift: '+str(p))
            checked[variant+'/'+rel]=expected
    for folder in ['flow','tools','config']:
        for p in (ROOT/folder).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:checked[p.relative_to(ROOT).as_posix()]=sha(p)
    return {'files':checked,'sha256':hashlib.sha256(json.dumps(checked,sort_keys=True).encode()).hexdigest()}
def generated_identity():
    base=ROOT/'candidate/build/vivado';result={}
    for p in base.rglob('*'):
        # Bind generated source, constraints, IP configuration and their synth DCPs.
        if p.is_file() and p.suffix.lower() in {'.v','.sv','.vh','.svh','.vhd','.vhdl','.xci','.bd','.xdc','.coe','.mem','.mif','.dcp','.prj'}:
            result[p.relative_to(ROOT).as_posix()]=sha(p)
    return result
def require_physical_checkpoint_inputs(checkpoint_inputs,current_inputs,receipt):
    if receipt.get('status')!='PASS' or receipt.get('scope')!='generated_physical_inputs_for_NEW_synthesis_only':raise ValueError('Complete pre-synthesis physical input preparation required')
    if checkpoint_inputs!=current_inputs or current_inputs!=receipt.get('generated_inputs'):raise ValueError('Checkpoint, current inputs and complete physical preparation are not identical')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('stage',choices=['version','prepare','synthesis','implementation_inputs','implementation']);ap.add_argument('--synthesis-receipt');ap.add_argument('--prepared-inputs-receipt')
    a=ap.parse_args();stage=a.stage
    if platform.node().upper()!='RY' or getpass.getuser()!='13151':raise ValueError('A host/account mismatch')
    state_path=ROOT/'evidence/system_state.json'
    if state_path.exists():
        previous=json.loads(state_path.read_text(encoding='utf-8'))
        if previous.get('mode')=='active':raise ValueError('Existing bound stage requires natural-exit review first')
    synth=None
    for required in ({'prepare':['version'],'synthesis':['version','prepare'],'implementation_inputs':['version','prepare'],'implementation':['version','prepare']}.get(stage,[])):
        p=ROOT/'evidence'/(required+'_admission.json')
        if not p.is_file():raise ValueError('Independent '+required+' admission required')
        accepted=json.loads(p.read_text(encoding='utf-8'))
        if accepted.get('status')!='PASS':raise ValueError(required+' not independently admitted')
        if required=='version' and accepted.get('tool_executable_sha256')!=sha(Path('G:/xilinx/Vivado/2020.1/bin/unwrapped/win64.o/vivado.exe')):raise ValueError('Tool identity changed')
    if stage in ['implementation_inputs','implementation']:
        if not a.synthesis_receipt:raise ValueError('Independent synthesis receipt required')
        p=Path(a.synthesis_receipt).resolve();synth=json.loads(p.read_text(encoding='utf-8'))
        if synth.get('status')!='PASS' or synth.get('scope')!='current_whole_gpu_synthesis':raise ValueError('Synthesis not independently admitted')
        dcp=Path(synth['checkpoint']);assert dcp.is_relative_to(ROOT/'runs') and sha(dcp)==synth['checkpoint_sha256']
    run=ROOT/'runs'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'_'+stage)
    run.mkdir(parents=True,exist_ok=False);(run/'artifacts').mkdir();(run/'reports').mkdir()
    before=source_identity();save(run/'source_before.json',before)
    gate=resource_gate(ROOT,stage);save(run/'resource_gate.json',gate)
    state={'utc':utc(),'mode':'resource_hold','host':platform.node(),'account':getpass.getuser(),'stage':stage,'run':str(run),'query_known':gate['query_known'],'resource_gate':str(run/'resource_gate.json'),'simulation':'WAIVED_NOT_RUN','board':'NOT_RUN'}
    save(state_path,state)
    if gate['status']!='READY':print(json.dumps(gate));return 2
    if stage in ['synthesis','implementation_inputs','implementation']:
        generated=generated_identity()
        if not generated:raise ValueError('Missing generated project inputs')
        save(run/'generated_inputs.json',generated)
        if stage=='synthesis':
            prep_path=Path(a.prepared_inputs_receipt).resolve() if a.prepared_inputs_receipt else ROOT/'evidence/prepare_admission.json'
            if not prep_path.is_relative_to(ROOT/'evidence'):raise ValueError('Preparation receipt outside own evidence')
            prepared=json.loads(prep_path.read_text(encoding='utf-8'))
            if prepared['status']!='PASS' or prepared['generated_inputs']!=generated:raise ValueError('Prepared project identity differs')
            state.update(prepared_inputs_receipt=str(prep_path),prepared_inputs_receipt_sha256=sha(prep_path))
        if stage=='implementation':
            prior_run=Path(synth['run']);prior_launch=json.loads((prior_run/'launch.json').read_text(encoding='utf-8'))
            prep_path=Path(prior_launch['prepared_inputs_receipt']).resolve()
            if not prep_path.is_relative_to(ROOT/'evidence') or sha(prep_path)!=prior_launch['prepared_inputs_receipt_sha256']:raise ValueError('Preparation provenance changed')
            admitted=json.loads(prep_path.read_text(encoding='utf-8'))
            require_physical_checkpoint_inputs(json.loads((prior_run/'generated_inputs.json').read_text(encoding='utf-8')),generated,admitted)
            state.update(prepared_inputs_receipt=str(prep_path),prepared_inputs_receipt_sha256=sha(prep_path),synthesis_receipt=str(Path(a.synthesis_receipt).resolve()),synthesis_checkpoint_sha256=synth['checkpoint_sha256'])
    exe=Path('G:/xilinx/Vivado/2020.1/bin/unwrapped/win64.o/vivado.exe')
    env=runtime_env(exe.parents[3],run);env['RDI_PROG']=str(exe);env['RDI_PROGNAME']='vivado.exe'
    env['S8_PHYSICAL_ROOT']=ROOT.as_posix();env['S8_RUN_DIR']=run.as_posix()
    if synth:env['S8_SYNTH_DCP']=Path(synth['checkpoint']).as_posix()
    cmd=[str(exe),'-version'] if stage=='version' else [str(exe),'-mode','batch','-source',str(ROOT/'flow'/(stage+'.tcl')),'-log',str(run/'vivado.log'),'-journal',str(run/'vivado.jou'),'-notrace']
    state.update(utc=utc(),mode='active',command=cmd,cwd=str(run),events=str(run/'events.jsonl'),source_sha256=before['sha256'],native_executable_sha256=sha(exe),threads=1)
    save(state_path,state);save(run/'launch.json',state)
    receipt=run_preserving(cmd,cwd=run,log_path=run/'console.log',events_path=run/'events.jsonl',env=env,deadline_seconds=86400)
    save(run/'natural_exit.json',receipt)
    after=source_identity();save(run/'source_after.json',after)
    log=(run/'console.log').read_text(encoding='utf-8',errors='replace')
    native=native_exit_verdict(receipt,['vivado.exe'])
    marker={'version':'Vivado v2020.1','prepare':'S8_W4_PREPARE_COMPLETE','synthesis':'S8_W4_SYNTHESIS_COMPLETE','implementation_inputs':'S8_W4_IMPLEMENTATION_INPUTS_COMPLETE','implementation':'S8_W4_ROUTE_COMPLETE'}[stage]
    errors=re.findall(r'(?m)^\s*(?:ERROR:|FATAL:|FATAL_ERROR:).+$',log)
    status='TOOL_COMPLETE' if native['status']=='PASS' and not errors and marker in log and after==before else 'FAIL' if native['status']=='FAIL' or errors or after!=before else 'UNKNOWN'
    products={p.relative_to(run).as_posix():sha(p) for p in run.rglob('*') if p.is_file()}
    save(run/'product_manifest.json',products)
    result={'utc':utc(),'stage':stage,'run':str(run),'status':status,'native':native,'semantic_errors':errors,'marker_observed':marker in log,'source_stable':after==before,'source_sha256':before['sha256'],'product_manifest_sha256':sha(run/'product_manifest.json'),'simulation':'WAIVED_NOT_RUN','board':'NOT_RUN'}
    save(run/'stage_receipt.json',result)
    state.update(utc=utc(),mode='awaiting_independent_acceptance' if status=='TOOL_COMPLETE' else 'failed_or_unknown',result=result,active_run=None)
    save(state_path,state)
    if status!='TOOL_COMPLETE':
        with (ROOT/'docs/error_ledger.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(result,ensure_ascii=False)+'\n')
    print(json.dumps(result,ensure_ascii=False));return 0 if status=='TOOL_COMPLETE' else 1
if __name__=='__main__':raise SystemExit(main())

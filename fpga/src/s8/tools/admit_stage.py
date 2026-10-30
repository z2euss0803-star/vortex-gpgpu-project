"""Independent readback of completed native stages; never executes EDA."""
from pathlib import Path
import argparse,json,re,sys
if __package__ in (None,''):sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.physical_stage import ROOT,sha,save,utc,source_identity,generated_identity
from tools.process_runner import native_exit_verdict
def native_array_dsp_counts(text):
    # Use explicit synthesized module identities, not guessed hierarchy syntax.
    # Vivado generate blocks can appear as g_native.native_core in cell names.
    arrays=re.findall(r'(?m)^S8_ARRAY_INSTANCE=(.+)$',text)
    dsps=re.findall(r'(?m)^DSP=(.+)$',text)
    return {name:sum(cell.startswith(name+'/') for cell in dsps) for name in arrays}
def implementation_input_changes(before,after):
    changed=sorted(k for k in set(before)|set(after) if before.get(k)!=after.get(k))
    hdl={'.v','.sv','.vh','.svh','.vhd','.vhdl','.coe','.mem','.mif','.dcp'}
    return {'changed_files':changed,'changed_synthesis_hdl':[k for k in changed if Path(k).suffix.lower() in hdl]}
def admit(run):
    run=Path(run).resolve()
    if not run.is_relative_to(ROOT/'runs'):raise ValueError('Run outside this independent root')
    result=json.loads((run/'stage_receipt.json').read_text(encoding='utf-8'));stage=result['stage']
    native=json.loads((run/'natural_exit.json').read_text(encoding='utf-8'))
    manifest=json.loads((run/'product_manifest.json').read_text(encoding='utf-8'))
    reasons=[]
    for rel,expected in manifest.items():
        p=run/rel
        if not p.is_relative_to(run) or not p.is_file() or sha(p)!=expected:reasons.append('Product identity drift: '+rel)
    if result['product_manifest_sha256']!=sha(run/'product_manifest.json'):reasons.append('Product manifest identity drift')
    if result['status']!='TOOL_COMPLETE':reasons.append('Stage did not complete semantically')
    if native_exit_verdict(native,['vivado.exe'])['status']!='PASS':reasons.append('Native identity/natural exit incomplete')
    current=source_identity()
    if current['sha256']!=result['source_sha256']:reasons.append('Current source/control differs from run')
    log=(run/'console.log').read_text(encoding='utf-8',errors='replace')
    if native['log_sha256']!=sha(run/'console.log'):reasons.append('Raw console identity drift')
    if re.search(r'(?m)^\s*(ERROR:|FATAL:|FATAL_ERROR:)',log):reasons.append('Raw semantic error')
    out={'utc':utc(),'run':str(run),'stage':stage,'scope':stage,'status':'PASS','reasons':reasons,'source_sha256':current['sha256'],'stage_receipt_sha256':sha(run/'stage_receipt.json'),'natural_exit_sha256':sha(run/'natural_exit.json'),'simulation':'WAIVED_NOT_RUN','board':'NOT_RUN','critical_warnings':re.findall(r'(?m)^CRITICAL WARNING:.+$',log)}
    if stage=='version':
        if 'Vivado v2020.1' not in log:reasons.append('Version not 2020.1')
        out['tool_executable_sha256']=sha(Path(native['command'][0]))
    if stage=='prepare':
        out['generated_inputs']=generated_identity()
        if not out['generated_inputs']:reasons.append('No generated synthesis inputs')
        ip=(run/'reports/ip_status.rpt').read_text(encoding='utf-8',errors='replace')
        if re.search(r'(?i)\b(locked|upgrade required)\b',ip):reasons.append('IP status requires explicit review')
    if stage=='synthesis':
        out['scope']='current_whole_gpu_synthesis'
        text=(run/'reports/synthesis_structure.txt').read_text(encoding='utf-8')
        for field in ['BLACKBOX_COUNT','LATCH_COUNT']:
            if not re.search(r'(?m)^'+field+r'=0\s*$',text):reasons.append(field+' not zero/unknown')
        # Direct named 8x8 DSP48E2 arrays must exist in every one of four cores.
        counts=native_array_dsp_counts(text)
        out['native_s8_dsp_by_core']=counts
        if len(counts)!=4 or any(n!=64 for n in counts.values()):reasons.append('Four independent 64-DSP S8 arrays not proven')
        drc=(run/'reports/synthesis_drc.rpt').read_text(encoding='utf-8',errors='replace')
        if re.search(r'\bMDRV-1\b',drc):reasons.append('Multiple drivers require diagnosis')
        util=(run/'reports/utilization_synth.rpt').read_text(encoding='utf-8',errors='replace')
        percentages=re.findall(r'^\|\s*(?:CLB LUTs\*?|CLB Registers|Block RAM Tile|DSPs)\s*\|[^\n]*?\|\s*([\d.]+)\s*\|\s*$',util,re.M)
        if len(percentages)<4:reasons.append('Resource legality not fully parsed; independent review required')
        elif any(float(n)>100 for n in percentages):reasons.append('Device capacity exceeded')
        checkpoint=run/'artifacts/top_synth.dcp'
        if not checkpoint.is_file():reasons.append('Missing synthesized checkpoint')
        else:out.update(checkpoint=str(checkpoint),checkpoint_sha256=sha(checkpoint))
    if stage=='implementation_inputs':
        before=json.loads((run/'generated_inputs.json').read_text(encoding='utf-8'))
        out['generated_inputs']=generated_identity()
        out['input_delta']=implementation_input_changes(before,out['generated_inputs'])
        if out['input_delta']['changed_synthesis_hdl']:reasons.append('Generated synthesis inputs changed; checkpoint reuse refused')
        text=(run/'reports/implementation_inputs.txt').read_text(encoding='utf-8')
        if 'IMPLEMENTATION_INPUTS_READY=1' not in text or 'BD_IMPLEMENTATION_TARGET_GENERATED=1' not in text:reasons.append('Implementation target audit incomplete')
        synth=json.loads((ROOT/'evidence/synthesis_admission.json').read_text(encoding='utf-8'))
        synthesized=json.loads((Path(synth['run'])/'generated_inputs.json').read_text(encoding='utf-8'))
        out['since_synthesis_delta']=implementation_input_changes(synthesized,out['generated_inputs'])
        if out['since_synthesis_delta']['changed_synthesis_hdl']:reasons.append('Actual generated HDL differs from accepted synthesis inputs')
        out['synthesis_receipt_sha256']=sha(ROOT/'evidence/synthesis_admission.json')
    if stage=='implementation':
        out['scope']='current_whole_gpu_routed_physical_review'
        # Routing completion is not signoff. Explicit review is required for
        # clock/exception/unconstrained/pulse/CDC and baseline open warnings.
        reasons.append('Independent physical signoff review required: route, setup/hold/pulse, DRC, clocks, CDC and exception coverage')
    if reasons:out['status']='REFUSED'
    dest=ROOT/'evidence'/(stage+'_admission.json')
    if dest.exists():
        old=json.loads(dest.read_text(encoding='utf-8'))
        if old.get('stage_receipt_sha256')!=out['stage_receipt_sha256']:
            dest=ROOT/'evidence'/(stage+'_admission_'+run.name+'.json')
    save(dest,out);return out
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);a=p.parse_args()
    result=admit(a.run);print(json.dumps(result,ensure_ascii=False));raise SystemExit(0 if result['status']=='PASS' else 2)

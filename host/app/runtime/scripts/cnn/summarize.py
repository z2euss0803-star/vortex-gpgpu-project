"""Independent raw-record checks and nearest-rank P95. No device timing inference."""
import argparse
import json
import math
import statistics
from pathlib import Path
METRICS=('cnn_audit_end_to_end_ns','kernel_chain_host_ns','cpu_reference_ns','tensor_h2d_ns','tensor_d2h_ns')
LAYER_METRICS=('layer_submit_wait_ns','layer_code_args_h2d_ns','layer_verify_ns')
def integer(v): return type(v) is int and 0<=v<=0xffffffffffffffff
def stats(values):
    if not values:return dict(n=0,min=None,median=None,p95=None,max=None)
    values=sorted(values);return dict(n=len(values),min=values[0],median=statistics.median(values),p95=values[math.ceil(.95*len(values))-1],max=values[-1])
def summarize(records):
    errors=[];success=[];failed=False;scopes=set();phases={'FIRST':0,'WARMUP':0,'MEASURED':0};failures=0
    expected=[('FIRST',0)]+[('WARMUP',i) for i in range(3)]+[('MEASURED',i) for i in range(20)]
    for index,r in enumerate(records):
        scopes.add(r.get('scope'));phase=r.get('phase')
        if phase in phases: phases[phase]+=1
        if index>=len(expected) or (phase,r.get('iteration'))!=expected[index]:errors.append('phase_sequence')
        if failed: errors.append('continued_after_failure')
        if r.get('result')!=0:
            failed=True;failures+=1;continue
        before=len(errors)
        if r.get('scope') not in ('REAL_DRIVER_CNN_I32_V1','TEST_PROTOCOL_NOT_GPU'):errors.append('scope')
        if any(r.get(k) is not None for k in ('device_kernel_ns','cycles','frequency')):errors.append('unsupported_device_timing')
        if not integer(r.get('start_ns')) or not integer(r.get('end_ns')) or r['end_ns']<r['start_ns']:errors.append('clock')
        elif r['end_ns']-r['start_ns']!=r.get('cnn_audit_end_to_end_ns'):errors.append('elapsed')
        if any(not integer(r.get(k)) for k in METRICS):errors.append('missing_metric')
        if not isinstance(r.get('bytes'),dict) or any(not integer(r['bytes'].get(k)) for k in ('h2d','d2h')):errors.append('bytes')
        layers=r.get('layers',[])
        if len(layers)!=4:errors.append('layer_count')
        for j,l in enumerate(layers):
            if l.get('program_id')!=j+2 or not integer(l.get('job_id')) or not l.get('job_id'):errors.append('layer_identity')
            if l.get('result')!=0 or l.get('guard_pass') is not True or l.get('mismatch_count')!=0:errors.append('layer_validation')
            if any(not integer(l.get(k)) for k in LAYER_METRICS):errors.append('layer_metric')
            if not integer(l.get('start_ns')) or not integer(l.get('end_ns')) or l['end_ns']<l['start_ns']:errors.append('layer_clock')
            elif l['end_ns']-l['start_ns']!=l.get('layer_submit_wait_ns'):errors.append('layer_elapsed')
            if not isinstance(l.get('bytes'),dict) or any(not integer(l['bytes'].get(k)) for k in ('h2d','d2h')):errors.append('layer_bytes')
        if len(layers)==4 and all(integer(l.get('layer_submit_wait_ns')) for l in layers):
            if sum(l['layer_submit_wait_ns'] for l in layers)!=r.get('kernel_chain_host_ns'):errors.append('kernel_chain_sum')
        if len(errors)==before and phase=='MEASURED':success.append(r)
    complete=len(records)==24 and phases=={'FIRST':1,'WARMUP':3,'MEASURED':20} and not failures and not errors and len(success)==20
    return dict(scope='INDEPENDENT_TIMING_RECOMPUTATION',total=len(records),phase_counts=phases,failures=failures,
        measured_success=len(success),complete=complete,real_board_pass=False,errors=sorted(set(errors)),
        statistics={k:stats([r[k] for r in success]) for k in METRICS},
        layers=[{k:stats([r['layers'][i][k] for r in success]) for k in LAYER_METRICS} for i in range(4)],
        device_kernel_ns=None,cycles=None,frequency=None,device_timing_reason='H0 has no accepted device counter/frequency interface',scopes=sorted(s for s in scopes if s))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--jsonl',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    records=[json.loads(line) for line in Path(a.jsonl).read_text().splitlines() if line.strip()]
    result=summarize(records);Path(a.out).write_text(json.dumps(result,indent=2)+'\n');raise SystemExit(0 if result['complete'] else 1)

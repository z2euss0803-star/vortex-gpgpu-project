"""Analyze diagnostic counters with explicit containment and no physical-clock claim."""
import argparse,csv,json,pathlib,re

TRACE_V1_FIELDS=('generation', 'job_id', 'sequence', 'op', 'stage', 'result', 'flags', 'submit_enter_ns', 'submit_return_ns', 'wait_enter_ns', 'wait_return_ns', 'gpu_start_ns', 'gpu_done_observed_ns', 'ack_end_ns', 'elapsed_cycles', 'prepare_ns', 'guard_init_ns', 'guard_check_ns', 'state_poll_ns', 'state_poll_calls', 'ack_ns', 'cdma_ns', 'cdma_calls', 'cdma_bytes', 'cdma_sleep_ns', 'cdma_pause_calls', 'guard_cdma_ns', 'guard_cdma_calls', 'guard_cdma_bytes', 'guard_cdma_sleep_ns', 'submit_mutex_wait_ns', 'wait_mutex_wait_ns', 'wait_sleep_ns', 'wait_sleep_calls', 'worker_poll_ns', 'worker_poll_calls', 'guard_depth')
TRACE_V2_FIELDS=TRACE_V1_FIELDS[:-1]+("cdma_fast_poll_ns","cdma_fast_poll_calls","guard_depth")

def analyze_rows(rows,clock_hz=None,program_id=8):
    if not rows:raise ValueError('EMPTY_TRACE')
    if program_id not in (6,7,8,9):raise ValueError('PROGRAM_ID')
    guard_calls=345 if program_id==9 else 130
    if clock_hz is not None and clock_hz<=0:raise ValueError('CLOCK_RATE')
    totals={k:0 for k in ('guard_init','handoff_before_start','gpu_active_observed','guard_check','handoff_after_done')}
    nested={k:0 for k in ('guard_cdma','guard_cdma_sleep','all_cdma','all_cdma_sleep','state_poll','wait_sleep','worker_poll','prepare','mutex_wait')}
    identities=set();jobs=[];raw_cycles=0
    for r in rows:
        if any(type(v) is not int or not 0<=v<2**64 for v in r.values()):raise ValueError('UINT64_RANGE')
        if r.get('cdma_fast_poll_ns',0)+r.get('cdma_sleep_ns',0)>r['cdma_ns']:raise ValueError('FAST_POLL_CONTAINMENT')
        if r.get('cdma_fast_poll_calls',0)>64*r['cdma_calls']:raise ValueError('FAST_POLL_BUDGET')
        ident=(r['generation'],r['job_id'])
        if not r['job_id'] or ident in identities:raise ValueError('DUPLICATE_OR_ZERO_JOB')
        identities.add(ident)
        if r['flags'] or r['guard_depth'] or r['result']:raise ValueError('FAULT_OR_INCOMPLETE_TRACE')
        if not (0<r['submit_enter_ns']<=r['gpu_start_ns']<=r['gpu_done_observed_ns']<=r['ack_end_ns']<=r['wait_return_ns']):raise ValueError('TIMELINE_ORDER')
        if not (r['submit_enter_ns']<=r['submit_return_ns']<=r['wait_enter_ns']<=r['wait_return_ns']):raise ValueError('IOCTL_ORDER')
        guard=r['guard_init_ns']+r['guard_check_ns']
        if r['guard_cdma_calls']!=guard_calls or r['guard_cdma_bytes']!=guard_calls*64:raise ValueError('CRT_GUARD_TRANSFER_COUNT')
        if not (r['guard_cdma_sleep_ns']<=r['guard_cdma_ns']<=guard and r['guard_cdma_ns']<=r['cdma_ns'] and r['cdma_sleep_ns']<=r['cdma_ns']):raise ValueError('NESTED_CDMA_TIMES')
        phases=dict(guard_init=r['guard_init_ns'],handoff_before_start=r['gpu_start_ns']-r['submit_enter_ns']-r['guard_init_ns'],
            gpu_active_observed=r['gpu_done_observed_ns']-r['gpu_start_ns'],guard_check=r['guard_check_ns'],
            handoff_after_done=r['wait_return_ns']-r['gpu_done_observed_ns']-r['guard_check_ns'])
        if min(phases.values())<0:raise ValueError('NEGATIVE_EXCLUSIVE_TIME')
        if r['state_poll_ns']>phases['gpu_active_observed']:raise ValueError('POLL_EXCEEDS_ACTIVE_WINDOW')
        if sum(phases.values())!=r['wait_return_ns']-r['submit_enter_ns']:raise ValueError('PARTITION_SUM')
        for k,v in phases.items():totals[k]+=v
        for k,field in [('guard_cdma','guard_cdma_ns'),('guard_cdma_sleep','guard_cdma_sleep_ns'),('all_cdma','cdma_ns'),('all_cdma_sleep','cdma_sleep_ns'),('state_poll','state_poll_ns'),('wait_sleep','wait_sleep_ns'),('worker_poll','worker_poll_ns'),('prepare','prepare_ns')]:nested[k]+=r.get(field,0)
        nested['mutex_wait']+=r.get('submit_mutex_wait_ns',0)+r.get('wait_mutex_wait_ns',0)
        jobs.append(dict(generation=r['generation'],job_id=r['job_id'],sequence=r['sequence'],stage=r['stage'],op=r['op'],timeline_ns=phases,raw_cycles=r['elapsed_cycles']))
        raw_cycles+=r['elapsed_cycles']
    nested['cdma_fast_poll']=sum(r.get('cdma_fast_poll_ns',0) for r in rows)
    counts={'cdma_fast_poll_calls':sum(r.get('cdma_fast_poll_calls',0) for r in rows)}
    if max([raw_cycles,sum(totals.values())]+list(totals.values())+list(nested.values())+list(counts.values()))>=2**64:raise ValueError('TOTAL_OVERFLOW')
    total=sum(totals.values());conditional=None if clock_hz is None else raw_cycles*1e9/clock_hz
    return dict(scope='DIAGNOSTIC_KERNEL_TIMING_NOT_APPLICATION_PASS',jobs=len(rows),total_kernel_span_ns=total,
        timeline_ns=totals,nested_ns=nested,counts=counts,raw_gpu_cycles=raw_cycles,configured_clock_hz=clock_hz,
        physical_measured_hz=None,gpu_active_conditional_ns=conditional,
        non_hardware_conditional_ns=None if conditional is None else total-conditional,
        notes=['state_poll is contained in observed GPU-active interval',
          'CDMA and sleep counters are nested; worker may overlap WAIT sleep',
          'observed active interval contains hardware, polling, and detection latency',
          'clock conversion requires unchanged rate and accurate reference oscillator'],per_job=jobs)

def read_trace(path):
    lines=pathlib.Path(path).read_text().splitlines()
    if len(lines)<2:raise ValueError('TRACE_SCHEMA')
    match=re.fullmatch(r'#VXL_SUBMIT_TIMING_V([12]) capacity=(\d+) used=(\d+) flags=(\d+) active=(\d+)',lines[0])
    if not match:raise ValueError('TRACE_SCHEMA')
    version,capacity,used,flags,active=map(int,match.groups())
    if flags or active or not 0<used<=capacity<=2048:raise ValueError('TRACE_NOT_COMPLETE')
    reader=csv.DictReader(lines[1:]);expected=TRACE_V1_FIELDS if version==1 else TRACE_V2_FIELDS
    if tuple(reader.fieldnames or ())!=expected:raise ValueError('TRACE_COLUMNS')
    try:rows=[{k:int(v) for k,v in r.items()} for r in reader]
    except (TypeError,ValueError):raise ValueError('TRACE_VALUE')
    if len(rows)!=used:raise ValueError('TRACE_LENGTH')
    return rows

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--trace',type=pathlib.Path,required=True);p.add_argument('--generation',type=int,required=True);p.add_argument('--expect-jobs',type=int,default=1010);p.add_argument('--clock-hz',type=int);p.add_argument('--program-id',type=int,choices=(6,7,8,9),default=8);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
    if a.out.exists():raise SystemExit('FRESH_OUTPUT_REQUIRED')
    rows=[r for r in read_trace(a.trace) if r['generation']==a.generation]
    if len(rows)!=a.expect_jobs:raise SystemExit('EXACT_GENERATION_JOB_COUNT')
    result=analyze_rows(rows,a.clock_hz,program_id=a.program_id);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='per_job'},indent=2))

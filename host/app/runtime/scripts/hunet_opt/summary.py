"""Explicit containment: Linux wall partitions, nested activity and raw GPU cycles."""
import csv,hashlib,importlib.util,json,math,re,struct
from pathlib import Path
from scripts.hunet_opt.catalog import validate_catalog,op_names
ROOT=Path(__file__).resolve().parents[2]
def ns(n):
    if type(n)!=int or not 0<=n<2**64:raise ValueError('UINT64_TIME')
    return n

def management_epoch(rows,generation):
    selected=[r for r in rows if r['generation']==generation]
    if not selected:raise ValueError('MISSING_CURRENT_MANAGEMENT_EPOCH')
    return selected

def envelope(raw,magic,size):
    if len(raw)!=16+size or struct.unpack_from('<4I',raw)!=(magic,1,len(raw),0):raise ValueError('EVIDENCE_ENVELOPE_V1')
    return raw[16:]

def load_breakdown(raw,total_ns,expected_bytes=None):
    raw=envelope(raw,0x4c4f5856,2616);used,flags,asset=struct.unpack_from('<3I',raw);start,end,last,interrupts,result=struct.unpack_from('<4Qi',raw,16)
    if not 0<used<=64 or flags or result or asset!=14 or any(raw[12:16])+any(raw[52:56]) or not 0<start<=last<=end or end-start>ns(total_ns):raise ValueError('LOAD_INCOMPLETE_OR_TIME')
    rows=[];seen=set();by_field={};by_asset={}
    for i in range(used):
        a,f,t,b,c,rc=struct.unpack_from('<IIQQQi',raw,56+40*i)
        if a>14 or f>6 or not c or rc or any(raw[56+40*i+36:56+40*i+40]) or (a,f) in seen:raise ValueError('LOAD_ROW')
        seen.add((a,f));rows.append(dict(asset=a,field=f,ns=t,bytes=b,calls=c));by_field[str(f)]=ns(by_field.get(str(f),0)+t);by_asset[str(a)]=ns(by_asset.get(str(a),0)+t)
    if any(raw[56+40*used:]):raise ValueError('LOAD_UNUSED_ROW')
    required={(a,0) for a in range(13)}|{(a,1) for a in range(1,13)}|{(13,2),(11,3),(12,4),(14,5)}
    if seen!=required:raise ValueError('LOAD_ASSET_COVERAGE')
    mapping={(r['asset'],r['field']):r for r in rows}
    if mapping[0,0]['bytes']!=512 or mapping[12,0]['bytes']!=23685056 or mapping[12,4]['bytes']!=23685056 or mapping[11,3]['bytes']!=mapping[11,0]['bytes'] or mapping[13,2]['calls']!=4 or mapping[14,5]['calls']!=1:raise ValueError('LOAD_TRANSFER_COVERAGE')
    for a in range(1,13):
        if mapping[a,0]['bytes']!=mapping[a,1]['bytes'] or (expected_bytes and mapping[a,0]['bytes']!=expected_bytes[a]):raise ValueError('LOAD_ASSET_BYTES')
    p=partition(end-start,by_field,{})
    return dict(schema=1,rows=rows,by_asset_ns=by_asset,by_field_ns=by_field,interval_ns=end-start,residual_ns=p['residual_ns'],outer_residual_ns=total_ns-(end-start),read_interrupts=interrupts,field_names=['read','hash','reserve','code_copy','weights_copy','seal','validate'],asset_names=['binding','self','cpu','cpu_contract','cpu_stages','jobs','model','aux','optimization','descriptors','layout','code','weights','address','transaction'])

def digest_breakdown(raw):
    raw=envelope(raw,0x44475856,392)
    if any(raw[384:]):raise ValueError('DIGEST_OVERFLOW')
    rows=[]
    for backend in range(4):
        offset=96*backend;calls,b,fail,fallback=struct.unpack_from('<4Q',raw,offset);name=raw[offset+32:offset+96]
        if not calls:
            if any(raw[offset:offset+96]):raise ValueError('DIGEST_EMPTY_ROW')
            continue
        if backend not in (1,2) or fail or fallback>calls or b==0 or b'\0' not in name:raise ValueError('USER_DIGEST_FAILURE')
        text,tail=name.split(b'\0',1)
        if not text or any(tail) or any(c<32 or c>126 for c in text):raise ValueError('DIGEST_IMPLEMENTATION')
        rows.append(dict(backend=backend,implementation=text.decode('ascii'),calls=calls,bytes=b,failures=fail,fallbacks=fallback))
    if sum(r['calls'] for r in rows)<13:raise ValueError('USER_DIGEST_COVERAGE')
    return dict(schema=1,backends=rows,acceleration='NOT_INFERRED_FROM_BACKEND_NAME')

def kernel_digest_breakdown(text,generation,enabled):
    reader=csv.DictReader(text.splitlines());columns=['generation','backend','implementation','calls','bytes','failures','fallbacks','overflow']
    if reader.fieldnames!=columns:raise ValueError('KERNEL_DIGEST_COLUMNS')
    rows=[];seen=set()
    for row in reader:
        numbers={k:int(row[k]) for k in columns if k!='implementation'}
        for v in numbers.values():ns(v)
        b=numbers['backend']
        if numbers['generation']!=generation or b not in (1,3) or b in seen or numbers['failures'] or numbers['overflow'] or not numbers['calls'] or numbers['fallbacks']>numbers['calls'] or not row['implementation']:raise ValueError('KERNEL_DIGEST_IDENTITY_OR_FAILURE')
        seen.add(b);rows.append(dict(numbers,implementation=row['implementation']))
    if bool(rows)!=bool(enabled):raise ValueError('KERNEL_DIGEST_COVERAGE')
    return dict(schema=1,rows=rows,scope='OBSERVED_BACKEND_NOT_SPEEDUP' if enabled else 'LEGACY_PORTABLE_UNINSTRUMENTED')
def partition(total,exclusive,nested):
    total=ns(total)
    if set(exclusive)&set(nested):raise ValueError('NESTED_COUNTED_AS_EXCLUSIVE')
    e={k:ns(v) for k,v in exclusive.items()};n={k:ns(v) for k,v in nested.items()}
    used=sum(e.values())
    if used>total:raise ValueError('NEGATIVE_RESIDUAL_OR_OVERFLOW')
    return dict(total_ns=total,exclusive_ns=e,nested_ns=n,residual_ns=total-used,nested_additive=False)
def validate_management(rows,generation,expected):
    if not rows:raise ValueError('MISSING_MANAGEMENT')
    seen=[];out=[];previous=0
    for r in rows:
        if r['generation']!=generation or r['flags'] or r['result']:raise ValueError('STALE_OR_FAILED_MANAGEMENT')
        start,end,total=ns(r['start_ns']),ns(r['end_ns']),ns(r['total_ns'])
        if start<previous or end<start or end-start!=total:raise ValueError('MANAGEMENT_TIME_ORDER')
        previous=end;key=(r['command']&255,r['token'])
        if key not in expected and key[0]!=5:raise ValueError('UNEXPECTED_TRANSACTION_ROLE')
        if key in seen and key in expected:raise ValueError('DUPLICATE_TRANSACTION_ROLE')
        seen.append(key);out.append(partition(total,r['exclusive'],r['nested']))
    if any(seen.count(key)!=1 for key in expected):raise ValueError('MISSING_TRANSACTION_ROLE')
    return dict(rows=len(rows),partitions=out,scope='MANAGEMENT_NESTED_NOT_ADDITIVE')
def read_management(path):
    lines=Path(path).read_text().splitlines()
    m=re.fullmatch(r'#VXL_MGMT_TIMING_V1 capacity=(\d+) used=(\d+) flags=(\d+) dropped=(\d+) active=(\d+)',lines[0] if lines else '')
    if not m:raise ValueError('MANAGEMENT_SCHEMA')
    capacity,used,flags,dropped,active=map(int,m.groups())
    if flags or dropped or active or not 0<used<=capacity<=512:raise ValueError('MANAGEMENT_INCOMPLETE')
    h=(ROOT/'include/vxl/mgmt_timing.h').read_text();a=h.index('#define VXL_MGMT_FIELDS');b=h.index('enum vxl_mgmt_field',a)
    fields=re.findall(r'X\([A-Z_]+,([a-z_]+)\)',h[a:b]);a=h.index('#define VXL_MGMT_NESTED');b=h.index('enum vxl_mgmt_nested_field',a);nested=re.findall(r'X\([A-Z_]+,([a-z_]+)\)',h[a:b])
    columns=['row','owner_cookie','generation','token','command','start_ns','end_ns','total_ns','result','flags']+[name+suffix for name in fields for suffix in ('_ns','_bytes','_calls')]+['nested_'+name+suffix for name in nested for suffix in ('_ns','_bytes','_calls')]
    reader=csv.DictReader(lines[1:])
    if reader.fieldnames!=columns:raise ValueError('MANAGEMENT_COLUMNS')
    rows=[]
    for i,raw in enumerate(reader):
        r={k:int(v) for k,v in raw.items()}
        if r['row']!=i:raise ValueError('MANAGEMENT_ROW_ORDER')
        for k,v in r.items():
            if k!='result':ns(v)
        if r['owner_cookie']!=((r['generation']<<32)^r['token']):raise ValueError('MANAGEMENT_OWNER_COOKIE')
        r['exclusive']={k:r[k+'_ns'] for k in fields};r['nested']={k:r['nested_'+k+'_ns'] for k in nested};rows.append(r)
    if len(rows)!=used:raise ValueError('MANAGEMENT_LENGTH')
    return rows
def internal_cycles(core_wall,warp_activity):
    if len(core_wall)!=4 or any(len(x)!=2 for x in core_wall) or len(warp_activity)!=4 or any(len(c)!=4 or any(len(w)!=2 for w in c) for c in warp_activity):raise ValueError('INTERNAL_CYCLE_SHAPE')
    for c in core_wall:
        for v in c:ns(v)
    for c in warp_activity:
        for w in c:
            for v in w:ns(v)
    return dict(per_core_wall_cycles=core_wall,per_warp_activity_cycles=warp_activity,global_wall_cycles=None,warp_activity_additive=False)
def gn_internal(raw,entry):
    if len(raw)!=448:raise ValueError('GN_DETAIL_SIZE')
    counts=struct.unpack_from('<64I',raw);mean,invstd=struct.unpack_from('<2f',raw,256);error,workers,count=struct.unpack_from('<3I',raw,264)
    if entry['op']==4:j=entry['words'];n=j[16]*j[18]*j[19]
    elif entry['op']==16:j=entry['nodes'][0];n=j[12]*j[14]*j[15]
    else:raise ValueError('GN_DETAIL_ROLE')
    expected=1 if n<4096 else 16 if n<65536 else 64
    if error or workers!=expected or count!=n or not math.isfinite(mean) or not math.isfinite(invstd) or any(raw[276:320]):raise ValueError('GN_DETAIL_STATUS')
    want=[0]*64 if entry['op']==16 else [n*(i+1)//workers-n*i//workers if i<workers else 0 for i in range(64)]
    if list(counts)!=want:raise ValueError('GN_WORK_COVERAGE')
    values=struct.unpack_from('<16Q',raw,320)
    return dict(per_core_phase_cycles=[list(values[i*4:(i+1)*4]) for i in range(4)],global_wall_cycles=None,cores_additive=False,scope='RAW_PHASE_INTERVALS_INCLUDE_SYNCHRONIZATION_NOT_GLOBAL_WALL')
def replay_history(path):
    path=Path(path);manifest=json.loads((path/'manifest.json').read_bytes())
    if manifest['run_id']!='39fb432d2a3549a5abdf8644ca6c2d6e' or manifest['scope']!='COPIED_HISTORICAL_ONLY_NOT_NEW_GPU':raise ValueError('HISTORY_ROLE')
    files={}
    for name,record in manifest['files'].items():
        raw=(path/name).read_bytes()
        if len(raw)!=record['bytes'] or hashlib.sha256(raw).hexdigest()!=record['sha256']:raise ValueError('HISTORICAL_FILE_IDENTITY')
        files[name]=raw
    m=json.loads(files['raw/run.json']);t=json.loads(files['raw/timing.json']);gb=files['raw/gpu-records.bin'];cb=files['raw/cpu-records.bin']
    if len(gb)!=103*448 or len(cb)!=13*712:raise ValueError('HISTORY_COVERAGE')
    gpu=[];operators={};names=op_names()
    for i,g in enumerate(t['gpu']):
        raw=gb[i*448:(i+1)*448];cycles=struct.unpack_from('<I',raw,268)[0];op=struct.unpack_from('<I',raw,12)[0]
        if g['elapsed_raw']!=cycles:raise ValueError('HISTORY_RAW_CYCLE_MISMATCH')
        gpu.append(dict(g,elapsed_cycles=cycles));r=operators.setdefault(str(op),dict(name=names[op],jobs=0,raw_cycles=0));r['jobs']+=1;r['raw_cycles']+=cycles
    spec=importlib.util.spec_from_file_location('opt_history_old_summary',ROOT/'scripts/hunet_fused/summary.py');old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
    meta=dict(m,timing_extension=t['handoff_v1']);result=old.timing_summary(gpu,t['cpu'],meta,96969696)
    if (result['T_load_ns'],result['T_resident_ns'],result['T_CPU_handler_ns']-result['T_CPU_handler_checkpoint_ns'],result['raw_gpu_cycles'])!=(4031236701,9270748114,1468704887,686600903):raise ValueError('HISTORICAL_TIMING_DRIFT')
    return dict(scope='HISTORICAL_39FB432D_REPLAY_NOT_NEW_GPU',timing=result,operators=operators,new_candidate_gpu_cycles=None)
def eligible_for_combine(receipts):
    seen=set();eligible=[]
    for r in receipts:
        candidate=r['candidate']
        if candidate in seen:raise ValueError('DUPLICATE_CANDIDATE')
        seen.add(candidate)
        if r.get('combinable',True) and r.get('software')=='PASS' and r.get('r2')=='PASS' and r.get('runtime_finite') is True and r.get('scope') in ('HOST_NUMERIC_MODEL','HOST_SOFTWARE') and r.get('gpu_cycles') is None and all(re.fullmatch('[0-9a-f]{64}',r.get(k,'')) for k in ('source_sha256','catalog_sha256')):eligible.append(candidate)
    return eligible
def expected_owner_counts(catalog,entry):
    matrices={s['stage_id']:s for s in catalog['matrix_layout']['matrix_stages']};out=[0]*4
    stages=[entry['first_stage']] if entry['op']==15 else [n[1] for n in entry['nodes'] if n[0]==15]
    for stage in stages:
        groups=[]
        for d in matrices[stage]['descriptors']:
            cols=d[4]//4
            for row in range(d[3]//8):
                for col in range(0,cols,catalog['tile_n']):groups.append(min(catalog['tile_n'],cols-col))
        active=min(4,(len(groups)+3)//4);offset=0
        for core in range(active):
            count=len(groups)//active+(core<len(groups)%active);out[core]+=sum(groups[offset:offset+count]);offset+=count
    return out
def validate_submit(rows,catalog,generation):
    wanted=[e for e in catalog['entries'] if e['kind']=='GPU']
    if len(rows)!=len(wanted):raise ValueError('SUBMIT_TRACE_COVERAGE')
    totals={};nested={};cycles=0;previous=0
    for r,e in zip(rows,wanted):
        for v in r.values():ns(v)
        if r['generation']!=generation or r['sequence']!=e['first'] or r['op']!=e['op'] or r['stage']!=e['first_stage'] or r['flags'] or r['result'] or r['guard_depth']:raise ValueError('SUBMIT_TRACE_IDENTITY_FLAGS')
        if not (previous<=r['submit_enter_ns']<=r['gpu_start_ns']<=r['gpu_done_observed_ns']<=r['ack_end_ns']<=r['wait_return_ns']):raise ValueError('SUBMIT_TRACE_ORDER')
        if not r['job_id'] or not 0<r['elapsed_cycles']<2**32:raise ValueError('SUBMIT_JOB_CYCLE')
        if not (r['submit_enter_ns']<=r['submit_return_ns']<=r['wait_enter_ns']<=r['wait_return_ns']):raise ValueError('SUBMIT_IOCTL_ORDER')
        previous=r['wait_return_ns']
        # 65 PL CRT stack guards at init and check, 212 tensor guards,
        # seven retained/new metadata guards; PS CRT guards use coherent memory.
        guard_calls=349+(2 if catalog['layout']['execution_policy']['parallel_gn'] and e['op'] in (4,16) else 0)
        if r['guard_cdma_calls']!=guard_calls or r['guard_cdma_bytes']!=guard_calls*64:raise ValueError('OPT_GUARD_TRANSFER_COVERAGE')
        guard=r['guard_init_ns']+r['guard_check_ns']
        if not (r['guard_cdma_sleep_ns']<=r['guard_cdma_ns']<=guard and r['guard_cdma_ns']<=r['cdma_ns'] and r['cdma_sleep_ns']+r.get('cdma_fast_poll_ns',0)<=r['cdma_ns']):raise ValueError('SUBMIT_CDMA_CONTAINMENT')
        if r.get('cdma_fast_poll_calls',0)>64*r['cdma_calls']:raise ValueError('FASTWAIT_BUDGET')
        parts=dict(guard_init=r['guard_init_ns'],handoff_before_start=r['gpu_start_ns']-r['submit_enter_ns']-r['guard_init_ns'],gpu_active_observed=r['gpu_done_observed_ns']-r['gpu_start_ns'],guard_check=r['guard_check_ns'],handoff_after_done=r['wait_return_ns']-r['gpu_done_observed_ns']-r['guard_check_ns'])
        detail={k:r[k] for k in ('guard_cdma_ns','cdma_ns','cdma_sleep_ns','state_poll_ns','wait_sleep_ns','worker_poll_ns','prepare_ns')};detail['cdma_fast_poll_ns']=r.get('cdma_fast_poll_ns',0)
        closure=partition(r['wait_return_ns']-r['submit_enter_ns'],parts,detail)
        if closure['residual_ns'] or r['state_poll_ns']>parts['gpu_active_observed']:raise ValueError('SUBMIT_TIME_CLOSURE')
        for k,v in parts.items():totals[k]=ns(totals.get(k,0)+v)
        for k,v in detail.items():nested[k]=ns(nested.get(k,0)+v)
        cycles=ns(cycles+r['elapsed_cycles'])
    return dict(exclusive_ns=totals,nested_ns=nested,raw_gpu_cycles=cycles,nested_additive=False)
def summarize(run,catalog):
    validate_catalog(catalog);run=Path(run);m=json.loads((run/'run.json').read_bytes());t=json.loads((run/'timing.json').read_bytes())
    if m.get('error') or m.get('scope')!='HUNET_OPT_LINUX_V2' or t.get('scope')!='TARGET_RAW_UNAUDITED':raise ValueError('TARGET_RAW_SCOPE_REQUIRED')
    gpu=t.get('gpu',[]);cpu=t.get('cpu',[]);wanted=[e for e in catalog['entries'] if e['kind']=='GPU']
    if len(gpu)!=len(wanted) or len(cpu)!=13:raise ValueError('MISSING_TIMING_ROWS')
    gb=(run/'gpu-records.bin').read_bytes();cb=(run/'cpu-records.bin').read_bytes()
    if len(gb)!=len(wanted)*448 or len(cb)!=13*712:raise ValueError('RAW_RECORD_COVERAGE')
    cycles=0;by_op={};names=op_names()
    for index,(g,e) in enumerate(zip(gpu,wanted)):
        if any(g[k]!=e[k] for k in ('first','last','op')) or g['stage']!=e['first_stage']:raise ValueError('GPU_TIMING_ROLE')
        raw=gb[index*448:(index+1)*448];meta=[e[k] for k in ('first','last','first_stage','last_stage','op','block_id','owner_tiles')]+[len(e['materialized_stages'])]
        if list(struct.unpack_from('<32I',raw))!=e['words'] or list(struct.unpack_from('<8I',raw,128))!=meta:raise ValueError('RAW_GPU_CATALOG_IDENTITY')
        if tuple(g[k] for k in ('T_submit_wait_ns','T_evidence_ns','T_reap_ns'))!=struct.unpack_from('<3Q',raw,416) or struct.unpack_from('<Q',raw,440)[0]:raise ValueError('RAW_GPU_TIMING_MISMATCH')
        c=g['elapsed_raw']
        if c!=struct.unpack_from('<I',raw,268)[0]:raise ValueError('RAW_GPU_CYCLE_MISMATCH')
        if type(c)!=int or not 0<c<2**32:raise ValueError('RAW_CYCLE_MISSING_OR_OVERFLOW')
        cycles+=c;q=by_op.setdefault(str(e['op']),dict(name=names[e['op']],jobs=0,raw_cycles=0));q['jobs']+=1;q['raw_cycles']+=c
    ns(cycles)
    path=ROOT/'scripts/hunet_fused/summary.py';spec=importlib.util.spec_from_file_location('opt_old_interval',path);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
    gs=[dict(g,elapsed_cycles=g['elapsed_raw']) for g in gpu]
    meta=dict(m,timing_extension=t['handoff_v1']);base=old.timing_summary(gs,cpu,meta,96969696)
    tokens=[];roles=[e for e in catalog['entries'] if e['kind']=='CPU']
    for i,(c,e) in enumerate(zip(cpu,roles)):
        raw=cb[i*712:(i+1)*712];token=struct.unpack_from('<Q',raw,48)[0]
        if not token or c['sequence']!=e['first'] or c['stage']!=e['first_stage'] or struct.unpack_from('<2I',raw,36)!=(e['first'],e['first_stage']):raise ValueError('RAW_CPU_ROLE')
        if tuple(c[k] for k in ('T_begin_call_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns'))!=struct.unpack_from('<5Q',raw,672):raise ValueError('RAW_CPU_TIMING_MISMATCH')
        if c['T_compute_ns']!=struct.unpack_from('<Q',raw,656)[0]:raise ValueError('RAW_CPU_COMPUTE_TIMING')
        tokens.append((18,token))
    required=[(17,0),(13,0),(21,tokens[-1][1])]+tokens+[(19,token) for _,token in tokens]
    management=validate_management(management_epoch(read_management(run/'management.csv'),m['generation']),m['generation'],required)
    spec=importlib.util.spec_from_file_location('opt_submit_parser',ROOT/'scripts/hunet_timing/analyze.py');parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
    rows=[r for r in parser.read_trace(run/'submit.csv') if r['generation']==m['generation']]
    submit=validate_submit(rows,catalog,m['generation'])
    if submit['raw_gpu_cycles']!=cycles:raise ValueError('RAW_SUBMIT_CYCLE_MISMATCH')
    for i,r in enumerate(rows):
        if r['job_id']!=struct.unpack_from('<Q',gb,i*448+192)[0]:raise ValueError('SUBMIT_JOB_ID_MISMATCH')
    loading=load_breakdown((run/'load-timing.bin').read_bytes(),m['T_load_ns']);user_digest=digest_breakdown((run/'user-digest-audit.bin').read_bytes());kernel_digest=kernel_digest_breakdown((run/'kernel-digest.csv').read_text(),m['generation'],catalog['layout']['execution_policy']['user_kernel_sha']=='auto')
    return dict(scope='TARGET_TIMING_REQUIRES_RELEASE_AND_NUMERIC_AUDIT',timing=base,loading=loading,user_digest=user_digest,kernel_digest=kernel_digest,gpu_by_operator=by_op,raw_gpu_cycles=cycles,submit=submit,management=management,virtual_stages=[s['stage'] for s in catalog['operator_coverage'] if not s['materialized']],physical_measured_hz=None,publish_performance=False)

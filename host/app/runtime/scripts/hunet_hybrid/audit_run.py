"""Independent raw-receipt audit. Never run a target program or change a threshold."""
import argparse, hashlib, importlib.util, json, math, re, struct, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('hybrid_numeric_frozen',ROOT/'scripts/hunet/audit_run.py')
pack=load('hybrid_release_auditor',ROOT/'scripts/hunet_hybrid/package_release.py')
freeze=pack.freeze
check=old.check
digest=old.digest
def read(p):return json.loads(Path(p).read_bytes())
def ints(b,off,n=1):return struct.unpack_from('<%dI'%n,b,off)
def longs(b,off,n=1):return struct.unpack_from('<%dQ'%n,b,off)
def header(b,magic,size):check(len(b)==size and ints(b,0,4)==(magic,1,size,0),'PAYLOAD_HEADER')
def finite(b):check(len(b)%4==0 and all(math.isfinite(v[0]) for v in struct.iter_unpack('<f',b)),'NONFINITE_TENSOR')
def top5(values):return [dict(class_index=i,score=values[i]) for i in sorted(range(len(values)),key=lambda n:(-values[n],n))[:5]]
def ns(value,label):check(type(value) is int and 0<=value<2**64,'TIMING:'+label);return value
def formal_gate(a,current,authorized):
 return bool(authorized is True and a.get('integrity')=='PASS' and a.get('scope')=='HW4_HYBRID_READONLY_PREFLIGHT' and a.get('mode')=='diagnostic' and a.get('R2',{}).get('status')=='PASS' and a.get('ORT',{}).get('status')=='PASS' and all(a.get(k) and a.get(k)==current.get(k) for k in ('boot_id','binding_sha256')))

def timing_check(m,gpu,cpu,payload):
 t=read_bytes_json(payload,'timing.json');check(set(t)=={'gpu','cpu'},'TIMING_ROLE_KEYS')
 check([x.get('sequence') for x in t['gpu']]==[x['sequence'] for x in gpu],'GPU_TIMING_MISSING_DUPLICATE')
 check([x.get('sequence') for x in t['cpu']]==[x['sequence'] for x in cpu],'CPU_TIMING_MISSING_DUPLICATE')
 total=0;capture=0
 for role,rows,records in [('GPU',t['gpu'],gpu),('CPU',t['cpu'],cpu)]:
  for row,e in zip(rows,records):
   seq=row['sequence'];instant=read_bytes_json(payload,'metrics/%03u-timing.json'%seq)
   check(instant==dict(role=role,**row),'INSTANT_TIMING:'+str(seq))
   required={'sequence','T_submit_wait_ns','T_evidence_ns','T_reap_ns','T_capture_ns'} if role=='GPU' else {'sequence','stage','T_begin_call_ns','T_compute_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns'}
   check(set(row)==required,'TIMING_FIELDS_OR_FAKE_CPU_CYCLES')
   for k,v in row.items():ns(v,k)
   if role=='GPU':
    total+=sum(row[k] for k in required if k.startswith('T_'));capture+=row['T_capture_ns']
   else:
    check(row['stage']==e['stage'] and row['T_compute_ns']==e['user_cpu_ns'],'CPU_COMPUTE_RECEIPT')
    # Compute and reference are disjoint calls between BEGIN and COMMIT.
    total+=sum(row[k] for k in required if k.startswith('T_'))
    check((row['T_cpu_reference_ns']>0)==(m['mode']!='formal'),'CPU_REFERENCE_SCOPE')
   e.update(row)
 for k in ['T_load_ns','T_resident_ns','T_checkpoint_ns','T_diagnostic_capture_ns']:ns(m.get(k),k)
 check(m['T_resident_ns']<=180000000000 and total+m['T_checkpoint_ns']<=m['T_resident_ns'],'WALL_SCOPE_OR_DEADLINE')
 check(capture==m['T_diagnostic_capture_ns'],'CAPTURE_TIMING_SCOPE')
 check(m.get('T_transfer_ns') is None and m.get('gpu_seconds') is None,'UNMEASURED_TRANSFER_OR_CLOCK')

def read_bytes_json(payload,name):
 check(name in payload,'MISSING_JSON:'+name);return json.loads(payload[name])

def audit_hybrid(run:Path,release:Path,references:dict)->dict:
 start=time.monotonic_ns();out=dict(integrity='FAIL',scope='UNVERIFIED',per_layer_cpu=[],gpu=[],logical=[],stage_errors=[],R2=dict(status='NOT_EVALUATED'),ORT=dict(status='NOT_EVALUATED'),v5_bitwise_relation='NOT_EVALUATED',publish_performance=False,formal_allowed=False,gpu_seconds=None,gpu_seconds_conditional=None,T_transfer_ns=None)
 try:
  run=Path(run);release=Path(release);m=read(run/'run.json');r=read(release/'release-hybrid.json');gate=references.get('gate')
  if isinstance(gate,(str,Path)):gate=read(gate)
  check(isinstance(gate,dict) and gate.get('integrity')=='PASS','READONLY_GATE_REQUIRED')
  check(gate.get('scope') in ['HW4_HYBRID_READONLY_PREFLIGHT','TEST_HYBRID_PROTOCOL_NOT_BOARD'],'GATE_SCOPE')
  real=gate['scope']=='HW4_HYBRID_READONLY_PREFLIGHT';out['scope']=gate['scope']
  check(r.get('profile')==4 and r.get('program_id')==8,'RELEASE_PROFILE')
  if real:
   verified=pack.verify_hybrid_release(release);check(verified.get('integrity')=='PASS' and verified.get('scope')=='HUNET_HYBRID_OFFLINE_RELEASE','RELEASE_AUDIT')
  else:check(r.get('scope')=='TEST_FIXTURE_HYBRID_NOT_CROSS','FIXTURE_RELEASE_REQUIRED')
  check(m.get('scope')=='HUNET_HYBRID_LINUX_V1' and m.get('transport')=='SCP_CAPTURE','RAW_SCOPE')
  check(m.get('state') in ['PREFIX_STOP','RAW_COMPLETE_REQUIRES_AUDIT'] and m.get('error')==0,'FAIL_STOP_RAW')
  check(m.get('mode') in ['first-gelu','diagnostic','formal'] and m.get('profile')==4 and m.get('program_id')==8,'RAW_MODE_PROFILE')
  for k in ['boot_id','build_id','kernel','profile']:check(m.get(k) and m[k]==gate.get(k),'STALE_EPOCH:'+k)
  for k in ['source_digest','binding_sha256']:check(r.get(k)==gate.get(k),'SOURCE_BINDING:'+k)
  check(type(m.get('generation')) is int and m['generation']>=gate.get('minimum_generation',1),'GENERATION')
  check(re.fullmatch('[0-9a-f]{32}',m.get('run_id','')) and int(m['run_id'],16)>0,'RUN_ID');rid=bytes.fromhex(m['run_id']);roi=m.get('roi_index')
  check(type(roi) is int and 0<=roi<127,'ROI');inputs=read(ROOT/'contracts/hunet_inputs.json')['records'];check(inputs[roi]['included'],'EXCLUDED_ROI')
  check(m.get('sample_kind') in [1,2,3,4] and type(m.get('sample_index')) is int and 0<=m['sample_index']<{1:1,2:3,3:20,4:256}[m['sample_kind']],'SAMPLE')
  if m['mode']=='formal':check(formal_gate(gate.get('diagnostic_audit',{}),gate,gate.get('formal_authorized') is True),'FORMAL_NUMERIC_AUTH_GATE')
  if gate.get('expected_run_id'):check(gate['expected_run_id']==m['run_id'],'UNEXPECTED_RUN_ID')
  payload={n:old.owned_file(run,n,v) for n,v in m['files'].items()}
  actual={p.relative_to(run).as_posix() for p in run.rglob('*') if p.is_file() or p.is_symlink()}
  check(actual==set(payload)|{'run.json'},'UNLISTED_RAW_FILE')
  binding=payload.get('binding-hybrid.bin',b'');canonical=(release/'binding-hybrid.bin').read_bytes()
  check(len(binding)==384 and len(canonical)==384 and ints(binding,0,6)==(0x44424c48,2,384,4,8,7),'BINDING_HEADER')
  check(ints(binding,24,2)==(m['generation'],0) and binding[32:]==canonical[32:],'BINDING_GENERATION_IDENTITY')
  check(ints(canonical,24,2)==(0,0) and digest(canonical)==r['binding_sha256'] and binding[96:128].hex()==r['source_digest'] and not any(binding[352:]),'BINDING_CANONICAL')
  runtime=read_bytes_json(payload,'cpu-runtime.json');check(runtime.get('algorithm')=='ERF_NATIVE_V1' and runtime.get('threads')==4 and runtime.get('precision_flags')==['-fno-fast-math','-ffp-contract=off'],'CPU_RUNTIME_CONTRACT')
  check(runtime.get('libm_sha256')==gate.get('libm_sha256') and runtime.get('compiler')==gate.get('cpu_compiler') and runtime.get('libm_bytes',0)>0,'CPU_RUNTIME_IDENTITY')
  jobs=(release/'base/jobs.bin').read_bytes();check(len(jobs)==130944 and digest(jobs)==binding[160:192].hex(),'CATALOG_IDENTITY');catalog=list(struct.iter_unpack('<32I',jobs))
  role=read(release/'cpu-stages.json');freeze.verify_baseline(role);check(digest((release/'cpu-stages.json').read_bytes())==binding[320:352].hex(),'CPU_ROLE_BINDING')
  check(digest((release/'cpu-contract.json').read_bytes())==binding[288:320].hex() and read(release/'cpu-contract.json')==pack.cpu_contract(),'CPU_CONTRACT_BINDING')
  check(digest((release/'cpu/gelu.elf').read_bytes())==binding[256:288].hex(),'CPU_BINARY_BINDING')
  check(digest(payload.get('input.f32',b''))==inputs[roi]['sha256'],'LIVE_INPUT_IDENTITY')
  begin=payload.get('begin.bin',b'');header(begin,0x47424c48,128);check(begin[16:32]==rid and ints(begin,32,4)==(roi,m['sample_kind'],m['sample_index'],0),'BEGIN_RUN_SAMPLE')
  check(longs(begin,56)[0]==196608 and begin[64:96].hex()==digest(payload['input.f32']) and not any(begin[96:]),'BEGIN_INPUT')
  prefix=m['mode']=='first-gelu';last=392 if prefix else 1023
  check(m['logical_completed']==last and m['gpu_records']==(391 if prefix else 1010) and m['cpu_records']==(1 if prefix else 13),'ROLE_COUNTS')
  check(payload.get('logical_args.bin')==jobs[:last*128],'LOGICAL_CATALOG')
  check({n for n in payload if n.startswith('logical/')}=={'logical/%03u-args.bin'%q for q in range(1,last+1)},'LOGICAL_CHECKPOINT_COVERAGE')
  for q in range(1,last+1):check(payload['logical/%03u-args.bin'%q]==jobs[(q-1)*128:q*128],'LOGICAL_CHECKPOINT_ARGS')
  cpu_map={x['sequence']:x for x in role['cpu_stages']};gpu=[];cpu=[];first_id=None;counter=None;initial=None;last_ack=0;tokens=set();gpu_names=set();cpu_names=set();metric_names=set()
  for seq,j in enumerate(catalog[:last],1):
   metric_names.add('metrics/%03u-timing.json'%seq)
   if seq not in cpu_map:
    name='gpu/%03u-evidence.bin'%seq;gpu_names.add(name);e=payload.get(name,b'');header(e,0x454a4c48,256);check(e[16:32]==rid,'GPU_RUN:'+str(seq))
    job,submit,done,ack=longs(e,32,4);first_id=job if first_id is None else first_id;check(job==first_id+len(gpu) and job>0,'GPU_JOB_ID')
    check(ints(e,64,4)==(seq,j[3],j[30],8),'GPU_SEQUENCE_ROLE')
    status,errors,pw,pr,before,after,watchdog,elapsed=ints(e,80,8);initial=before if initial is None else initial
    check(status&2 and not status&4 and errors==pw==pr==0 and watchdog==4000000000 and after==(before+1)&0xffffffff and (counter is None or before==counter),'GPU_COUNTER_STATUS');counter=after
    ready,idle,cdma,valid=ints(e,112,4);check(ready==idle==1 and not cdma&0x4770 and valid&27==27 and not valid&~31,'GPU_IDLE_VALID_CYCLES')
    check(ints(e,128,4)==(0,0,0,0) and ints(e,144,4)==(3,0,m['generation'],0) and ints(e,208)[0]==3 and not any(e[228:]),'GPU_EXIT_GENERATION_GUARD')
    check(e[160:192].hex()==digest(jobs[(seq-1)*128:seq*128]),'GPU_ARGS_SHA')
    deadline,wait=longs(e,192,2);check(last_ack<=submit<=done<=ack and done<=deadline and deadline==submit+60000000000 and wait<=60000,'GPU_MONOTONIC_DEADLINE');last_ack=ack
    if valid&4:check(j[3]==2 and sum(ints(e,212,4))==j[7],'GPU_OWNER_COUNTS')
    else:check(not any(e[212:228]),'GPU_OWNER_FLAGS')
    gpu.append(dict(sequence=seq,stage=j[30],op=j[3],job_id=job,elapsed_cycles=elapsed,jobs_before=before,jobs_after=after,submit_ns=submit,done_ns=done,ack_ns=ack))
   else:
    s=cpu_map[seq];stage=s['stage'];n=s['elements']*4;pre='cpu/%03u-'%stage
    names={pre+x for x in ['begin.bin','input.f32','output.f32','compute.bin','commit.bin','evidence.bin','writeback.f32']}
    if not m['mode']=='formal':names.add(pre+'reference.f32')
    cpu_names|=names;check(names<=set(payload),'CPU_COVERAGE');b=payload[pre+'begin.bin'];q=payload[pre+'commit.bin'];e=payload[pre+'evidence.bin'];header(b,0x42435948,128);header(q,0x43435948,128);header(e,0x45435948,384)
    token=longs(b,48)[0];check(token and token not in tokens,'CPU_TOKEN');tokens.add(token)
    check(b[16:32]==q[16:32]==e[32:48]==rid and ints(b,32,4)==(m['generation'],seq,stage,1) and ints(q,32,4)==(m['generation'],seq,stage,1) and ints(e,16,4)==(1,m['generation'],seq,stage),'CPU_RUN_SEQUENCE_STAGE')
    check(longs(q,48)[0]==longs(e,48)[0]==token and longs(b,64,2)==(n,n) and longs(q,64)[0]==n and longs(e,64,2)==(n,n),'CPU_TOKEN_BOUNDS')
    x=payload[pre+'input.f32'];y=payload[pre+'output.f32'];check(len(x)==len(y)==n,'CPU_TENSOR_SIZE');finite(x);finite(y)
    check(b[80:112]==e[80:112]==hashlib.sha256(x).digest() and q[80:112]==e[112:144]==hashlib.sha256(y).digest() and e[144:176]==binding[160:192],'CPU_LIVE_HASH')
    check(payload[pre+'writeback.f32']==y,'CPU_WRITEBACK')
    if m['mode']!='formal':check(payload[pre+'reference.f32']==y,'CPU_INDEPENDENT_REFERENCE_BITWISE')
    check(ints(q,112)[0]==4 and ints(e,56,2)==(1,4) and not any(b[112:]) and not any(q[116:]) and not any(e[256:]),'CPU_RESERVED_OR_ALGORITHM')
    check(ints(e,216,6)==(counter,counter,ints(e,224)[0],2,0,63) and not ints(e,224)[0]&0x4770,'CPU_ZERO_GPU_DELTA_IDLE_CHECKS')
    a,z,d,threads,reserved=struct.unpack('<3Q2I',payload[pre+'compute.bin']);read_ns,write_ns,guard_ns,total,user=longs(e,176,5)
    check(a<=z and z-a==d==user==longs(q,72)[0] and threads==4 and reserved==0 and d>0,'CPU_COMPUTE_TIMING')
    check(total>=read_ns+write_ns+guard_ns+user and total<=60000000000 and longs(e,248)[0]>=total,'CPU_DRIVER_TIMING_DEADLINE')
    cpu.append(dict(sequence=seq,stage=stage,elements=s['elements'],input_sha256=digest(x),output_sha256=digest(y),read_ns=read_ns,write_ns=write_ns,guard_ns=guard_ns,total_ns=total,user_cpu_ns=user,gpu_jobs_before=counter,gpu_jobs_after=counter,target_reference='BITWISE_EQUAL' if m['mode']!='formal' else 'NOT_RUN'))
   out['logical'].append(dict(sequence=seq,stage=j[30],op=j[3],role='CPU' if seq in cpu_map else 'GPU'))
  check({x for x in payload if x.startswith('gpu/')}==gpu_names and {x for x in payload if x.startswith('cpu/')}==cpu_names and {x for x in payload if x.startswith('metrics/')}==metric_names,'EXACT_ROLE_COVERAGE')
  if m['mode']!='formal':
   stages=read(ROOT/'generated/hunet/catalog.json')['stages'];stage_names=set();trace_names=set()
   for s in stages:
    if s['last_sequence']>last:continue
    name='stages/%03u.f32'%s['stage_id'];stage_names.add(name);check(len(payload.get(name,b''))==s['bytes'],'STAGE_SIZE');finite(payload[name])
    record=references.get('stages',{}).get(str(s['stage_id']))
    if record:
     baseline=old.checked_reference(record);check(len(baseline)==len(payload[name]),'STAGE_REFERENCE_SIZE');a=[x[0] for x in struct.iter_unpack('<f',payload[name])];b=[x[0] for x in struct.iter_unpack('<f',baseline)];finite(baseline)
     out['stage_errors'].append(dict(stage=s['stage_id'],relation='EQUAL' if payload[name]==baseline else 'DIFFERENT',max_abs=max(abs(x-y) for x,y in zip(a,b)),reference_sha256=digest(baseline),scope='V5_DIFFERENT_PATH_COMPARISON'))
   for j in catalog[:last]:
    if j[3]==2:name='traces/%04u.u32'%j[4];trace_names.add(name);old.owner_trace(payload.get(name,b''),j[7])
   check({n for n in payload if n.startswith('stages/')}==stage_names and {n for n in payload if n.startswith('traces/')}==trace_names,'EXACT_STAGE_TRACE_COVERAGE')
  else:check(not any(n.startswith(('stages/','traces/')) for n in payload),'FORMAL_EXTRA_DIAGNOSTIC')
  timing_check(m,gpu,cpu,payload)
  if prefix:
   check('end.bin' not in payload and read_bytes_json(payload,'prefix-stop.json')==dict(state='PREFIX_STOP',logical_jobs=392,gpu_jobs=391,cpu_jobs=1),'PREFIX_NOT_FULL_END')
   if gate.get('first_cpu_input_sha256'):check(cpu[0]['input_sha256']==gate['first_cpu_input_sha256'],'FIRST_LIVE_INPUT_BASELINE')
   if real:check(re.fullmatch('[0-9a-f]{64}',gate.get('first_cpu_input_sha256','')),'FIRST_LIVE_INPUT_BASELINE_REQUIRED')
   s=importlib.util.spec_from_file_location('hybrid_pack_oracle',ROOT/'scripts/hunet_hybrid/pack_reference.py');oracle=importlib.util.module_from_spec(s);s.loader.exec_module(oracle)
   descriptors=(release/'base/matrix_descriptors.bin').read_bytes();check(digest(descriptors)==binding[192:224].hex(),'AUX_IDENTITY')
   pack_names=set()
   for j in catalog[:last]:
    if j[30]!=9 or j[3]!=1:continue
    name='packs/%04u.f16'%j[4];pack_names.add(name);offset=j[15]-catalog[0][15];check(offset>=0 and offset%32==0 and offset+32<=len(descriptors),'PACK_AUX_BOUNDS');matrix=struct.unpack_from('<8I',descriptors,offset)
    expected=oracle.pack_reference(j,matrix,payload['cpu/008-output.f32']);check(payload.get(name)==expected,'FIRST_PACK_INDEPENDENT_RNE_LAYOUT:'+name)
   check(len(pack_names)==32 and {n for n in payload if n.startswith('packs/')}==pack_names,'FIRST_PACK_EXACT_COVERAGE');out['first_pack_reference']='32_PACKS_BITWISE_EQUAL'
   check(m['state']=='PREFIX_STOP','PREFIX_STATE');out['stop_reason']='PREFIX_ONLY_NOT_FULL_NUMERIC'
  else:
   check(not any(n.startswith('packs/') for n in payload),'NON_PREFIX_EXTRA_PACK_CAPTURE')
   check(m['state']=='RAW_COMPLETE_REQUIRES_AUDIT','FULL_STATE');e=payload.get('end.bin',b'');header(e,0x4e455948,256)
   check(e[16:32]==rid and ints(e,32,8)==(roi,m['generation'],1023,1010,13,100,1,31),'END_COUNTS_IDENTITY');check(longs(e,64)[0]==first_id+1009 and ints(e,72,2)==(initial,counter) and (counter-initial)&0xffffffff==1010,'END_HW_COUNTER')
   check(e[80:112].hex()==digest(payload['input.f32']) and e[112:144].hex()==digest(payload['logits.f32']) and e[144:176].hex()==r['binding_sha256'] and not any(e[176:]),'END_FULL_HASH_RESERVED')
   check(payload.get('guards.bin')==b'\xa5'*13568,'FULL_GUARDS');old.owner_trace(payload.get('last_trace.u32',b''),1014)
   numeric=old.numeric(payload['logits.f32'],roi,references);out['R2']=numeric['R2_numeric'];out['ORT']=numeric['ORT_application_numeric'];v=numeric['Linux-v5_same_math']['status'];out['v5_bitwise_relation']={'PASS':'EQUAL','FAIL':'DIFFERENT'}.get(v,v)
   out['topk']=dict(scope=gate['scope'],candidate=top5(list(struct.unpack('<8105f',payload['logits.f32']))))
   for role,label in [('original_R2','R2'),('original_ORT_FP32','ORT')]:
    if role in references:out['topk'][label]=top5(old.read_npy_row(Path(references[role]['path']),roi))
   out['stop_reason']='' if out['R2']['status']==out['ORT']['status']=='PASS' else 'FROZEN_NUMERIC_FAILURE_STOP_NO_FORMAL'
  out.update(integrity='PASS',gpu=gpu,per_layer_cpu=cpu,**{k:m[k] for k in ['run_id','boot_id','build_id','kernel','generation','mode','roi_index','sample_kind','sample_index','T_load_ns','T_resident_ns','T_checkpoint_ns','T_diagnostic_capture_ns']})
  out['binding_sha256']=r['binding_sha256'];out['source_digest']=r['source_digest'];out['raw_gpu_cycles']=sum(x['elapsed_cycles'] for x in gpu);out['cpu_compute_ns']=sum(x['user_cpu_ns'] for x in cpu)
  if gate.get('framework_hz') is not None:
   hz=gate['framework_hz'];check(type(hz) is int and hz>0,'CONDITIONAL_FRAMEWORK_HZ');out['gpu_seconds_conditional']=dict(scope='FRAMEWORK_RATE_CONVERSION_NOT_PHYSICAL_CALIBRATION',framework_hz=hz,seconds=out['raw_gpu_cycles']/hz)
  out['formal_allowed']=formal_gate(out,gate,True);out['publish_performance']=bool(real and not prefix and out['R2']['status']==out['ORT']['status']=='PASS')
 except (OSError,ValueError,TypeError,KeyError,struct.error,OverflowError) as e:out.update(integrity='FAIL',formal_allowed=False,publish_performance=False,stop_reason=str(e))
 out['T_audit_ns']=time.monotonic_ns()-start;return out

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--references',type=Path,required=True);p.add_argument('--gate',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();refs=read(a.references)
 for row in [refs[k] for k in ['original_acceptance','original_R2','original_ORT_FP32']]+list(refs['v5'].values()):
  if not Path(row['path']).is_absolute():row['path']=str(a.references.resolve().parent/row['path'])
 refs['gate']=a.gate;r=audit_hybrid(a.run,a.release,refs)
 with a.out.open('x',encoding='utf-8') as f:json.dump(r,f,ensure_ascii=False,indent=2,allow_nan=False);f.write('\n')
 raise SystemExit(0 if r['integrity']=='PASS' else 1)

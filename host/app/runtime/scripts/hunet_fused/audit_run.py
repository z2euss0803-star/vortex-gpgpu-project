"""Independent compact-receipt and frozen-tensor audit. Never executes a model."""
import argparse,hashlib,importlib.util,json,math,pathlib,re,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
old=load('fused_frozen_numeric',ROOT/'scripts/hunet/audit_run.py')
check=old.check;digest=old.digest;owned_file=old.owned_file
def read(p):return json.loads(pathlib.Path(p).read_bytes())
def I(b,o,n=1):return struct.unpack_from('<%dI'%n,b,o)
def Q(b,o,n=1):return struct.unpack_from('<%dQ'%n,b,o)
def header(b,magic,n):check(len(b)==n and I(b,0,4)==(magic,1,n,0),'HEADER')
def finite(b):check(len(b)%4==0 and all(math.isfinite(x[0]) for x in struct.iter_unpack('<f',b)),'NONFINITE')
def bitwise(actual,expected):
 check(len(actual)==len(expected) and len(actual)%4==0,'TENSOR_SIZE');finite(actual);finite(expected)
 result=dict(status='PASS' if actual==expected else 'FAIL',actual_sha256=digest(actual),expected_sha256=digest(expected))
 if actual!=expected:
  index=next(i for i,(a,b) in enumerate(zip(struct.iter_unpack('<I',actual),struct.iter_unpack('<I',expected))) if a!=b)
  result['first_difference']=dict(index=index,expected_bits='0x%08x'%I(expected,index*4)[0],actual_bits='0x%08x'%I(actual,index*4)[0])
 return result
def expected_meta(args):
 j=I(args,0,32)
 return (j[4],j[5],j[6],j[3],j[28],j[16],j[17]) if j[3]==15 else (j[4],j[4],j[30],j[3],0,0,0)
def gpu_record(raw,args,rid,generation):
 check(len(raw)==448 and raw[:128]==args and I(raw,128,7)==expected_meta(args) and not any(raw[156:160]),'GPU_ARGS_META_PADDING')
 first,last,stage,op,trace,pack,tiles=expected_meta(args);e=raw[160:416];header(e,0x454a4c48,256)
 check(e[16:32]==rid and I(e,64,4)==(first,op,stage,9),'GPU_RUN_ROLE')
 job,submit,done,ack=Q(e,32,4);status,errors,pw,pr,before,after,watchdog,elapsed=I(e,80,8)
 check(job>0 and status&2 and not status&4 and errors==pw==pr==0 and after==(before+1)&0xffffffff and watchdog==4000000000,'GPU_COMPLETION')
 ready,idle,cdma,valid=I(e,112,4)
 check(ready==idle==1 and not cdma&0x4770 and valid&27==27 and not valid&~31,'GPU_IDLE_VALID')
 check(I(e,128,4)==(0,0,0,0) and I(e,144,4)==(3,0,generation,0) and I(e,208)[0]==3 and not any(e[228:]),'GPU_EXIT_GENERATION_GUARDS')
 check(e[160:192].hex()==digest(args),'GPU_ARGS_SHA');deadline,wait=Q(e,192,2)
 check(submit<=done<=ack and deadline==submit+60000000000 and done<=deadline and wait<=60000,'GPU_DEADLINE')
 active=min((trace+3)//4,4)
 expected=tuple(trace//active+(c<trace%active) if c<active else 0 for c in range(4)) if active else (0,0,0,0)
 check(I(e,212,4)==expected and bool(valid&4)==bool(trace),'GPU_OWNER')
 a,b,c,capture=Q(raw,416,4);check(capture==0,'GPU_CAPTURE_NOT_USED')
 return dict(first=first,last=last,stage=stage,op=op,job_id=job,jobs_before=before,jobs_after=after,submit_ns=submit,done_ns=done,ack_ns=ack,elapsed_cycles=elapsed,T_submit_wait_ns=a,T_evidence_ns=b,T_reap_ns=c)
def cpu_record(raw,s,rid,generation,counter,logical_sha,payload,diag):
 check(len(raw)==712,'CPU_RECORD_SIZE');b=raw[:128];q=raw[128:256];e=raw[256:640]
 header(b,0x42435948,128);header(q,0x43435948,128);header(e,0x45435948,384)
 seq,stage,n=s['sequence'],s['stage'],s['elements']*4;token=Q(b,48)[0]
 check(token>0 and b[16:32]==q[16:32]==e[32:48]==rid and I(b,32,4)==(generation,seq,stage,1) and I(q,32,4)==(generation,seq,stage,1) and I(e,16,4)==(1,generation,seq,stage),'CPU_RUN_ROLE')
 check(Q(q,48)[0]==Q(e,48)[0]==token and Q(b,64,2)==Q(e,64,2)==(n,n) and Q(q,64)[0]==n,'CPU_TOKEN_BOUNDS')
 check(I(q,112)[0]==4 and I(e,56,2)==(1,4) and not any(b[112:]) and not any(q[116:]) and not any(e[256:]),'CPU_CONTRACT_RESERVED')
 check(e[144:176].hex()==logical_sha and I(e,216,2)==(counter,counter) and not I(e,224)[0]&0x4770 and I(e,228,3)==(2,0,63),'CPU_GPU_IDLE_CHECKS')
 a,z,d,threads,reserved=struct.unpack_from('<3Q2I',raw,640);read_ns,write_ns,guard_ns,total,user=Q(e,176,5)
 check(a<=z and z-a==d==user==Q(q,72)[0] and threads==4 and reserved==0 and d>0 and total>=read_ns+write_ns+guard_ns+user and total<=60000000000 and Q(e,248)[0]>=total,'CPU_TIMING')
 begin,commit,ev,wb,ref=Q(raw,672,5)
 check((ref>0)==diag and (wb>0)==diag,'CPU_TIMING_MODE')
 check(b[80:112]==e[80:112] and q[80:112]==e[112:144],'CPU_HASH_CHAIN')
 if diag:
  prefix='cpu/%03u-'%stage
  check(payload.get(prefix+'begin.bin')==b,'CPU_BEGIN_CAPTURE')
  x=payload.get(prefix+'input.f32',b'');y=payload.get(prefix+'output.f32',b'')
  check(len(x)==len(y)==n and digest(x)==e[80:112].hex() and digest(y)==e[112:144].hex(),'CPU_LIVE_HASH');finite(x);finite(y)
  check(payload.get(prefix+'writeback.f32')==payload.get(prefix+'reference.f32')==y,'CPU_REFERENCE_WRITEBACK')
 return dict(sequence=seq,stage=stage,token=token,elements=s['elements'],input_sha256=e[80:112].hex(),output_sha256=e[112:144].hex(),read_ns=read_ns,write_ns=write_ns,guard_ns=guard_ns,total_ns=total,user_cpu_ns=user,T_begin_call_ns=begin,T_compute_ns=d,T_commit_call_ns=commit,T_evidence_call_ns=ev,T_writeback_capture_ns=wb,T_cpu_reference_ns=ref)
BASE_RUN='a08373c0ad524f2584283d4623293703'
BASE_MANIFEST='5b435e7a09fdd3e38f201c5811b6fc84d0d34008513cc432a7fd2c2f55f1759e'
def historical_baseline(path):
 path=pathlib.Path(path);manifest=(path/'run.json').read_bytes();check(digest(manifest)==BASE_MANIFEST,'FROZEN_BASELINE_MANIFEST')
 m=json.loads(manifest);check(m['run_id']==BASE_RUN and m['logical_completed']==1023,'FROZEN_BASELINE_IDENTITY')
 stages={s['stage_id']:owned_file(path,'stages/%03u.f32'%s['stage_id'],m['files']['stages/%03u.f32'%s['stage_id']]) for s in read(ROOT/'generated/hunet/catalog.json')['stages']}
 cpu={s['stage']: {kind:owned_file(path,'cpu/%03u-%s.f32'%(s['stage'],kind),m['files']['cpu/%03u-%s.f32'%(s['stage'],kind)]) for kind in ['input','output','writeback']} for s in read(ROOT/'generated/hunet_fused/schedule.json')['cpu']}
 return dict(stages=stages,cpu=cpu,logits=owned_file(path,'logits.f32',m['files']['logits.f32']),manifest_sha256=BASE_MANIFEST,run_id=BASE_RUN)
def frozen_numeric(logits):
 from package_release import frozen_references
 contract=read(ROOT/'contracts/hunet_v5.json');refs=frozen_references(contract)
 for value in [refs[k] for k in ('original_acceptance','original_R2','original_ORT_FP32')]+list(refs['v5'].values()):
  matches=[f for f in contract['files'] if f['sha256']==value['sha256'] and f['bytes']==value['bytes']]
  check(bool(matches),'REFERENCE_SOURCE_MISSING');value['path']=str(ROOT.parent.parent/matches[0]['path'])
 return old.numeric(logits,0,refs)
def formal_eligible(execution,baseline_equal,numeric,authorized):
 return bool(execution and baseline_equal and authorized and numeric.get('R2_numeric',{}).get('status')=='PASS' and numeric.get('ORT_application_numeric',{}).get('status')=='PASS')
def audit_run(run,release,baseline):
 out=dict(scope='UNVERIFIED',execution='FAIL',coverage='NOT_EVALUATED',baseline_equal='NOT_EVALUATED',r2=dict(status='NOT_EVALUATED'),ort=dict(status='NOT_EVALUATED'),formal_eligible=False,h1='NOT_RUN',publish_performance=False)
 try:
  run,release=pathlib.Path(run),pathlib.Path(release);m=read(run/'run.json');gate=read(run.parent/'readonly-preflight.json');r=read(release/'release-fused.json')
  check(gate.get('integrity')=='PASS' and gate.get('scope')=='HW4_FUSED_READONLY_PREFLIGHT','READONLY_GATE_REQUIRED')
  # This import is deliberately late: a missing release auditor can never admit a real run.
  package=load('fused_release_auditor',ROOT/'scripts/hunet_fused/package_release.py')
  verified=package.verify_fused_release(release);check(verified.get('integrity')=='PASS' and verified.get('scope')=='HUNET_FUSED_OFFLINE_RELEASE','RELEASE_AUDIT')
  check(m.get('scope')=='HUNET_FUSED_LINUX_V1' and m.get('profile')==4 and m.get('program_id')==9 and m.get('error')==0,'RAW_SCOPE_OR_ERROR')
  check(m.get('mode') in ('stage0','diagnostic','measured'),'MODE');diag=m['mode']!='measured';prefix=m['mode']=='stage0'
  for k in ('boot_id','build_id','kernel'):check(m.get(k) and m[k]==gate.get(k),'STALE_EPOCH:'+k)
  for k in ('source_digest','binding_sha256','bit_sha256','xsa_sha256'):check(r.get(k) and gate.get(k)==r[k],'PREFLIGHT_RELEASE:'+k)
  check(gate.get('expected_run_id')==m.get('run_id') and re.fullmatch('[0-9a-f]{32}',m['run_id']) and int(m['run_id'],16)>0,'RUN_ID')
  check(type(m.get('generation')) is int and m['generation']>=gate.get('minimum_generation',1) and m['roi_index']==0,'GENERATION_ROI')
  check(type(m.get('sample_index')) is int and m.get('sample_kind') in (1,2,3,4) and 0<=m['sample_index']<{1:1,2:3,3:20,4:256}[m['sample_kind']],'SAMPLE')
  check((diag and m['sample_kind']==4) or (not diag and m['sample_kind']!=4),'SAMPLE_MODE')
  payload={n:owned_file(run,n,v) for n,v in m['files'].items()}
  check({p.relative_to(run).as_posix() for p in run.rglob('*') if p.is_file() or p.is_symlink()}==set(payload)|{'run.json'},'EXACT_FILES')
  binding=payload.get('binding-fused.bin',b'');canonical=(release/'binding-fused.bin').read_bytes()
  # CLI captures the live binding immediately after open: VXL_READY=1.
  # Only the canonical release has zero generation/state; neither is optional.
  check(len(binding)==len(canonical)==512 and I(binding,0,8)==(0x44424c48,3,512,4,9,15,m['generation'],1) and I(canonical,24,2)==(0,0) and binding[32:]==canonical[32:] and digest(canonical)==r['binding_sha256'],'BINDING_V3')
  runtime=json.loads(payload['cpu-runtime.json']);expected=read(release/'cpu/runtime.json')
  check(all(runtime.get(k)==expected.get(k) for k in ('compiler','algorithm','threads','precision_flags','libm_bytes','libm_sha256')),'CPU_RUNTIME')
  rid=bytes.fromhex(m['run_id']);begin=payload['begin.bin'];header(begin,0x47424c48,128)
  check(begin[16:32]==rid and I(begin,32,4)==(0,m['sample_kind'],m['sample_index'],0) and Q(begin,56)[0]==196608 and not any(begin[96:]),'BEGIN')
  input_sha=read(ROOT/'contracts/hunet_inputs.json')['records'][0]['sha256'];check(begin[64:96].hex()==input_sha,'INPUT_SHA')
  jobs=(release/'jobs.bin').read_bytes();check(len(jobs)==103*128 and digest(jobs)==binding[160:192].hex(),'JOBS')
  wanted=[jobs[i:i+128] for i in range(0,len(jobs),128)];wanted=wanted[:1] if prefix else wanted
  gb=payload['gpu-records.bin'];cb=payload['cpu-records.bin'];schedule=read(ROOT/'generated/hunet_fused/schedule.json');cpu_roles=[] if prefix else schedule['cpu']
  check(m['gpu_record_bytes']==448 and m['cpu_record_bytes']==712 and len(gb)==len(wanted)*448 and len(cb)==len(cpu_roles)*712 and m['gpu_records']==len(wanted) and m['cpu_records']==len(cpu_roles),'RECORD_COUNTS')
  gpu=[gpu_record(gb[i*448:(i+1)*448],a,rid,m['generation']) for i,a in enumerate(wanted)]
  for previous,current in zip(gpu,gpu[1:]):check(current['job_id']==previous['job_id']+1 and current['jobs_before']==previous['jobs_after'] and current['submit_ns']>=previous['ack_ns'],'GPU_CHAIN')
  sequence=1;gi=ci=0;cpu=[];tokens=set();logical_sha=schedule['logical_sha256']
  while sequence<=(96 if prefix else 1023):
   if ci<len(cpu_roles) and sequence==cpu_roles[ci]['sequence']:
    c=cpu_record(cb[ci*712:(ci+1)*712],cpu_roles[ci],rid,m['generation'],gpu[gi-1]['jobs_after'],logical_sha,payload,diag)
    check(c['token'] not in tokens,'CPU_TOKEN_REUSE');tokens.add(c['token']);cpu.append(c);ci+=1;sequence+=1
   else:
    check(gi<len(gpu) and gpu[gi]['first']==sequence,'LOGICAL_RANGE_GAP');sequence=gpu[gi]['last']+1;gi+=1
  check(gi==len(gpu) and ci==len(cpu_roles),'LOGICAL_COVERAGE')
  stages=read(ROOT/'generated/hunet/catalog.json')['stages'];stages=stages[:1] if prefix else stages
  check(m['logical_completed']==(96 if prefix else 1023) and m['stage_count']==len(stages) and m['captured_stages']==(len(stages) if diag else 0),'STAGE_COUNTS')
  expected_names={'binding-fused.bin','cpu-runtime.json','begin.bin','gpu-records.bin','cpu-records.bin','timing.json'}
  comparisons=[];base=historical_baseline(baseline)
  if diag:
   for s in stages:
    name='stages/%03u.f32'%s['stage_id'];expected_names.add(name);check(len(payload.get(name,b''))==s['bytes'],'STAGE_BYTES')
    comparisons.append(dict(stage=s['stage_id'],gpu_address=s['gpu'],bytes=s['bytes'],**bitwise(payload[name],base['stages'][s['stage_id']])))
   for c in cpu_roles:
    for kind in ('begin.bin','input.f32','output.f32','reference.f32','writeback.f32'):expected_names.add('cpu/%03u-%s'%(c['stage'],kind))
    for kind in ('input','output','writeback'):
     comparisons.append(dict(cpu_stage=c['stage'],kind=kind,**bitwise(payload['cpu/%03u-%s.f32'%(c['stage'],kind)],base['cpu'][c['stage']][kind])))
  if prefix:check(m['state']=='PREFIX_STOP' and 'end.bin' not in payload,'PREFIX_STATE')
  else:
   expected_names|={'input.f32','logits.f32','end.bin'};check(m['state']=='RAW_COMPLETE_REQUIRES_AUDIT','FULL_STATE')
   end=payload['end.bin'];header(end,0x4e455948,256)
   check(end[16:32]==rid and I(end,32,8)==(0,m['generation'],1023,103,13,100,1,31) and Q(end,64)[0]==gpu[-1]['job_id'] and I(end,72,2)==(gpu[0]['jobs_before'],gpu[-1]['jobs_after']) and not any(end[176:]),'END_COMPLETION')
   check(end[80:112].hex()==digest(payload['input.f32'])==input_sha and end[112:144].hex()==digest(payload['logits.f32']) and end[144:176]==bytes.fromhex(r['binding_sha256']),'END_HASH')
   comparisons.append(dict(kind='logits',**bitwise(payload['logits.f32'],base['logits'])))
  check(set(payload)==expected_names,'EXACT_SEMANTIC_CAPTURE')
  timing=json.loads(payload['timing.json']);check(len(timing['gpu'])==len(gpu) and len(timing['cpu'])==len(cpu),'TIMING_COVERAGE')
  check(set(timing) in ({'gpu','cpu'},{'gpu','cpu','handoff_v1'}),'TIMING_EXTENSION_SCHEMA')
  for row,g in zip(timing['gpu'],gpu):check(row=={k:g[v] for k,v in dict(first='first',last='last',stage='stage',op='op',elapsed_raw='elapsed_cycles',T_submit_wait_ns='T_submit_wait_ns',T_evidence_ns='T_evidence_ns',T_reap_ns='T_reap_ns').items()},'GPU_TIMING_DUPLICATE')
  for row,c in zip(timing['cpu'],cpu):check(row=={k:c[k] for k in ('sequence','stage','T_begin_call_ns','T_compute_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns')},'CPU_TIMING_DUPLICATE')
  timing_module=load('fused_independent_summary',ROOT/'scripts/hunet_fused/summary.py')
  if 'handoff_v1' in timing:m=dict(m,timing_extension=timing['handoff_v1'])
  summary=timing_module.timing_summary(gpu,cpu,m,96969696)
  out.update(scope='HW4_FUSED_RAW_AUDIT',execution='PASS',coverage='PREFIX_ONLY' if prefix else 'PASS',mode=m['mode'],gpu=gpu,cpu=cpu,stages=comparisons,timing=summary,baseline_manifest_sha256=base['manifest_sha256'],boot_id=m['boot_id'],binding_sha256=r['binding_sha256'],source_digest=r['source_digest'],bit_sha256=r['bit_sha256'],xsa_sha256=r['xsa_sha256'])
  out['baseline_equal']='PASS' if diag and all(x['status']=='PASS' for x in comparisons) else 'FAIL' if diag else 'NOT_CAPTURED'
  if not prefix:
   numeric=frozen_numeric(payload['logits.f32']);out['r2']=numeric['R2_numeric'];out['ort']=numeric['ORT_application_numeric']
   out['formal_eligible']=formal_eligible(True,out['baseline_equal']=='PASS',numeric,gate.get('formal_authorized') is True)
   if not diag:
    approved=gate.get('diagnostic_audit',{});check(approved.get('formal_eligible') is True and approved.get('execution')=='PASS' and approved.get('coverage')=='PASS' and approved.get('mode')=='diagnostic' and approved.get('baseline_equal')=='PASS' and approved.get('r2',{}).get('status')==approved.get('ort',{}).get('status')=='PASS' and approved.get('binding_sha256')==r['binding_sha256'] and approved.get('boot_id')==m['boot_id'],'FORMAL_DIAGNOSTIC_GATE')
    out['formal_eligible']=formal_eligible(True,True,numeric,gate.get('formal_authorized') is True)
  if out['baseline_equal']=='FAIL' or out['r2']['status']=='FAIL' or out['ort']['status']=='FAIL':out['stop_reason']='NUMERIC_FAIL_RETAINED_NO_FORMAL'
 except (ValueError,KeyError,TypeError,OSError,struct.error,ImportError) as e:out.update(execution='FAIL',reason=str(e),formal_eligible=False,publish_performance=False)
 return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--run',type=pathlib.Path,required=True);p.add_argument('--release',type=pathlib.Path,required=True);p.add_argument('--baseline',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
 r=audit_run(a.run,a.release,a.baseline)
 with a.out.open('x',encoding='utf-8') as f:json.dump(r,f,indent=2);f.write('\n')
 raise SystemExit(0 if r['execution']=='PASS' and r['r2']['status']=='PASS' and r['ort']['status']=='PASS' and (r['baseline_equal']=='PASS' or (r.get('mode')=='measured' and r['formal_eligible'])) else 1)

"""Evidence integrity and frozen numeric comparisons; never execute a model."""
import argparse,hashlib,json,math,re,struct,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def digest(b):return hashlib.sha256(b).hexdigest()
def read_json(p):return json.loads(Path(p).read_text(encoding='utf-8'))
def checked_reference(record):
 p=Path(record['path']);b=p.read_bytes()
 if len(b)!=record['bytes'] or digest(b)!=record['sha256']:raise ValueError('REFERENCE_IDENTITY:'+str(p))
 return b
def read_npy_row(path:Path,roi:int):
 b=Path(path).read_bytes()
 if b[:6]!=b'\x93NUMPY' or len(b)<10:raise ValueError('NPY_MAGIC')
 version=tuple(b[6:8])
 if version==(1,0):n=struct.unpack_from('<H',b,8)[0];offset=10
 elif version in [(2,0),(3,0)]:n=struct.unpack_from('<I',b,8)[0];offset=12
 else:raise ValueError('NPY_VERSION')
 if n>65535 or offset+n>len(b):raise ValueError('NPY_HEADER_TRUNCATED')
 # Strict grammar for the frozen numeric arrays. No Python evaluation or pickle.
 header=b[offset:offset+n].decode('utf-8').strip()
 m=re.fullmatch(r"\{\s*'descr':\s*'(<f[48])',\s*'fortran_order':\s*False,\s*'shape':\s*\(\s*(\d+)\s*,\s*(\d+)\s*,?\s*\),\s*\}",header)
 if not m:raise ValueError('NPY_UNSUPPORTED_HEADER')
 dtype,rows,cols=m.group(1),int(m.group(2)),int(m.group(3));size=int(dtype[-1]);offset+=n
 if (rows,cols)!=(127,8105) or not 0<=roi<rows or len(b)!=offset+rows*cols*size:raise ValueError('NPY_SHAPE_OR_SIZE')
 values=list(struct.unpack_from('<%d%s'%(cols,'f' if size==4 else 'd'),b,offset+roi*cols*size))
 if not all(math.isfinite(x) for x in values):raise ValueError('REFERENCE_NONFINITE')
 return values
def fp32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def compare(a,b,contract):
 if len(a)!=8105 or len(b)!=8105 or not all(math.isfinite(x) for x in a+b):raise ValueError('LOGITS_SHAPE_OR_NONFINITE')
 errors=[x-y for x,y in zip(a,b)];atol=contract['atol'];rtol=contract['rtol'];limit=contract['nrmse_limit']
 # Original NumPy threshold array is FP32; delta and RMSE are FP64.
 count=sum(abs(e)>fp32(fp32(atol)+fp32(fp32(rtol)*abs(y))) for e,y in zip(errors,b))
 rmse=math.sqrt(math.fsum(x*x for x in errors)/8105);scale=math.sqrt(math.fsum(x*x for x in b)/8105);nrmse=rmse/max(scale,1e-12)
 return dict(status='PASS' if not count and nrmse<=limit else 'FAIL',mismatch_count=count,max_abs=max(map(abs,errors)),rmse=rmse,nrmse=nrmse,atol=atol,rtol=rtol,nrmse_limit=limit)
def checked_references(references,identity=None):
 from package_release import frozen_references
 expected=identity if identity is not None else frozen_references(read_json(ROOT/'contracts/hunet_v5.json'))
 if set(references.get('v5',{}))!=set(expected['v5']):raise ValueError('FROZEN_REFERENCE_ROIS')
 pairs=[(references.get(k),expected[k]) for k in ['original_acceptance','original_R2','original_ORT_FP32']]
 pairs += [(references['v5'][k],v) for k,v in expected['v5'].items()]
 for actual,want in pairs:
  if not isinstance(actual,dict) or any(actual.get(k)!=want[k] for k in ['bytes','sha256']):raise ValueError('FROZEN_REFERENCE_IDENTITY:'+want['path'])
  checked_reference(actual) # Alternate directories may locate only identical bytes.

def numeric(logits:bytes,roi:int,references:dict,identity=None):
 checked_references(references,identity)
 if len(logits)!=32420:raise ValueError('FULL_LOGITS_REQUIRED')
 a=list(struct.unpack('<8105f',logits));accept=json.loads(checked_reference(references['original_acceptance']).decode('utf-8'))
 same=references.get('v5',{}).get(str(roi))
 exact=dict(status='NO_FROZEN_REFERENCE') if same is None else dict(status='PASS' if logits==checked_reference(same) else 'FAIL',reference_scope='BAREMETAL_REFERENCE')
 for role in ['original_R2','original_ORT_FP32']:checked_reference(references[role])
 r2=read_npy_row(Path(references['original_R2']['path']),roi);ort=read_npy_row(Path(references['original_ORT_FP32']['path']),roi)
 return {'Linux-v5_same_math':exact,'R2_numeric':compare(a,r2,accept['device_logits_R2']),'ORT_application_numeric':compare(a,ort,accept['R1_R0'])}
def owned_file(root,name,record):
 if not isinstance(name,str) or '\\' in name or not name or name.startswith('/') or any(x in ['.','..',''] for x in name.split('/')):raise ValueError('PATH_ESCAPE')
 p=root
 for part in name.split('/'):
  p=p/part
  if p.is_symlink():raise ValueError('SYMLINK_ESCAPE')
 if not p.is_file() or p.stat().st_size!=record['bytes']:raise ValueError('FILE_SIZE:'+name)
 b=p.read_bytes()
 if digest(b)!=record.get('sha256'):raise ValueError('FILE_SHA256:'+name)
 return b
def owner_trace(b,tiles):
 if len(b)!=tiles*4 or not 1<=tiles<=32768:raise ValueError('OWNER_TRACE_SIZE')
 active=min((tiles+3)//4,4);values=list(struct.unpack('<%dI'%tiles,b));expected=[]
 for core in range(active):expected += [core+1]*(tiles//active+(core<tiles%active))
 if values!=expected:raise ValueError('OWNER_TRACE_CONTENT')
def check(condition,reason):
 if not condition:raise ValueError(reason)
def audit_run(run_dir:Path,release_dir:Path,references:dict)->dict:
 begin=time.monotonic_ns();out=dict(integrity='FAIL',scope='UNVERIFIED',publish_performance=False,gpu_seconds=None,**{k:dict(status='NOT_EVALUATED') for k in ['Linux-v5_same_math','R2_numeric','ORT_application_numeric']})
 try:
  run=Path(run_dir);rel=Path(release_dir);m=read_json(run/'run.json');r=read_json(rel/'release.json');gate=references.get('gate')
  if isinstance(gate,(str,Path)):gate=read_json(gate)
  check(isinstance(gate,dict) and gate.get('integrity')=='PASS','READONLY_BOARD_GATE_REQUIRED')
  check(gate.get('scope') in ['HW4_READONLY_PREFLIGHT','TEST_PROTOCOL_NOT_BOARD'],'GATE_SCOPE')
  out['scope']=gate['scope']
  reference_identity=None
  if gate['scope']=='HW4_READONLY_PREFLIGHT':
   from package_release import verify_release
   verified=verify_release(rel);check(verified['integrity']=='PASS','RELEASE_INTEGRITY');reference_identity=verified['reference_identity']
  check(m.get('scope')=='HUNET_LINUX_V1' and m.get('transport')=='SCP_CAPTURE','RAW_SCOPE')
  check(m.get('profile')==4 and r.get('profile')==4 and m.get('mode') in ['resident','diagnostic'],'HW4_PROFILE_MODE')
  for key in ['boot_id','build_id','kernel','profile']:check(m.get(key)==gate.get(key),'STALE_EPOCH:'+key)
  for key in ['source_digest','bit_sha256','xsa_sha256']:check(r.get(key)==gate.get(key),'SOURCE_BOOT_BINDING:'+key)
  check(type(m.get('generation')) is int and m['generation']>=gate.get('minimum_generation',1),'GENERATION_FLOOR')
  check(re.fullmatch('[0-9a-f]{32}',m.get('run_id','')) is not None and int(m['run_id'],16)>0,'RUN_ID')
  rid=bytes.fromhex(m['run_id']);roi=m['roi_index'];check(type(roi) is int and 0<=roi<127,'ROI_INDEX')
  inputs=read_json(ROOT/'contracts/hunet_inputs.json')['records'];check(inputs[roi]['included'],'EXCLUDED_ROI')
  check(m['sample_kind'] in [1,2,3,4] and type(m['sample_index']) is int,'SAMPLE_KIND_INDEX')
  limits={1:1,2:3,3:20,4:256};check(0<=m['sample_index']<limits[m['sample_kind']],'SAMPLE_INDEX')
  payload={n:owned_file(run,n,v) for n,v in m['files'].items()}
  required={'binding.bin':256,'begin.bin':128,'end.bin':128,'job_args.bin':130944,'job_evidence.bin':261888,'input.f32':196608,'logits.f32':32420,'guards.bin':13568,'last_trace.u32':4056}
  for name,size in required.items():check(len(payload.get(name,b''))==size,'FULL_RAW_REQUIRED:'+name)
  binding=payload['binding.bin'];check(struct.unpack_from('<6I',binding)==(0x44424c48,1,256,4,6,3),'BINDING_HEADER');check(struct.unpack_from('<I',binding,24)[0]==m['generation'],'BINDING_GENERATION')
  check(binding[32:]==(rel/'binding.bin').read_bytes()[32:],'BINDING_RELEASE');check(binding[96:128].hex()==r['source_digest'],'SOURCE_DIGEST')
  check(digest(binding[32:])==r['binding_sha256'],'BINDING_SHA256');check(binding[64:96].hex()==r['program_sha256'],'PROGRAM_SHA256')
  jobs=payload['job_args.bin'];check(jobs==(rel/'jobs.bin').read_bytes() and digest(jobs)==binding[160:192].hex(),'CATALOG_BYTES')
  check(digest((rel/'matrix_descriptors.bin').read_bytes())==binding[192:224].hex(),'AUX_IDENTITY')
  check(payload['guards.bin']==bytes([0xa5])*13568,'GUARDS');owner_trace(payload['last_trace.u32'],1014)
  check(digest(payload['input.f32'])==inputs[roi]['sha256'],'INPUT_IDENTITY')
  values=list(struct.unpack('<8105f',payload['logits.f32']));check(all(math.isfinite(x) for x in values),'FINITE_LOGITS')
  b=payload['begin.bin'];check(struct.unpack_from('<4I',b)==(0x47424c48,1,128,0) and b[16:32]==rid,'BEGIN_HEADER_RUN');check(struct.unpack_from('<4I',b,32)==(roi,m['sample_kind'],m['sample_index'],0),'BEGIN_SAMPLE');check(struct.unpack_from('<Q',b,56)[0]==196608 and b[64:96].hex()==digest(payload['input.f32']) and not any(b[96:]),'BEGIN_INPUT')
  ev=payload['job_evidence.bin'];first_id=None;previous_jobs=None;previous_ack=0;catalog=list(struct.iter_unpack('<32I',jobs))
  for index,j in enumerate(catalog):
   e=ev[index*256:(index+1)*256];seq=index+1;check(struct.unpack_from('<4I',e)==(0x454a4c48,1,256,0) and e[16:32]==rid,'EVIDENCE_HEADER_RUN:%d'%seq)
   job,submit,done,ack=struct.unpack_from('<4Q',e,32)
   if first_id is None:first_id=job
   check(job==first_id+index and job>0,'JOB_ID:%d'%seq)
   check(struct.unpack_from('<4I',e,64)==(seq,j[3],j[30],6),'SEQUENCE_OP_STAGE_PROGRAM:%d'%seq)
   status,errors,pw,pr,before,after,watchdog,elapsed=struct.unpack_from('<8I',e,80)
   check(status&2 and not status&4 and errors==pw==pr==0 and after==(before+1)&0xffffffff and watchdog==4000000000,'HARDWARE_COMPLETION:%d'%seq)
   check(previous_jobs is None or before==previous_jobs,'HARDWARE_COUNTER_CHAIN:%d'%seq);previous_jobs=after
   ready,idle,cdma,valid=struct.unpack_from('<4I',e,112);check(ready==idle==1 and cdma&0x770==0 and valid&27==27 and valid&~31==0,'READY_IDLE_CDMA_VALID:%d'%seq)
   check(struct.unpack_from('<4I',e,128)==(0,0,0,0),'CORE_EXIT:%d'%seq);check(struct.unpack_from('<4I',e,144)==(3,0,m['generation'],0),'SOFTWARE_GENERATION:%d'%seq)
   check(e[160:192].hex()==digest(jobs[index*128:(index+1)*128]),'ARGS_SHA256:%d'%seq)
   deadline,wait=struct.unpack_from('<2Q',e,192);check(previous_ack<=submit<=done<=ack and deadline==submit+60000000000 and done<=deadline and wait<=60000,'MONOTONIC_KERNEL_TIMING:%d'%seq);previous_ack=ack
   check(struct.unpack_from('<I',e,208)[0]==3 and not any(e[228:]),'CRT_GUARDS_RESERVED:%d'%seq)
   if valid&4:check(j[3]==2 and sum(struct.unpack_from('<4I',e,212))==j[7],'OWNER_COUNTS:%d'%seq)
   else:check(not any(e[212:228]),'UNVALIDATED_OWNER_COUNTS:%d'%seq)
  end=payload['end.bin'];check(struct.unpack_from('<4I',end)==(0x4e454c48,1,128,0) and end[16:32]==rid,'END_HEADER_RUN');check(struct.unpack_from('<4I',end,32)==(roi,1023,m['generation'],31),'END_COMPLETE_CHECKS');check(struct.unpack_from('<Q2I',end,48)==(first_id+1022,previous_jobs,1),'END_JOB_COUNTER_IDLE');check(end[64:96].hex()==digest(payload['logits.f32']) and end[96:128].hex()==digest(payload['input.f32']),'END_OUTPUT_INPUT_SHA')
  if m['mode']=='diagnostic':
   report=read_json(ROOT/'generated/hunet/catalog.json')
   stage_names=set()
   for s in report['stages']:
    name='stages/%03d.f32'%s['stage_id'];stage_names.add(name);raw=payload.get(name,b'');check(len(raw)==s['bytes'],'STAGE_COVERAGE:'+name);check(all(math.isfinite(x[0]) for x in struct.iter_unpack('<f',raw)),'STAGE_FINITE:'+name)
   trace_names=set()
   for j in catalog:
    if j[3]==2:
     name='traces/%04d.u32'%j[4];trace_names.add(name);owner_trace(payload.get(name,b''),j[7])
   check(len(stage_names)==100 and len(trace_names)==314 and {n for n in payload if n.startswith('stages/')}==stage_names and {n for n in payload if n.startswith('traces/')}==trace_names,'DIAGNOSTIC_EXACT_COVERAGE')
  else:check(not any(n.startswith(('stages/','traces/')) for n in payload),'RESIDENT_EXTRA_LAYER_CAPTURE')
  for key in ['T_load_ns','T_resident_ns','T_capture_ns','T_diagnostic_capture_ns']:check(type(m.get(key)) is int and 0<=m[key]<2**64,'USER_TIMING:'+key)
  check(m['T_resident_ns']<=180000000000,'WHOLE_GRAPH_MONITOR_LIMIT')
  check(len(m.get('T_submit_wait_ns',[]))==1023 and all(type(n) is int and 0<=n<2**64 for n in m['T_submit_wait_ns']),'SUBMIT_WAIT_TIMING');check(sum(m['T_submit_wait_ns'])<=m['T_resident_ns'],'RESIDENT_TIMING_SCOPE')
  out.update(numeric(payload['logits.f32'],roi,references,reference_identity));out.update({k:m[k] for k in ['boot_id','build_id','kernel','generation','run_id','roi_index','sample_kind','sample_index','mode','T_load_ns','T_resident_ns','T_submit_wait_ns','T_capture_ns','T_diagnostic_capture_ns','T_transfer_ns']});out['source_digest']=r['source_digest'];out['integrity']='PASS';out['binding_sha256']=r['binding_sha256'];out['completed_jobs']=1023
 except (ValueError,KeyError,TypeError,OSError,struct.error,OverflowError) as e:out['reason']=str(e)
 out['T_audit_ns']=time.monotonic_ns()-begin;return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--references',type=Path,required=True);p.add_argument('--gate',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();refs=read_json(a.references);refs['gate']=a.gate;result=audit_run(a.run,a.release,refs);a.out.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8');raise SystemExit(0 if result['integrity']=='PASS' else 1)

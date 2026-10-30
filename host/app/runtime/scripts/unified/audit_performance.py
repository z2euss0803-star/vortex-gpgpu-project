"""Offline strict event metadata audit; no hardware measurement or GPU speed claim."""
import argparse,collections,hashlib,json,os,pathlib,re,stat

CONTRACT=pathlib.Path(__file__).resolve().parents[2]/'contracts/unified/event-v1.json'
SCHEMA=json.loads(CONTRACT.read_text());U64=(1<<64)-1
DIGESTS=('hardware_sha','bit_sha','xsa_sha','compatible_sha','program_code_sha','program_schema_sha','model_sha','plan_sha','source_sha','input_sha')
POLICIES=('reuse_constants','batch_submit','weighted_dispatch','eliminate_redundant_layout')

def require(condition,message):
 if not condition:raise ValueError(message)

def unsigned(value,maximum=U64,minimum=0):
 require(type(value) is int and minimum<=value<=maximum,'bounded unsigned integer required')
 return value

def digest(value):
 require(type(value) is str and re.fullmatch('[0-9a-f]{64}',value) and value!='0'*64,'nonzero SHA256 identity required')

def identifier(value):
 require(type(value) is str and 0<len(value)<=128 and re.fullmatch('[A-Za-z0-9_.:-]+',value),'bounded identity required')

def validate(e):
 require(type(e) is dict and set(e)==set(SCHEMA['required']),'missing or unknown common event field')
 require(e['format']==SCHEMA['format'] and e['hardware_identity_source']==SCHEMA['hardware_identity_source'],'event format/identity source differs')
 for k in ('run_id','boot_id','client_id','request_id','operator_id','job_id','host'):identifier(e[k])
 for k in DIGESTS:digest(e[k])
 for k in ('bytes','buffer_generation','start_ns','end_ns'):unsigned(e[k])
 for k in ('event_id','device_generation'):unsigned(e[k],minimum=1)
 for k in ('namespace_id','profile_id','program_id'):unsigned(e[k],(1<<32)-1,1)
 require(type(e['app']) is int and type(e['provider_flags']) is int,'integer application/provider required')
 require((e['provider_flags']==1 and e['app'] in (1,3)) or (e['provider_flags']==2 and e['app']==2),'provider capability isolation')
 require(e['namespace_id']==e['app'],'application namespace differs')
 require(e['phase'] in SCHEMA['phase_names'] and e['clock_domain'] in SCHEMA['clock_domains'] and e['scope'] in SCHEMA['evidence_scopes'] and e['counter_kind'] in SCHEMA['counter_kinds'],'unknown phase/clock/scope/counter')
 require(e['end_ns']>=e['start_ns'],'reversed interval')
 if e['parent_event'] is not None:unsigned(e['parent_event'],minimum=1)
 require(type(e['status']) is int and -(1<<31)<=e['status']<(1<<31),'signed status required')
 if e['status']:
  x=e['execution_error'];require(type(x) is dict and set(x)=={'code','category','detail'},'original execution error required')
  require(type(x['code']) is int and x['code']==e['status'] and type(x['category']) is str and 0<len(x['category'])<=256 and type(x['detail']) is str and len(x['detail'])<=65536,'original status/category/detail differ')
 else:require(e['execution_error'] is None,'success cannot hide an execution error')
 require(type(e['raw_counter_bits']) is int and e['raw_counter_bits']==32 and type(e['cycles_valid']) is bool,'raw counter format differs')
 require(e['cycles_valid']==(e['gpu_raw_cycles'] is not None),'counter validity/null differs')
 if e['gpu_raw_cycles'] is not None:unsigned(e['gpu_raw_cycles'],(1<<32)-1)
 real=e['scope']=='REAL_BOARD'
 require(not real or e['clock_domain']=='LINUX_MONOTONIC','real counter cannot borrow a host clock')
 require(real or e['gpu_raw_cycles'] is None,'offline GPU counter must be null')
 if e['counter_kind']=='NONE':require(e['gpu_raw_cycles'] is None,'non-GPU event repeats a raw counter')
 elif real and e['counter_kind']=='ZERO_JOB_CACHE':require(e['cycles_valid'] and e['gpu_raw_cycles']==0,'real zero-job cache needs valid zero')
 elif real:
  require(e['status']!=0 or e['cycles_valid'],'successful GPU job missing counter')
  require(e['gpu_raw_cycles'] is None or e['gpu_raw_cycles']>0,'GPU job cannot forge cache zero')

def add(a,b):
 require(a<=U64-b,'u64 timing/counter total overflow')
 return a+b

def union_length(intervals):
 total=0;start=None;end=None
 for a,b in sorted(intervals):
  if start is None:start,end=a,b
  elif a<=end:end=max(end,b)
  else:total=add(total,end-start);start,end=a,b
 return add(total,end-start) if start is not None else 0

def summarize(rows,configured_frequency):
 index={};children=collections.defaultdict(list);roots=[];phases={};counter_jobs=set();counter_seen=False;counter_complete=True;raw_total=0;errors=[]
 immutable=DIGESTS[:4]+('model_sha','plan_sha','source_sha','input_sha','app','provider_flags','device_generation','scope')
 for e in rows:
  require(all(e[k]==rows[0][k] for k in immutable),'request source/provider/scope identity differs')
  require(e['event_id'] not in index,'duplicate event ID');index[e['event_id']]=e
 for e in rows:
  parent=e['parent_event']
  if parent is None:roots.append((e['start_ns'],e['end_ns']))
  else:
   require(parent in index,'missing or cross-domain parent');p=index[parent]
   require(p['start_ns']<=e['start_ns']<=e['end_ns']<=p['end_ns'],'child outside parent')
   children[parent].append((e['start_ns'],e['end_ns']))
  at=e;seen={e['event_id']}
  while at['parent_event'] is not None:
   p=at['parent_event'];require(p not in seen and p in index,'parent cycle/missing ancestor');seen.add(p);at=index[p]
  if e['counter_kind']!='NONE':
   require(e['job_id'] not in counter_jobs,'duplicate logical GPU job counter');counter_jobs.add(e['job_id']);counter_seen=True
   if e['scope']!='REAL_BOARD' or not e['cycles_valid']:counter_complete=False
   else:raw_total=add(raw_total,e['gpu_raw_cycles'])
  if e['status']:errors.append({k:e[k] for k in ('event_id','job_id','status','execution_error')})
 work=0
 for e in rows:
  span=e['end_ns']-e['start_ns'];nested=union_length(children[e['event_id']]);require(nested<=span,'nested union exceeds parent');exclusive=span-nested;work=add(work,exclusive)
  p=phases.setdefault(e['phase'],dict(inclusive_ns=0,exclusive_ns=0,count=0));p['inclusive_ns']=add(p['inclusive_ns'],span);p['exclusive_ns']=add(p['exclusive_ns'],exclusive);p['count']+=1
 complete=counter_seen and counter_complete;declared=raw_total if complete else None;estimate=None
 if configured_frequency is not None and declared is not None:
  n,d=configured_frequency;ns=declared*1000000000*d//n;unsigned(ns);estimate=dict(numerator_hz=n,denominator=d,ns_floor=ns,physical_measurement=False,condition='configured clock only; no independent physical frequency measurement')
 first=rows[0]
 return dict(host=first['host'],clock_domain=first['clock_domain'],run_id=first['run_id'],boot_id=first['boot_id'],client_id=first['client_id'],request_id=first['request_id'],source_scope=first['scope'],wall_ns=union_length(roots),exclusive_work_ns=work,work_can_overlap=True,phases=phases,declared_raw_counter_sum=declared,gpu_counter_complete=complete,counter_source='UNATTESTED_INPUT_METADATA_NOT_NEW_MEASUREMENT',configured_frequency_estimate=estimate,errors=errors)

def audit(events,configured_frequency=None):
 require(type(events) is list and len(events)<=SCHEMA['limits']['max_events'],'bounded list of events required')
 if configured_frequency is not None:
  require(isinstance(configured_frequency,(list,tuple)) and len(configured_frequency)==2,'rational configured frequency required')
  for n in configured_frequency:unsigned(n,minimum=1)
 groups=collections.defaultdict(list)
 for e in events:
  validate(e);key=tuple(e[k] for k in ('run_id','boot_id','client_id','request_id','host','clock_domain'));groups[key].append(e)
 return dict(scope='OFFLINE_EVENT_METADATA_AUDIT_NOT_GPU_OR_BOARD',gpu_raw_cycles=None,contract_sha256=hashlib.sha256(CONTRACT.read_bytes()).hexdigest(),groups=[summarize(groups[k],configured_frequency) for k in sorted(groups)],global_cross_domain_wall_ns=None)

def compare_candidates(baseline,candidate):
 require(type(baseline) is dict and type(candidate) is dict,'candidate receipts required')
 fields={'binding','policy','accepted_receipt_sha','copy_calls','copied_output_sha'}
 binding=set(('hardware_sha','bit_sha','xsa_sha','compatible_sha','program_code_sha','program_schema_sha','model_sha','plan_sha','input_sha','numeric_reference_sha'))
 for r in (baseline,candidate):
  require(set(r)==fields and type(r['binding']) is dict and set(r['binding'])==binding,'complete fixed comparison binding required')
  for value in r['binding'].values():digest(value)
  digest(r['accepted_receipt_sha']);digest(r['copied_output_sha']);unsigned(r['copy_calls'])
  require(type(r['policy']) is dict and set(r['policy'])==set(POLICIES) and all(type(x) is bool for x in r['policy'].values()),'exact four boolean policy switches required')
 require(not any(baseline['policy'].values()) and sum(candidate['policy'].values())==1,'baseline false and exactly one candidate switch required')
 require(baseline['binding']==candidate['binding'] and baseline['accepted_receipt_sha']==candidate['accepted_receipt_sha'] and baseline['copied_output_sha']==candidate['copied_output_sha'],'fixed hardware/program/input/numeric/output identity differs')
 return dict(scope='OFFLINE_SOFTWARE_COUNTER_COMPARISON_NOT_GPU_SPEEDUP',gpu_raw_cycles=None,gpu_speedup_claim=False,copy_call_reduction=baseline['copy_calls']-candidate['copy_calls'],switch=next(k for k in POLICIES if candidate['policy'][k]))

def strict_pairs(pairs):
 out={}
 for k,v in pairs:
  require(k not in out,'duplicate JSON key');out[k]=v
 return out

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('events');p.add_argument('--output');p.add_argument('--configured-hz-numerator',type=int);p.add_argument('--configured-hz-denominator',type=int,default=1);args=p.parse_args()
 limit=SCHEMA['limits']['max_file_bytes'];fd=os.open(args.events,os.O_RDONLY|getattr(os,'O_CLOEXEC',0)|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0))
 with os.fdopen(fd,'rb') as f:
  st=os.fstat(f.fileno());require(stat.S_ISREG(st.st_mode) and st.st_size<=limit,'bounded regular event file required');raw=f.read(limit+1);require(len(raw)<=limit,'event file grew beyond bound')
 rows=[]
 for line in raw.splitlines():
  if not line.strip():continue
  require(len(line)<=SCHEMA['limits']['max_record_bytes'],'event record too large');rows.append(json.loads(line.decode('utf8'),object_pairs_hook=strict_pairs,parse_constant=lambda value:require(False,'nonfinite JSON scalar')))
 frequency=(args.configured_hz_numerator,args.configured_hz_denominator) if args.configured_hz_numerator is not None else None
 report=audit(rows,frequency);report['input_event_file_sha256']=hashlib.sha256(raw).hexdigest();text=json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+'\n'
 if args.output:
  with open(args.output,'x') as f:f.write(text);f.flush();os.fsync(f.fileno())
 else:print(text,end='')
if __name__=='__main__':main()

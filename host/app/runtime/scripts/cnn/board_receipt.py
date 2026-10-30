"""Independent post-run audit. Files cannot attest physical hardware by themselves.

The caller supplies the release identity and boot UUID from trusted serial/SSH
checks. A passing structure still requires the separate deployment, live identity,
authorization, COPY/vecadd and RTL gates before H0_CNN_DIAGNOSTIC_PASS.
"""
import argparse,hashlib,json,re,tempfile
from pathlib import Path
from generate_fixtures import generate_fixture
from image_audit import identity_errors
from summarize import summarize,integer

NAMES=('x','wconv','bconv','z','r','p','wfc','bfc','y')
COUNTS=(64,18,2,72,72,18,36,2,2)
SIZES=tuple(128+((n*4+63)//64)*64 for n in COUNTS)
TOTAL=sum(SIZES)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def unique(pairs):
 value={}
 for k,v in pairs:
  if k in value:raise ValueError('duplicate JSON field '+k)
  value[k]=v
 return value
def load(text):return json.loads(text,object_pairs_hook=unique)
def interval(t,lo=0,hi=0xffffffffffffffff):
 return (t.get('valid') is True and type(t.get('result')) is int and t['result']==0
  and all(integer(t.get(k)) for k in ('start_ns','end_ns','elapsed_ns'))
  and lo<=t['start_ns']<=t['end_ns']<=hi and t['end_ns']-t['start_ns']==t['elapsed_ns'])
def verify(folder,expected,boot):
 errors=[];raw_hashes={};summary={}
 try:
  errors+=identity_errors(expected,expected['source_commit'],expected['build_id'])
  if not re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}',boot):errors.append('trusted boot UUID')
  run=load((folder/'run.json').read_text())
  manifest=load((folder/'release-manifest.json').read_text())
  if manifest!=expected:errors.append('installed manifest')
  for key,value in (('scope','REAL_DRIVER_CNN_I32_V1'),('source_commit',expected['source_commit']),('build_id',expected['build_id']),('boot_id',boot),('clock','CLOCK_MONOTONIC')):
   if run.get(key)!=value:errors.append('run '+key)
  if not re.match(r'^5\.4\.0-xilinx-v2020\.1 .+ aarch64$',run.get('kernel','')):errors.append('kernel/architecture')
  for key in ('result','cleanup_result','close_result','save_result'):
   if type(run.get(key)) is not int or run[key]!=0:errors.append(key)
  if run.get('iterations')!=24 or run.get('pinned_retained') is not False or not integer(run.get('clock_resolution_ns')) or not run['clock_resolution_ns']:errors.append('run completeness/clock')
  if not interval(run['setup']) or not interval(run['teardown']):errors.append('setup/teardown time')
  initial=run['initial_info'];final=run['final_info'];generation=initial['generation'];jobs=initial['jobs']
  if run.get('final_info_valid') is not True:errors.append('final snapshot missing')
  for snap in (initial,final):
   if any(type(snap.get(k)) is not int or snap[k]!=v for k,v in (('state',1),('profile_id',0),('generation',generation),('used',0),('result',0),('errors',0),('pending_w',0),('pending_r',0))):errors.append('READY/cleanup snapshot')
  records=[load(line) for line in (folder/'timing.jsonl').read_text().splitlines() if line.strip()]
  summary=summarize(records);errors+=summary['errors']
  if not summary['complete']:errors.append('24 iterations/20 measured')
  job_ids=set();last_end=run['setup']['end_ns']
  with tempfile.TemporaryDirectory() as tmp:
   gold=Path(tmp);generate_fixture('main',gold)
   for ri,r in enumerate(records):
    if r.get('scope')!='REAL_DRIVER_CNN_I32_V1':errors.append('record scope')
    if r['start_ns']<last_end:errors.append('overlapping iterations')
    last_end=r['end_ns']
    if r['bytes']!={'h2d':TOTAL+4*4160,'d2h':4*TOTAL}:errors.append('iteration bytes')
    transfers=r.get('transfers',[])
    want=[(o,'H2D') for o in range(2,11)]+[(o,'D2H') for _ in range(4) for o in range(2,11)]
    if [(t.get('object'),t.get('direction')) for t in transfers]!=want:errors.append('transfer inventory')
    tensor_times={'H2D':0,'D2H':0};last_transfer=r['start_ns']
    for t in transfers:
     o=t['object']-2
     if o not in range(9) or t['direction'] not in tensor_times:raise ValueError('transfer object/direction')
     size=SIZES[o]
     if any(type(t.get(k)) is not int or t[k]!=v for k,v in (('bytes',size),('confirmed_bytes',size),('payload_bytes',COUNTS[o]*4),('guard_bytes',128),('padding_bytes',size-128-COUNTS[o]*4),('result',0))):errors.append('transfer bytes/result')
     if not interval(t['time'],last_transfer,r['end_ns']):errors.append('transfer clock')
     last_transfer=t['time']['end_ns'];tensor_times[t['direction']]+=t['time']['elapsed_ns']
    if tensor_times!={'H2D':r['tensor_h2d_ns'],'D2H':r['tensor_d2h_ns']}:errors.append('tensor timing sum')
    previous=transfers[8]['time']['end_ns'] if len(transfers)>8 else r['start_ns']
    for li,l in enumerate(r['layers']):
     jobs=(jobs+1)&0xffffffff;snap=l['snapshot'];job=l['job_id']
     if not integer(job) or not job or job in job_ids:errors.append('job ID')
     job_ids.add(job)
     if l.get('snapshot_valid') is not True or l.get('raw_valid')!=[True]*9 or l.get('max_abs')!=0:errors.append('layer validation/snapshot')
     if any(type(snap.get(k)) is not int or snap[k]!=v for k,v in (('state',3),('generation',generation),('profile_id',0),('job_id',job),('jobs',jobs),('result',0),('errors',0),('pending_w',0),('pending_r',0))):errors.append('completion snapshot')
     if not integer(snap.get('hwstatus')) or not snap['hwstatus']&2 or snap['hwstatus']&4:errors.append('DONE/FENCE completion')
     if l.get('program_sha256')!=expected['programs'][li]['sha256'] or l.get('bytes')!={'h2d':4160,'d2h':TOTAL}:errors.append('program/layer bytes')
     if not interval(l['code_args_time'],previous,l['start_ns']) or l['code_args_time']['elapsed_ns']!=l['layer_code_args_h2d_ns']:errors.append('code/args clock')
     reads=transfers[9+li*9:18+li*9]
     if len(reads)!=9 or reads[0]['time']['start_ns']<l['end_ns']:errors.append('read before completion')
     read_end=reads[-1]['time']['end_ns'] if reads else l['end_ns']
     if not interval(l['verify_time'],read_end,r['end_ns']) or l['verify_time']['elapsed_ns']!=l['layer_verify_ns']:errors.append('verification clock')
     previous=l['verify_time']['end_ns']
     for oi,name in enumerate(NAMES):
      suffix='.expected.bin' if name in ('z','r','p','y')[:li+1] else '.bin'
      p=folder/'raw'/('iteration-%d-layer-%d-object-%d.bin'%(ri,li,oi))
      if p.read_bytes()!=(gold/(name+suffix)).read_bytes():errors.append('raw numeric/guard '+p.name)
      raw_hashes[p.name]=sha(p)
  if run['teardown']['start_ns']<last_end or final.get('jobs')!=jobs:errors.append('final jobs/time')
  if len(job_ids)!=96 or len(raw_hashes)!=864 or set(p.name for p in (folder/'raw').iterdir())!=set(raw_hashes):errors.append('raw/job inventory')
 except (OSError,KeyError,TypeError,ValueError,IndexError) as e:errors.append('Incomplete/malformed evidence: '+str(e))
 return dict(scope='CNN_BOARD_RECEIPT_STRUCTURAL_AUDIT',pass_=not errors,errors=sorted(set(errors)),summary=summary,raw_sha256=raw_hashes,
  h0_cnn='REQUIRES_SEPARATE_TRUSTED_LIVE_AND_RELEASE_GATES',h1='NOT_RUN',physical_identity='NOT_ATTESTED_BY_FILES')
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--run',required=True);a.add_argument('--expected-manifest',required=True);a.add_argument('--trusted-boot-id',required=True);a.add_argument('--out',required=True);o=a.parse_args()
 result=verify(Path(o.run),load(Path(o.expected_manifest).read_text()),o.trusted_boot_id)
 with Path(o.out).open('x') as f:f.write(json.dumps(result,indent=2)+'\n')
 raise SystemExit(0 if result['pass_'] else 1)

"""Keep every sample, require a full batch, never drop a failed/slow run."""
import argparse,json,math,statistics
from pathlib import Path
def summarize(audits:list,timing_kind:str)->dict:
 out=dict(publish_performance=False,median_ns=None,p95_ns=None,gpu_seconds=None,samples=audits,errors=[])
 if not audits:out['errors'].append('NO_SAMPLES');return out
 identity_keys=['boot_id','generation','source_digest','roi_index','mode']
 base=audits[0];seen=set();kinds={1:[],2:[],3:[]};values=[]
 for a in audits:
  if a.get('integrity')!='PASS':out['errors'].append('FAILED_SAMPLE')
  numeric=a.get('Linux-v5_same_math')
  if not isinstance(numeric,dict) or numeric.get('status')!='PASS':out['errors'].append('LINUX_V5_NUMERIC_NOT_PASS')
  if any(a.get(k)!=base.get(k) for k in identity_keys) or a.get('mode')!=timing_kind:out['errors'].append('MIXED_IDENTITY_OR_SCOPE')
  key=(a.get('run_id'),a.get('roi_index'),a.get('sample_kind'),a.get('sample_index'))
  if key in seen:out['errors'].append('DUPLICATE_SAMPLE')
  seen.add(key);kind=a.get('sample_kind')
  if kind not in kinds:out['errors'].append('NON_TIMING_SAMPLE');continue
  kinds[kind].append(a.get('sample_index'))
  if kind==3:
   n=a.get('T_resident_ns')
   if type(n) not in [int,float] or not math.isfinite(n) or n<0:out['errors'].append('INVALID_DURATION')
   else:values.append(n)
 if sorted(kinds[1])!=[0] or sorted(kinds[2])!=[0,1,2] or sorted(kinds[3])!=list(range(20)):out['errors'].append('FIRST_WARMUP_MEASURED_INCOMPLETE')
 if len(values)==20 and not any(e in out['errors'] for e in ['FAILED_SAMPLE','MIXED_IDENTITY_OR_SCOPE','DUPLICATE_SAMPLE','INVALID_DURATION']):
  s=sorted(values);index=(len(s)-1)*.95;lo=int(index);fraction=index-lo;out['median_ns']=statistics.median(s);out['p95_ns']=s[lo]+fraction*(s[min(lo+1,len(s)-1)]-s[lo]);out['measured_raw_ns']=values
 if any(a.get('scope')!='HW4_READONLY_PREFLIGHT' for a in audits):out['errors'].append('TEST_OR_UNVERIFIED_SCOPE')
 out['publish_performance']=not out['errors'];return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('audits',nargs='+',type=Path);p.add_argument('--mode',required=True);p.add_argument('--out',required=True,type=Path);a=p.parse_args();result=summarize([json.loads(p.read_text(encoding='utf-8')) for p in a.audits],a.mode);a.out.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8');raise SystemExit(0 if result['publish_performance'] else 1)

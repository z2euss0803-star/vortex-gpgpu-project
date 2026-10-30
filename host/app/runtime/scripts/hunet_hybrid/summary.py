"""Separate raw GPU cycles, CPU/driver durations, and whole-run timing."""
import argparse,csv,json
from pathlib import Path
def rows(path,values):
 if not values:return
 fields=list(dict.fromkeys(k for row in values for k in row))
 with path.open('x',newline='',encoding='utf-8') as f:
  w=csv.DictWriter(f,fields);w.writeheader();w.writerows(values)
def write_summary(audit,out):
 out=Path(out);out.mkdir(exist_ok=False,parents=True)
 for name,key in [('gpu-jobs','gpu'),('cpu-stages','per_layer_cpu'),('logical-stages','logical'),('stage-errors','stage_errors')]:rows(out/(name+'.csv'),audit.get(key,[]))
 with (out/'audit.json').open('x',encoding='utf-8') as f:json.dump(audit,f,indent=2,ensure_ascii=False,allow_nan=False);f.write('\n')
 stop=dict(integrity=audit.get('integrity'),run_id=audit.get('run_id'),boot_id=audit.get('boot_id'),binding_sha256=audit.get('binding_sha256'),reason=audit.get('stop_reason',''),formal_allowed=audit.get('formal_allowed',False),publish_performance=audit.get('publish_performance',False))
 with (out/'stop-receipt.json').open('x',encoding='utf-8') as f:json.dump(stop,f,indent=2,allow_nan=False);f.write('\n')
 if not audit.get('publish_performance'):
  (out/'performance-not-accepted.txt').write_text(audit.get('stop_reason','')+'\nRaw timing is retained; no accepted mixed-network speedup.\n',encoding='utf-8')
def summarize_formal(audits):
 # No synthetic batch completion: precisely one first, three warmup, twenty measured.
 if len(audits)!=24 or any(a.get('integrity')!='PASS' or not a.get('publish_performance') or a.get('mode')!='formal' for a in audits):raise ValueError('FORMAL_BATCH_NOT_ACCEPTED')
 if len({a['run_id'] for a in audits})!=24 or len({(a['boot_id'],a['binding_sha256'],a['roi_index']) for a in audits})!=1:raise ValueError('FORMAL_BATCH_IDENTITY')
 if {(a['sample_kind'],a['sample_index']) for a in audits}!={(1,0)}|{(2,i) for i in range(3)}|{(3,i) for i in range(20)}:raise ValueError('FORMAL_BATCH_COVERAGE')
 measured=[a for a in audits if a['sample_kind']==3]
 return dict(scope='ACCEPTED_FORMAL_BATCH',gpu_seconds=None,raw_gpu_cycles=[a['raw_gpu_cycles'] for a in measured],cpu_compute_ns=[a['cpu_compute_ns'] for a in measured],resident_ns=[a['T_resident_ns'] for a in measured],first=[a for a in audits if a['sample_kind']==1][0],T_transfer_ns=None)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--audit',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();write_summary(json.loads(a.audit.read_bytes()),a.out)

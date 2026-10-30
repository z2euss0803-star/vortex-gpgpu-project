"""Deduplicate accelerator receipts and union overlapping timing intervals."""
import argparse,json,re
from collections import defaultdict
from decimal import Decimal,localcontext
from pathlib import Path

def integer(value):
    if type(value) is int and value>=0:return value
    if isinstance(value,str) and re.fullmatch('[0-9]+',value):return int(value)
    raise ValueError('nonnegative integer required; floats/bools are not counters')

def nonempty(row,fields):
    for k in fields:
        if not isinstance(row.get(k),str) or not row[k].strip():raise ValueError('nonempty '+k+' required')

def summarize(d):
    try:
        if not isinstance(d,dict):raise ValueError('object required')
        jobs=d.get('jobs');intervals=d.get('intervals',[])
        if not isinstance(jobs,list) or not isinstance(intervals,list) or not (jobs or intervals):raise ValueError('nonempty jobs or intervals required')
        seen={};duplicates=0;groups=defaultdict(lambda:{'cycles':0,'jobs':0})
        for row in jobs:
            if not isinstance(row,dict):raise ValueError('job object required')
            fields=['boot_id','run_id','request_id','job_id','clock_domain'];nonempty(row,fields)
            epoch=integer(row.get('owner_epoch'));cycles=integer(row.get('gpu_cycles'))
            hz=None if row.get('counter_hz') is None else integer(row['counter_hz'])
            if hz==0:raise ValueError('frequency must be positive or null')
            key=(row['boot_id'],epoch,row['run_id'],row['request_id'],row['job_id'],row['clock_domain'])
            if key in seen:
                if seen[key]!=(cycles,hz):raise ValueError('conflicting duplicate receipt')
                duplicates+=1;continue
            seen[key]=(cycles,hz)
            group=(row['boot_id'],epoch,row['run_id'],row['clock_domain'],hz)
            groups[group]['cycles']+=cycles;groups[group]['jobs']+=1
        cycle_groups=[]
        for key,values in groups.items():
            boot,epoch,run,domain,hz=key
            with localcontext() as ctx:
                ctx.prec=max(40,len(str(values['cycles']))+20)
                seconds=None if hz is None else str(Decimal(values['cycles'])/Decimal(hz))
            cycle_groups.append(dict(boot_id=boot,owner_epoch=epoch,run_id=run,clock_domain=domain,counter_hz=hz,estimated_seconds=seconds,**values))
        spans=defaultdict(list)
        for row in intervals:
            if not isinstance(row,dict):raise ValueError('interval object required')
            nonempty(row,['scope_id','clock_domain'])
            start=integer(row.get('start_ns'));end=integer(row.get('end_ns'))
            if end<start:raise ValueError('interval end before start')
            spans[(row['scope_id'],row['clock_domain'])].append((start,end))
        interval_groups=[]
        for (scope,domain),items in spans.items():
            total=0;left=right=None
            for start,end in sorted(items):
                if left is None:left,right=start,end
                elif start>right:total+=right-left;left,right=start,end
                else:right=max(right,end)
            total+=right-left
            interval_groups.append(dict(scope_id=scope,clock_domain=domain,union_ns=total,summed_ns=sum(b-a for a,b in items),intervals=len(items)))
        return dict(status='SUMMARIZED',unique_jobs=len(seen),duplicate_receipts=duplicates,cycle_groups=cycle_groups,interval_groups=interval_groups,scope='DECLARED_TRACE_ONLY')
    except (ValueError,TypeError) as e:return {'status':'INVALID_INPUT','issues':[str(e)]}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',required=True);a=p.parse_args()
    try:
        with Path(a.input).open(encoding='utf-8-sig') as f:r=summarize(json.load(f))
    except (OSError,ValueError) as e:r={'status':'INVALID_INPUT','issues':[str(e)]}
    print(json.dumps(r,ensure_ascii=False,indent=2))
    return 0 if r['status']=='SUMMARIZED' else 1

if __name__=='__main__':raise SystemExit(main())

"""Static canonical Mosaic budget; no GPU timing prediction or device access."""
import argparse
import json
from pathlib import Path
from scripts.mosaic.contract import build_graph


def workload(model, width=1280, height=960, limits=(4096,)*6):
    if len(limits)!=6 or any(type(n) is not int or not 1<=n<=4096 for n in limits):
        raise ValueError('BATCH_POLICY')
    graph=build_graph(model,width,height)
    phases=[]
    for row in graph['layers']:
        for op,items in zip(row['ops'],row['work_items']):
            limit=limits[op-1]
            phases.append({'layer':row['index'],'op':op,'items':items,'limit':limit,'jobs':(items+limit-1)//limit})
    jobs=sum(p['jobs'] for p in phases)
    return {'scope':'STATIC_WORKLOAD_NOT_BOARD','gpu_raw_cycles':None,'phases':phases,
            'jobs':jobs,'raw_evidence_bytes':jobs*512,'graph_sha256':graph['sha256']}


def admit(report,max_jobs,max_evidence_bytes):
    if type(max_jobs) is not int or type(max_evidence_bytes) is not int or max_jobs<=0 or max_evidence_bytes<=0:
        raise ValueError('EXPLICIT_POSITIVE_BUDGET_REQUIRED')
    if report['jobs']>max_jobs or report['raw_evidence_bytes']>max_evidence_bytes:
        raise ValueError('WORKLOAD_BUDGET_EXCEEDED')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model',required=True);p.add_argument('--conv-limit',type=int,default=4096)
    p.add_argument('--max-jobs',type=int,required=True);p.add_argument('--max-evidence-bytes',type=int,required=True)
    args=p.parse_args();limits=[4096]*6;limits[1]=args.conv_limit
    report=workload(json.loads(Path(args.model).read_text()),limits=limits)
    print(json.dumps(report,indent=2));admit(report,args.max_jobs,args.max_evidence_bytes)


if __name__=='__main__':main()

"""Summarize explicitly bound evidence records, without promoting evidence layers."""
import argparse,json
from pathlib import Path

def summarize(document):
    invalid=lambda message:{'status':'INVALID_INPUT','issues':[message]}
    if not isinstance(document,dict):return invalid('object required')
    target=document.get('target_identity');required=document.get('required_layers');rows=document.get('records')
    if (not isinstance(target,str) or not target.strip() or not isinstance(required,list) or not required
        or any(not isinstance(x,str) or not x.strip() for x in required)
        or len(set(required))!=len(required) or not isinstance(rows,list)):
        return invalid('target_identity, unique nonempty required_layers and records are required')
    for row in rows:
        if not isinstance(row,dict) or any(not isinstance(row.get(k),str) or not row[k].strip() for k in ('layer','status','identity','evidence')):
            return invalid('each record needs nonempty layer, status, identity and evidence')
    layers={};other=[]
    for row in rows:
        if row['identity']!=target:other.append(row)
    for layer in dict.fromkeys(required+[r['layer'] for r in rows if r['identity']==target]):
        matched=[r for r in rows if r['identity']==target and r['layer']==layer]
        statuses={r['status'] for r in matched}
        if not matched:verdict='UNKNOWN'
        elif 'PASS' in statuses and 'FAIL' in statuses:verdict='CONFLICT'
        elif 'FAIL' in statuses:verdict='FAIL'
        elif statuses=={'PASS'}:verdict='PASS'
        else:verdict='INCOMPLETE'
        layers[layer]={'status':verdict,'records':matched}
    return {'status':'SUMMARIZED','target_identity':target,'layers':layers,
            'other_identity_records':other,'all_required_pass':all(layers[x]['status']=='PASS' for x in required),
            'scope':'DECLARED_RECORDS_ONLY'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',required=True);a=p.parse_args()
    try:
        with Path(a.input).open(encoding='utf-8-sig') as f:r=summarize(json.load(f))
    except (OSError,ValueError) as e:r={'status':'INVALID_INPUT','issues':[str(e)]}
    print(json.dumps(r,ensure_ascii=False,indent=2))
    return 0 if r['status']=='SUMMARIZED' else 1

if __name__=='__main__':raise SystemExit(main())

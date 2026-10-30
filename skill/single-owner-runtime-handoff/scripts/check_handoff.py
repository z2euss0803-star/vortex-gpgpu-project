"""Read-only handoff gate for a normalized snapshot; no device/control calls."""
import argparse,json
from pathlib import Path

def evaluate(d):
    def verdict(decision,reason):return {'decision':decision,'reason':reason,'scope':'SNAPSHOT_ONLY','device_operations':False}
    if not isinstance(d,dict):return verdict('UNKNOWN','object required')
    expected=d.get('expected');s=d.get('snapshot')
    if not isinstance(expected,dict) or not isinstance(s,dict):return verdict('UNKNOWN','expected identity and snapshot required')
    for field in ['boot_id','current_mode']:
        if any(not isinstance(v.get(field),str) or not v[field].strip() for v in [expected,s]):return verdict('UNKNOWN','missing '+field)
    for field in ['owner_pid','owner_start_ticks','owner_epoch']:
        minimum=1 if field=='owner_pid' else 0
        if any(type(v.get(field)) is not int or v[field]<minimum for v in [expected,s]):return verdict('UNKNOWN','invalid '+field)
    for field in ['boot_id','owner_pid','owner_start_ticks','owner_epoch','current_mode']:
        if expected[field]!=s[field]:return verdict('STOP','identity changed: '+field)
    if not isinstance(d.get('target_mode'),str) or not d['target_mode'].strip():return verdict('UNKNOWN','target mode required')
    for field,owner in [('now_ns',d),('max_age_ns',d),('observed_at_ns',s)]:
        if type(owner.get(field)) is not int or owner[field]<0:return verdict('UNKNOWN','invalid '+field)
    age=d['now_ns']-s['observed_at_ns']
    if age<0 or age>d['max_age_ns']:return verdict('UNKNOWN','stale or future observation')
    for field in ['owner_count','inflight','dma_pending','tex_pending','completion_pending']:
        if type(s.get(field)) is not int or s[field]<0:return verdict('UNKNOWN','counter unavailable: '+field)
    if s['owner_count']!=1:return verdict('STOP','exactly one owner is required')
    if type(s.get('retirement_unknown')) is not bool:return verdict('UNKNOWN','retirement state unavailable')
    if s['retirement_unknown']:return verdict('STOP','retirement is unproven; preserve original request')
    if any(s[x]!=0 for x in ['inflight','dma_pending','tex_pending','completion_pending']):return verdict('WAIT','request or transactions have not drained')
    return verdict('ALLOW_HANDOFF','same owner; all declared work drained')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',required=True);a=p.parse_args()
    try:
        with Path(a.input).open(encoding='utf-8-sig') as f:r=evaluate(json.load(f))
    except (OSError,ValueError) as e:r={'decision':'UNKNOWN','reason':str(e),'device_operations':False}
    print(json.dumps(r,ensure_ascii=False,indent=2))
    return 0

if __name__=='__main__':raise SystemExit(main())

"""Hierarchical timing audit. Host fixtures never supply new GPU evidence."""
import argparse, json, pathlib
PHASES = frozenset(('decode','preprocess','load','quant_pack','gpu_submit_wait','guard_cdma','poll','handoff','cpu_post','readback','colour','display_submit','display_present'))
CLOCKS = frozenset(('A53_MONOTONIC','B_MONOTONIC','DRM_CLOCK_UNKNOWN'))

def uint(value, bits=64):
    return type(value) is int and 0 <= value < 2**bits

def audit_document(d):
    errors, totals, unclassified = [], {}, {}
    intervals = d.get('intervals', [])
    jobs = d.get('gpu_jobs', [])
    if d.get('schema') != 1 or d.get('scope') not in ('OFFLINE_TIMELINE','REAL_BOARD_CAPTURE'):
        errors.append('schema/scope')
    if d.get('raw_gpu_cycles') is not None:
        errors.append('unscoped aggregate raw cycles')
    if not isinstance(intervals, list) or len(intervals) > 2048 or not isinstance(jobs, list) or len(jobs) > 2048:
        return dict(status='FAIL', errors=['bounded arrays required'], raw_gpu_cycles=None, gpu_jobs=jobs)
    usable = {}
    children = {}
    groups = {}
    for i, r in enumerate(intervals):
        if not isinstance(r,dict) or r.get('phase') not in PHASES or r.get('clock') not in CLOCKS or type(r.get('valid')) is not bool or not uint(r.get('begin_ns')) or not uint(r.get('end_ns')) or type(r.get('parent')) is not int:
            errors.append('invalid interval %s' % i); continue
        parent = r['parent']
        if parent < -1 or parent >= i:
            errors.append('parent order %s' % i); continue
        if not r['valid']:
            errors.append('invalid/unknown interval %s' % i); continue
        if r['clock'] == 'DRM_CLOCK_UNKNOWN' or r['begin_ns'] > r['end_ns']:
            errors.append('clock/reversed interval %s' % i); continue
        if parent >= 0:
            p = usable.get(parent)
            if p is None or p['clock'] != r['clock'] or r['begin_ns'] < p['begin_ns'] or r['end_ns'] > p['end_ns']:
                errors.append('cross clock/outside parent %s' % i); continue
            children.setdefault(parent,[]).append(r)
        groups.setdefault((r['clock'],parent),[]).append(r)
        usable[i] = r
    for (clock,parent), rows in groups.items():
        rows = sorted(rows,key=lambda r:(r['begin_ns'],r['end_ns']))
        if any(a['end_ns'] > b['begin_ns'] for a,b in zip(rows,rows[1:])):
            errors.append('overlapping siblings/root %s %s' % (clock,parent))
        if parent == -1:
            totals[clock] = sum(r['end_ns']-r['begin_ns'] for r in rows)
    for i,r in usable.items():
        if i in children:
            unclassified[i] = r['end_ns']-r['begin_ns']-sum(c['end_ns']-c['begin_ns'] for c in children[i])
            if unclassified[i] < 0: errors.append('children exceed parent %s' % i)
    seen = set()
    for j in jobs:
        if not isinstance(j,dict): errors.append('invalid GPU job'); continue
        key = (j.get('generation'),j.get('job_id'))
        if d.get('scope') != 'REAL_BOARD_CAPTURE': errors.append('offline GPU observation forbidden')
        if not uint(j.get('observed_raw_u32'),32) or not uint(key[0],32) or not uint(key[1]) or key[1] == 0 or key in seen:
            errors.append('GPU identity/raw/duplicate')
        seen.add(key)
        for field in ('valid','pre_ack','submit_enter','wrapped'):
            if type(j.get(field)) is not bool: errors.append('GPU boolean '+field)
        valid = j.get('valid') is True and j.get('pre_ack') is True and j.get('submit_enter') is True and j.get('wrapped') is False
        if not valid or j.get('raw_cycles') != j.get('observed_raw_u32'):
            errors.append('invalid GPU preACK/entry/wrap/raw')
    return dict(status='FAIL' if errors else 'HOST_AUDIT_PASS_NOT_NEW_TIMING', errors=errors, root_ns_by_clock=totals, unclassified_ns=unclassified, raw_gpu_cycles=None, gpu_jobs=jobs, display_presented_proven=False,
                note='Intervals are independently audited per clock. A timestamp interval alone is not a DRM completion event; pure PE/internal GPU stages remain UNKNOWN.')

def audit_timeline(run_dir):
    return audit_document(json.loads((pathlib.Path(run_dir)/'timeline.json').read_text()))

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--offline',action='store_true');a=p.parse_args()
    d=json.loads((pathlib.Path(a.run)/'timeline.json').read_text())
    if a.offline and d.get('scope')!='OFFLINE_TIMELINE': raise SystemExit('offline invocation cannot admit board capture')
    r=audit_document(d);print(json.dumps(r,sort_keys=True));raise SystemExit(0 if not r['errors'] else 1)

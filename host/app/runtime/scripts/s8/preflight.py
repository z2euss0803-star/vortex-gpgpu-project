"""Pure admission decisions; observations never launch or delete processes."""
from pathlib import Path
import re
import subprocess

def verify_parent(repo: Path, expected: str) -> dict:
    if not re.fullmatch(r'[0-9a-f]{40}', expected):
        raise ValueError('expected full lowercase commit identity')
    def git(*args):
        return subprocess.run(['git','-C',str(repo),*args],capture_output=True,text=True)
    head=git('rev-parse','HEAD')
    obj=git('rev-parse','--verify',expected+'^{commit}')
    if head.returncode or obj.returncode or obj.stdout.strip()!=expected:
        raise ValueError('parent commit unavailable or not exact')
    ancestry=git('merge-base','--is-ancestor',expected,'HEAD')
    if ancestry.returncode:
        raise ValueError('candidate does not descend from frozen parent')
    return dict(parent=expected,head=head.stdout.strip(),ancestor_verified=True,
                repo=str(repo.resolve()))

def admit_resources(snapshot: dict, request: dict) -> dict:
    reasons=[]
    for key in ('host','account','owner'):
        if not isinstance(request.get(key),str) or not request[key] or snapshot.get(key)!=request[key]:
            reasons.append('identity:'+key)
    if snapshot.get('coordination')!='CLEAR': reasons.append('coordination_unknown_or_busy')
    if snapshot.get('other_heavy')!=[]: reasons.append('other_heavy_unknown_or_active')
    for key in ('memory_bytes','disk_bytes','cpu_jobs','now','max_age'):
        value=request.get(key)
        if type(value) is not int or value<=0: reasons.append('invalid_request:'+key)
    for key in ('memory_available_bytes','disk_available_bytes','cpu_available','captured_at'):
        value=snapshot.get(key)
        if type(value) is not int or value<0: reasons.append('invalid_snapshot:'+key)
    if reasons:
        return dict(admitted=False,classification='RESOURCE_REFUSED',reasons=reasons)
    if request['cpu_jobs']>2 or request['cpu_jobs']>snapshot['cpu_available']:
        reasons.append('cpu_budget')
    if request['memory_bytes']>snapshot['memory_available_bytes']: reasons.append('memory_budget')
    if request['disk_bytes']>snapshot['disk_available_bytes']: reasons.append('disk_budget')
    age=request['now']-snapshot['captured_at']
    if request['max_age']>300 or age<0 or age>request['max_age']: reasons.append('stale_snapshot')
    return dict(admitted=not reasons,classification='RESOURCE_REFUSED' if reasons else 'ADMITTED',
                reasons=reasons,snapshot=dict(snapshot),request=dict(request))

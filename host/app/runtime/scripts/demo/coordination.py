"""Acquire the existing shared-B ownership locks before heavy offline work."""
import contextlib,datetime,json,pathlib
from scripts.demo.resources import admit_resources

@contextlib.contextmanager
def coordinated(env_path,path,kind):
    import fcntl
    from scripts.demo.image import resource_facts
    e=json.loads(pathlib.Path(env_path).read_text());marker=pathlib.Path(e['coordination_file'])
    if str(marker)!='/home/z2euss/codex-coordination/20261018-mosaic-hunet-dp.md' or not marker.is_file():raise ValueError('known C coordination document required')
    locks=[]
    try:
        for name in ('S8-NATIVE-OWNER.lock','S8-IMAGE-OWNER.lock','S8-REVIEW-NATIVE-OWNER.lock'):
            f=(marker.parent/name).open('a');locks.append(f);fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
        gate=admit_resources(resource_facts(path),kind)
        with marker.open('a') as f:f.write('\nUTC='+datetime.datetime.utcnow().isoformat()+'Z dual demo '+kind+' all3 locks, worker1; actual fresh admission='+json.dumps(gate)+'; isolated software/image only, C/board/SD untouched.\n')
        yield gate
    finally:
        for f in reversed(locks):f.close()

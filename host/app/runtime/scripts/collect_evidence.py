"""Run independently failing offline gates and retain command/exit/output evidence."""
import argparse,datetime,hashlib,json,pathlib,subprocess,sys,os,platform,re,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/cnn'))
from source_identity import source_commit, source_tree_clean
STAGES=('contract','uapi','memory','jobs','probe','copy','submit','runtime',
        'cnn-reference','cnn-kernels','cnn-contract','cnn-preflight','cnn-timing',
        'cnn-session','cnn-release','tools','legacy')

def new_folder(root,run_id):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*',run_id):
        raise ValueError('Invalid run ID')
    folder=root/'evidence'/run_id;folder.mkdir(parents=True,exist_ok=False)
    return folder
def run(name,command,folder):
    t=datetime.datetime.now(datetime.timezone.utc).isoformat()
    started_ns=time.monotonic_ns()
    p=subprocess.run(command,cwd=str(ROOT),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (folder/(name+'.log')).write_bytes(p.stdout)
    (folder/(name+'.stderr.txt')).write_bytes(p.stderr)
    result=dict(command=command,cwd=str(ROOT),started=t,
        ended=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        host_elapsed_ns=time.monotonic_ns()-started_ns,
        stdout_sha256=hashlib.sha256(p.stdout).hexdigest(),
        stderr_sha256=hashlib.sha256(p.stderr).hexdigest(),
        exit=p.returncode,status='PASS' if p.returncode==0 else 'FAIL')
    (folder/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print(name,result['status'],flush=True)
    return result
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--out',default='build/cnn');a.add_argument('--run-id',default=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));args=a.parse_args()
    folder=new_folder(ROOT,args.run_id)
    commit=source_commit(ROOT);clean=source_tree_clean(ROOT)
    gates={}
    for stage in STAGES:
        gates[stage]=run(stage,['make','OUT='+args.out,'test-'+stage],folder)
    (folder/'results.json').write_text(json.dumps(dict(scope='OFFLINE_HOST_ONLY',source_commit=commit,source_tree_clean=clean,
        machine=platform.node(),account=os.environ.get('USERNAME',os.environ.get('USER','unknown')),
        cwd=str(ROOT),out=args.out,run_id=args.run_id,gates=gates,board='NOT_RUN',h1='NOT_RUN'),indent=2)+'\n')
    sys.exit(any(r['exit'] for r in gates.values()))

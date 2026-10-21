"""Read-only F-drive metadata inventory. Never follows reparse points or deletes."""
from pathlib import Path
import datetime, heapq, json, os, platform, getpass, shutil, stat, time

ROOT=Path('F:/')
OUT=Path(__file__).resolve().parent/'evidence'
OUT.mkdir(exist_ok=True)
START=datetime.datetime.now(datetime.timezone.utc).isoformat()
SKIP={'$RECYCLE.BIN','System Volume Information','Config.Msi'}
errors=[]; reparse=[]; large=[]; directory_rows=[]; agents=[]
totals={'files':0,'directories':0,'logical_bytes':0}
interesting={'.Xil','.cache','.pytest_cache','__pycache__','node_modules','.venv','venv','build','runs','evidence','artifacts','delivery','.git','xsim.dir','ip_user_files'}

def walk(folder, depth=0):
    size=count=0; local_errors=0
    try:
        with os.scandir(folder) as iterator:
            entries=list(iterator)
    except OSError as error:
        errors.append({'path':str(folder),'error':str(error)})
        return 0,0,1
    totals['directories']+=1
    for entry in entries:
        p=Path(entry.path)
        if depth==0 and entry.name in SKIP:continue
        try:
            info=entry.stat(follow_symlinks=False)
            if getattr(info,'st_file_attributes',0)&0x400 or stat.S_ISLNK(info.st_mode):
                reparse.append(str(p)); continue
            if stat.S_ISDIR(info.st_mode):
                b,n,e=walk(p,depth+1); size+=b; count+=n; local_errors+=e
            elif stat.S_ISREG(info.st_mode):
                size+=info.st_size; count+=1; totals['files']+=1; totals['logical_bytes']+=info.st_size
                if entry.name=='AGENTS.md':agents.append(str(p))
                if info.st_size>=16*1024**2:
                    large.append({'path':str(p),'bytes':info.st_size,'modified_utc':datetime.datetime.fromtimestamp(info.st_mtime,datetime.timezone.utc).isoformat(),'hardlink_count':info.st_nlink,'attributes':getattr(info,'st_file_attributes',0)})
        except OSError as error:
            errors.append({'path':str(p),'error':str(error)}); local_errors+=1
    if depth<=4 or folder.name in interesting:
        directory_rows.append({'path':str(folder),'depth':depth,'logical_bytes':size,'files':count,'read_errors':local_errors})
    if depth==1:
        print(json.dumps({'root':str(folder),'gib':round(size/2**30,3),'files':count,'read_errors':local_errors}),flush=True)
    return size,count,local_errors

usage_before=shutil.disk_usage(ROOT)
walk(ROOT)
result={'started_utc':START,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.node(),'account':getpass.getuser(),
        'mode':'READ_ONLY_METADATA_NO_DELETE','capacity':dict(zip(['total','used','free'],usage_before)),
        'measurement':'logical file bytes; may double-count hardlinks and sparse images, not guaranteed recoverable disk allocation',
        'excluded_system_roots':sorted(SKIP),'totals':totals,'directories':directory_rows,
        'large_files':sorted(large,key=lambda x:x['bytes'],reverse=True),'errors':errors,'reparse_skipped':reparse,'agents_files':agents}
(OUT/'f_drive_inventory.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'status':'INVENTORY_COMPLETE','totals':totals,'read_errors':len(errors),'reparse_skipped':len(reparse),'large_files':len(large),'report':str(OUT/'f_drive_inventory.json')},ensure_ascii=True),flush=True)

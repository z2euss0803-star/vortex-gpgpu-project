"""Read-only SHA-256 manifest audit. Python 3.9+, standard library only."""
from pathlib import Path
import argparse,hashlib,json,os,re,stat

def audit(root,manifest):
    root=Path(root).resolve()
    rows=manifest.get('files') if isinstance(manifest,dict) else None
    issues=[];seen=set()
    if not isinstance(rows,list) or not rows:
        return {'status':'INVALID_MANIFEST','issues':[{'reason':'NONEMPTY_FILES_REQUIRED'}]}
    for row in rows:
        if not isinstance(row,dict):issues.append({'reason':'INVALID_ROW'});continue
        name=row.get('path');size=row.get('bytes');digest=row.get('sha256')
        if (not isinstance(name,str) or not name or '\\' in name or ':' in name
            or any(x in ('','.','..') or x.endswith(('.',' ')) for x in name.split('/'))
            or type(size) is not int or size<0
            or not isinstance(digest,str) or re.fullmatch('[0-9a-fA-F]{64}',digest) is None):
            issues.append({'path':name,'reason':'INVALID_PATH_SIZE_OR_SHA256'});continue
        normalized=os.path.normcase(name)
        if normalized in seen:issues.append({'path':name,'reason':'DUPLICATE_PATH'})
        seen.add(normalized)
    if issues:return {'status':'INVALID_MANIFEST','issues':issues}
    if not root.is_dir():return {'status':'MISMATCH','issues':[{'reason':'ROOT_UNAVAILABLE'}]}
    for row in rows:
        name=row['path'];p=root/name
        try:
            parent=root;linked=False
            for part in name.split('/'):
                parent=parent/part
                if parent.is_symlink():linked=True;break
            if linked:issues.append({'path':name,'reason':'SYMLINK_NOT_ACCEPTED'});continue
            before=p.stat()
            if not stat.S_ISREG(before.st_mode):issues.append({'path':name,'reason':'NOT_REGULAR_FILE'});continue
            h=hashlib.sha256()
            with p.open('rb') as f:
                for data in iter(lambda:f.read(1048576),b''):h.update(data)
            after=p.stat()
            if (before.st_size,before.st_mtime_ns,before.st_ino)!=(after.st_size,after.st_mtime_ns,after.st_ino):
                issues.append({'path':name,'reason':'CHANGED_DURING_READ'})
            elif before.st_size!=row['bytes'] or h.hexdigest()!=row['sha256'].lower():
                issues.append({'path':name,'reason':'CONTENT_MISMATCH','actual_bytes':before.st_size,'actual_sha256':h.hexdigest()})
        except FileNotFoundError:issues.append({'path':name,'reason':'MISSING'})
        except OSError as error:issues.append({'path':name,'reason':'READ_ERROR','errno':error.errno})
    return {'status':'MISMATCH' if issues else 'VERIFIED','files':len(rows),'issues':issues,'scope':'FILE_BYTES_ONLY'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',required=True);p.add_argument('--manifest',required=True)
    a=p.parse_args()
    try:
        with Path(a.manifest).open(encoding='utf-8-sig') as f:manifest=json.load(f)
        result=audit(a.root,manifest)
    except (OSError,ValueError) as e:result={'status':'INVALID_MANIFEST','issues':[{'reason':str(e)}]}
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['status']=='VERIFIED' else 1

if __name__=='__main__':raise SystemExit(main())

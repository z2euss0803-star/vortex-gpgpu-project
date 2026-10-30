"""Verify a canonical clean Git archive inventory, independent of checkout paths."""
import hashlib,json,re
from pathlib import Path
def canonical(value):return (json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode()
def verify_snapshot(root,inventory):
 root=Path(root)
 if inventory.get('schema')!=1 or not re.fullmatch('[0-9a-f]{40}',inventory.get('source_commit','')) or not inventory.get('files'):raise ValueError('SOURCE_INVENTORY_SCHEMA')
 for name,want in inventory['files'].items():
  if not name or name.startswith('/') or '\\' in name or any(p in ('','..','.') for p in name.split('/')):raise ValueError('SOURCE_PATH')
  p=root
  for component in name.split('/'):
   p=p/component
   if p.is_symlink():raise ValueError('SOURCE_SYMLINK:'+name)
  b=p.read_bytes()
  if len(b)!=want['bytes'] or hashlib.sha256(b).hexdigest()!=want['sha256'] or hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()!=want['git_blob']:raise ValueError('SOURCE_SNAPSHOT_DRIFT:'+name)
 return dict(source_commit=inventory['source_commit'],source_digest=hashlib.sha256(canonical(inventory)).hexdigest())

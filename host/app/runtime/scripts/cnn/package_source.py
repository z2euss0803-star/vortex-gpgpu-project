"""Snapshot exact tracked bytes with SOURCE_COMMIT; no build/evidence/.git payload."""
import argparse,hashlib,io,json,sys,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
from source_identity import source_commit,source_tree_clean,git_bytes
def package(output):
 if not source_tree_clean(ROOT):raise ValueError('Dirty tracked source')
 if output.exists():raise ValueError('Keep prior package')
 commit=source_commit(ROOT);names=[p for p in git_bytes(ROOT,'ls-files','-z').decode().split('\0') if p]
 inventory={};buf=io.BytesIO()
 with tarfile.open(fileobj=buf,mode='w:gz') as tar:
  for name in names:
   data=(ROOT/name).read_bytes();inventory[name]=hashlib.sha256(data).hexdigest()
   entry=tarfile.TarInfo('vxl-source/'+name);entry.size=len(data);entry.mode=0o644;tar.addfile(entry,io.BytesIO(data))
  for name,data in [('SOURCE_COMMIT',(commit+'\n').encode()),('SOURCE_SHA256.json',(json.dumps(inventory,indent=2)+'\n').encode())]:
   entry=tarfile.TarInfo('vxl-source/'+name);entry.size=len(data);entry.mode=0o644;tar.addfile(entry,io.BytesIO(data))
 output.write_bytes(buf.getvalue());print(commit,len(names),hashlib.sha256(output.read_bytes()).hexdigest())
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--out',required=True);o=a.parse_args();package(Path(o.out))

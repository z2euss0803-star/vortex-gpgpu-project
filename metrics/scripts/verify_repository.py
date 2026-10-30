from pathlib import Path
import hashlib, json, re, sys, zipfile
root=Path(__file__).resolve().parents[2]
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
errors=[]
manifest=json.loads((root/'docs/package-manifest.json').read_text(encoding='utf8'))
for row in manifest['files']:
 p=root/row['path']
 if not p.is_file() or p.stat().st_size!=row['bytes'] or digest(p)!=row['sha256']:errors.append('file identity: '+row['path'])
source=json.loads((root/'host/app/source-lock-r13.json').read_text(encoding='utf8'))
identity=json.loads((root/'docs/release-identity.json').read_text(encoding='utf8'))
relocated=identity.get('source_metadata_relocations',{})
for row in source['snapshot']['files']:
 p=root/relocated[row['path']] if row['path'] in relocated else root/'host/app/runtime'/row['path']
 if not p.is_file() or digest(p)!=row['sha256']:errors.append('R13 source: '+row['path'])
for package in (root/'host/model/packages').iterdir():
 if package.is_dir():
  m=json.loads((package/'manifest.json').read_text(encoding='utf8'))
  for a in m.get('assets',[]):
   p=package/a['path']
   if not p.is_file() or digest(p)!=a['sha256']:errors.append('model asset: '+str(p.relative_to(root)))
for r in ['README.md','docs/architecture.md','docs/interface.md','docs/hardware_setup.md','fpga/src','fpga/build','fpga/report','host/model/MODEL.md','host/app','host/deploy','metrics/metrics.csv','metrics/logs','metrics/scripts','metrics/evidence','skill/README.md','report/design.md']:
 if not (root/r).exists():errors.append('required: '+r)
for p in root.rglob('*'):
 if p.is_file() and '.git' not in p.parts and p.stat().st_size>=100*1024*1024:errors.append('GitHub file limit: '+str(p.relative_to(root)))
for p in [root/'README.md',*(root/'docs').glob('*.md')]:
 for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)',p.read_text(encoding='utf8')):
  if not link.startswith(('http:','https:','#')) and not (p.parent/link.split('#')[0]).exists():errors.append('link: '+str(p.relative_to(root))+' '+link)
if errors:print(json.dumps({'errors':errors},ensure_ascii=False,indent=2));sys.exit(1)
print(json.dumps({'status':'PACKAGE_HASH_AND_STRUCTURE_VERIFIED','files':len(manifest['files']),'frozen_source_files':len(source['snapshot']['files']),'models':9,'R13_board':'NOT_RUN'},ensure_ascii=False))

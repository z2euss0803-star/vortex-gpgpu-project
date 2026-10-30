"""Deterministic source archive; excludes evidence/build/.git and private files."""
import hashlib,json,pathlib,tarfile
S=pathlib.Path(__file__).resolve().parents[1]
dst=S/'evidence/20261009-takeover/source.tar.gz'
files=[p for p in S.rglob('*') if p.is_file() and not any(x in p.relative_to(S).parts for x in ('.git','.toolchain','build','evidence','__pycache__'))]
with tarfile.open(dst,'w:gz') as tar:
 for p in sorted(files): tar.add(p,arcname=str(p.relative_to(S)).replace('\\','/'))
manifest={str(p.relative_to(S)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(dst.parent/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(hashlib.sha256(dst.read_bytes()).hexdigest())

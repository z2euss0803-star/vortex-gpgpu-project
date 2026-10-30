import hashlib,json,pathlib,difflib
ROOT=pathlib.Path(__file__).resolve().parents[1]
m=json.loads((ROOT/'evidence/20261009-takeover/inputs.json').read_text(encoding='utf-8'))
results=[]
for r in m['sources']:
    p=pathlib.Path(r['source']);h=hashlib.sha256(p.read_bytes()).hexdigest()
    results.append(dict(path=str(p),sha256=h,unchanged=h==r['sha256']))
    assert h==r['sha256'],str(p)
patch=[]
for r in m['sources']:
    if r['copy'].startswith('vendor/vortex/runtime/stub/'):
        original=pathlib.Path(r['source']).read_text(encoding='utf-8').splitlines(keepends=True)
        current=(ROOT/r['copy']).read_text(encoding='utf-8').splitlines(keepends=True)
        patch.extend(difflib.unified_diff(original,current,fromfile='original/'+r['copy'],tofile=r['copy']))
(ROOT/'docs/vortex-stub.patch').write_text(''.join(patch),encoding='utf-8')
(ROOT/'evidence/20261009-takeover/protected-inputs.json').write_text(json.dumps({'count':len(results),'all_unchanged':True,'files':results},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('PROTECTED_ORIGINAL_INPUTS_UNCHANGED',len(results))

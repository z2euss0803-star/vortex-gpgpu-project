"""Content-valid semantic attacks against the full frozen Mosaic model gate."""
import json,pathlib,hashlib
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':')).encode()
def sha(x):return hashlib.sha256(x).hexdigest()
def verify(model,elf,out,run):
 model=pathlib.Path(model);out=pathlib.Path(out);out.mkdir();names=['weight-view','coeff-view','weights-original','scale','job-count','threshold','history-waiver','formal-reference','wrong-residual']
 base={p.name:p.read_bytes() for p in model.iterdir()};members=set(base)
 for name in names:
  case=out/name;case.mkdir();copies=case/'package';copies.mkdir();changed={};graph=json.loads(base['graph.json']);numeric=json.loads(base['numeric-contract.json'])
  if name in ('weight-view','coeff-view','weights-original','formal-reference'):
   path=next(n for n in sorted(base) if n.endswith('-q16.bin' if name=='weight-view' else '-coeff.bin')) if name in ('weight-view','coeff-view') else 'weights-original.ms8' if name=='weights-original' else 'formal-history.json'
   data=bytearray(base[path]);data[100 if name=='formal-reference' else 0]^=1;changed[path]=bytes(data)
   if name=='formal-reference':numeric['reference_sha']=sha(changed[path]);changed['numeric-contract.json']=wire(numeric)
  if name=='scale':graph['nodes'][0]['parameters']['scale_bits']^=1;changed['graph.json']=wire(graph)
  if name=='job-count':graph['nodes'][1]['job_count']-=1;changed['graph.json']=wire(graph)
  if name=='threshold':numeric['threshold_sha']='0'*64;changed['numeric-contract.json']=wire(numeric)
  if name=='history-waiver':numeric['known_failures']=[];changed['numeric-contract.json']=wire(numeric)
  if name=='wrong-residual':
   add=next(n for n in graph['nodes'] if n['operator']['name']=='Mosaic.Add.v1');save=next(n for n in graph['nodes'] if n['parameters']['flags']==1);later=graph['nodes'][save['id']+3];assert later['operator']['name']=='Mosaic.Relu.v1';add['inputs'][1]=later['outputs'][0];changed['graph.json']=wire(graph)
  manifest=json.loads(base['manifest.json']);manifest.pop('model_id')
  for path,raw in changed.items():
   row=next(x for x in manifest['assets'] if x['path']==path);row.update(bytes=len(raw),sha256=sha(raw))
  manifest['model_id']=sha(wire(manifest));data={**base,**changed,'manifest.json':wire(manifest)}
  for path,raw in data.items():(copies/path).write_bytes(raw);(copies/path).chmod(0o400)
  copies.chmod(0o500);proof=dict(name=name,model_id=manifest['model_id'],mutations={p:dict(bytes=len(r),sha256=sha(r)) for p,r in changed.items()},scope='EXPECTED_MODEL_SEMANTIC_REFUSAL_NOT_GPU',gpu_raw_cycles=None)
  (case/'proof.json').write_bytes(wire(proof));(case/'manifest.json').write_bytes(wire(manifest))
  for path,raw in changed.items():
   if path.endswith('.json'): (case/path).write_bytes(raw)
  assert 'U13_MOSAIC_MODEL_SEMANTIC_REJECTION_PASS' in run([elf,copies,'semantic-reject'])
  # Only this successful, fully verified test duplicate is released. The frozen
  # base package, mutant manifest and every mutation digest remain evidence.
  assert copies.parent.resolve()==case.resolve() and set(p.name for p in copies.iterdir())==members
  copies.chmod(0o700)
  for p in copies.iterdir():assert p.is_file() and not p.is_symlink() and p.read_bytes()==data[p.name];p.chmod(0o600);p.unlink()
  copies.rmdir()
 return len(names)

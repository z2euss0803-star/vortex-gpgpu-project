"""Valid, content-rehashed hostile packages, not loader corruption tests.

Each expected rejection retains manifest/changed metadata and exact mutations.
Successful test-only duplicate payloads are released after command evidence;
on first unexpected outcome the entire failing package stays for archival.
"""
import hashlib,json,pathlib,shutil,stat
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
def sha(x):return hashlib.sha256(x).hexdigest()
def verify(model,elf,out,run):
 out=pathlib.Path(out);out.mkdir();model=pathlib.Path(model)
 for name in ['cpu-program','cpu-gpu-job','weight-byte','threshold','known-failure-waiver','reference-byte','input-byte']:
  case=out/name;case.mkdir();copies=case/'package';copies.mkdir()
  for f in model.iterdir():
   target=copies/f.name;target.write_bytes(f.read_bytes())
  changed={};graph=json.loads((copies/'graph.json').read_text());numeric=json.loads((copies/'numeric-contract.json').read_text())
  if name in ('cpu-program','cpu-gpu-job'):
   cpu=next(n for n in graph['nodes'] if n['operator']['name']=='Hunet.GeluErf4.v1');cpu['program_requirements']=graph['nodes'][0]['program_requirements']
   if name=='cpu-gpu-job':cpu['job_count']=1
   changed['graph.json']=wire(graph)
  elif name=='weight-byte':
   path=sorted(p.name for p in copies.iterdir() if p.name.startswith('weight-'))[0];raw=bytearray((copies/path).read_bytes());raw[0]^=1;changed[path]=bytes(raw)
  elif name=='threshold':numeric['threshold_sha']='0'*64;changed['numeric-contract.json']=wire(numeric)
  elif name=='known-failure-waiver':numeric['known_failures']=[];changed['numeric-contract.json']=wire(numeric)
  else:
   path='reference.f32' if name=='reference-byte' else 'input.f32';raw=bytearray((copies/path).read_bytes());raw[0]^=1;changed[path]=bytes(raw)
   if name=='reference-byte':numeric['reference_sha']=sha(changed[path]);changed['numeric-contract.json']=wire(numeric)
  manifest=json.loads((copies/'manifest.json').read_text());manifest.pop('model_id')
  for path,raw in changed.items():
   (copies/path).write_bytes(raw)
   row=next(r for r in manifest['assets'] if r['path']==path);row.update(bytes=len(raw),sha256=sha(raw))
  manifest['model_id']=sha(wire(manifest));(copies/'manifest.json').write_bytes(wire(manifest))
  for f in copies.iterdir():f.chmod(0o400)
  copies.chmod(0o500)
  (case/'manifest.json').write_bytes(wire(manifest));proof=dict(case=name,model_id=manifest['model_id'],base_model_id=json.loads((model/'manifest.json').read_text())['model_id'],mutations={p:dict(bytes=len(b),sha256=sha(b)) for p,b in changed.items()},scope='EXPECTED_SEMANTIC_REFUSAL_NOT_GPU',gpu_raw_cycles=None)
  for path,raw in changed.items():
   if path.endswith('.json'):(case/path).write_bytes(raw)
  (case/'mutations.json').write_bytes(wire(proof))
  assert 'U12_MODEL_SEMANTIC_REJECT_PASS' in run([elf,copies,'semantic-reject'])
  # Scoped successful unit-test duplicates only; base package and case proof
  # are retained. Revalidate every member against the frozen base/mutation.
  copies.chmod(0o700)
  for f in copies.iterdir():
   assert f.is_file() and not f.is_symlink() and f.name in set(p.name for p in model.iterdir())
   expected=wire(manifest) if f.name=='manifest.json' else changed.get(f.name,(model/f.name).read_bytes())
   assert f.read_bytes()==expected;f.chmod(0o600);f.unlink()
  copies.rmdir()
 return 7

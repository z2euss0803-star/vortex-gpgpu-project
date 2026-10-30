"""Copy frozen inputs to a private read-only loader fixture; never regenerate references."""
import hashlib,json,pathlib,shutil,sys
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
for name,source in [('A',root/'data/nnrt/packages/int16-mlp-a'),('B',root/'tests/nnrt/fixtures/external-model-b')]:
 target=out/name;shutil.copytree(str(source),str(target))
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
def negative(name,file,change):
 target=out/name;shutil.copytree(str(root/'data/nnrt/packages/int16-mlp-a'),str(target))
 p=target/file;v=json.loads(p.read_text());change(v);p.write_bytes(wire(v))
 manifest=json.loads((target/'manifest.json').read_text());manifest.pop('model_id')
 for row in manifest['assets']:
  b=(target/row['path']).read_bytes();row.update(bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
 manifest['model_id']=hashlib.sha256(wire(manifest)).hexdigest();(target/'manifest.json').write_bytes(wire(manifest))
negative('bad-shape','graph.json',lambda g:g['tensors'][0].update(shape=[7,8]))
negative('bad-layout','graph.json',lambda g:g['tensors'][2].update(layout='PACKED_I48'))
negative('bad-dtype','graph.json',lambda g:g['tensors'][0].update(dtype='FP32'))
negative('bad-domain','graph.json',lambda g:g['tensors'][0].update(domain='CPU'))
negative('bad-version','graph.json',lambda g:g['nodes'][0]['operator'].update(version='2.0.0'))
negative('bad-code','program-requirements.json',lambda g:g['programs'][0].update(code='ee'*32))
negative('bad-schema','program-requirements.json',lambda g:g['programs'][0].update(schema='ee'*32))
negative('old-program','program-requirements.json',lambda g:g['programs'][0].update(profile=4,program=9))
negative('no-job','graph.json',lambda g:g['nodes'][0].update(job_count=0))
negative('burst-jobs','graph.json',lambda g:g['nodes'][0].update(job_count=2))
negative('numeric-waiver','numeric-contract.json',lambda g:g.update(known_failures=['waive_integer_error']))
negative('bad-threshold','numeric-contract.json',lambda g:g.update(threshold_sha='ab'*32))
negative('bad-reference','numeric-contract.json',lambda g:g.update(reference_sha='ab'*32))
def legacy48(g):
 g['tensors']=[dict(id=i,dtype='I48',layout='PACKED_I48',shape=[8,8],domain='PL',access=3,alignment=64,constant=False) for i in (1,3)]
 g['nodes']=[dict(id=10,operator=dict(name='test.legacy48',version='1.0.0',implementation_sha='77'*32),inputs=[1],outputs=[3],parameters={},job_count=1,evidence_bytes=64,program_requirements=g['nodes'][0]['program_requirements'])];g['inputs']=[1];g['outputs']=[3]
negative('legacy-packed48','graph.json',legacy48)
for target in out.iterdir():
 if target.is_dir():
  for p in target.iterdir():p.chmod(0o444)
  target.chmod(0o500)
(out/'.ready').write_text('FROZEN_REFERENCE_COPY_NOT_GPU\n')

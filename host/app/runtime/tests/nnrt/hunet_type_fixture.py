"""Independent half-precision storage declarations; not a GPU program/model PASS."""
import copy,hashlib,json,pathlib,struct,sys
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
def wire(v):return json.dumps(v,sort_keys=True,separators=(',',':')).encode()
def sha(v):return hashlib.sha256(v).hexdigest()
for name in ('fp16','i16-mislabel','wrong-layout','short-constant'):
 tensors=[dict(id=i,dtype='FP16',layout='RM_FP16',shape=[2,4],domain='PL',access=1 if i==1 else 3,alignment=64,constant=i==1) for i in (1,2)]
 tensors[0]['asset']='weights.f16'
 if name=='i16-mislabel':
  for t in tensors:t['dtype']='I16'
 if name=='wrong-layout':tensors[0]['layout']='RM_I16'
 graph=dict(schema=1,tensors=tensors,nodes=[dict(id=10,operator=dict(name='test.fp16.storage',version='1.0.0',implementation_sha='aa'*32),inputs=[1],outputs=[2],parameters={},job_count=1,evidence_bytes=64,program_requirements=['11'*32])],inputs=[],outputs=[2],shape_profiles=[])
 programs=dict(schema=1,hardware_contract='22'*32,bit='33'*32,xsa='44'*32,compatible='55'*32,programs=[dict(namespace=1,profile=1,program=65552,code='11'*32,schema='66'*32)])
 numeric=dict(schema=1,reference_sha='77'*32,threshold_sha='88'*32,semantics='STORAGE_FIXTURE_NOT_NUMERIC_PASS',known_failures=[])
 # IEEE half patterns are frozen literal data, never synthesized as signed I16.
 half=struct.pack('<8H',0,0x8000,0x3c00,0xbc00,0x7bff,1,0x3e00,0x0400)
 if name=='short-constant':half=half[:-2]
 data={'graph.json':wire(graph),'program-requirements.json':wire(programs),'numeric-contract.json':wire(numeric),'weights.f16':half}
 roles={'graph.json':'graph','program-requirements.json':'programs','numeric-contract.json':'numeric','weights.f16':'weights'}
 m=dict(schema=1,max_jobs=1,max_evidence_bytes=64,assets=[dict(path=p,bytes=len(b),sha256=sha(b),type='json' if p.endswith('.json') else 'binary',role=roles[p]) for p,b in sorted(data.items())]);m['model_id']=sha(wire(m));data['manifest.json']=wire(m)
 dest=out/name;dest.mkdir()
 for p,b in data.items():(dest/p).write_bytes(b);(dest/p).chmod(0o444)
 dest.chmod(0o500)
(out/'.ready').write_text('INDEPENDENT_FP16_STORAGE_FIXTURE_NOT_APPLICATION\n')

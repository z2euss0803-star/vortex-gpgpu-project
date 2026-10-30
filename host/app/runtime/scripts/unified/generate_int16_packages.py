"""Freeze tiny integer networks independently of production parser/math.

Program bytes must already exist with an audited RV32 identity. This generator
never compiles/executes a GPU, imports production planner or changes a kernel.
"""
import hashlib,json,pathlib,struct
ROOT=pathlib.Path(__file__).resolve().parents[2]
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf8')
def sha(x):return hashlib.sha256(x).hexdigest()
def source_bytes(root,name):
 raw=(root/name).read_bytes()
 # Own text is checked out with eol=lf. Never bind transient Windows CRLF
 # bytes; immutable vendor inputs retain their exact original representation.
 if not name.startswith('vendor/') and b'\r' in raw:raise ValueError('SOURCE_CHECKOUT_LF '+name)
 return raw
def make_schema():
 rules=[]
 def scalar(off,lo,hi,words=1):
  a=[0]*16;a[0]=1;a[1]=off;a[10]=words*4;a[11]=lo;a[12]=hi;rules.append(a)
 def address(off,index,access,alignment,count):
  a=[0]*16;a[:10]=[2,off,index,access,alignment,1,count,0xffffffff,0xffffffff,0xffffffff];rules.append(a)
 scalar(0,1,1);scalar(4,128,128);scalar(8,65537,65539);scalar(12,8,64);scalar(16,8,64);scalar(20,0,64)
 address(24,0,1,2,36);address(28,1,1,2,40);address(32,2,2,8,44)
 for off in (36,40,44):scalar(off,1,32768)
 scalar(48,0,0xffffffff,10)
 a=[0]*16;a[0]=3;a[1]=88;a[10]=40;rules.append(a)
 header=[0x534c5856,1,64+len(rules)*64,128,len(rules),0x80000000,0,0x10002000,4096,0xfff00000,0x100000,1,0,0,0,0]
 return struct.pack('<16I',*header)+b''.join(struct.pack('<16I',*a) for a in rules)
def generate():
 program=ROOT/'data/nnrt/programs/int16-family-v1';original=json.loads((program/'rv32-build.json').read_text());code=(program/'code.bin').read_bytes();assert sha(code)==original['flat']['sha256'] and len(code)==65536
 for name,digest in original['sources'].items():assert sha(source_bytes(ROOT,name))==digest,'RV32 source changed since audited build: '+name
 schema=make_schema();(program/'args-schema.bin').write_bytes(schema)
 source_names=['include/vxnn/ops_int16.hpp','include/vxnn/package.hpp','nnrt/ops/int16.cpp','nnrt/graph/prepare.cpp','contracts/nnrt/ops-int16-v1.json','scripts/unified/generate_int16_packages.py']+sorted(original['sources'])
 source_rows=[dict(path=n,sha256=sha(source_bytes(ROOT,n))) for n in sorted(set(source_names))];implementation=sha(wire(source_rows))
 provider=next(p for p in json.loads((ROOT/'contracts/unified/providers.json').read_text())['providers'] if p['name']=='S8_NATIVE_INT16')
 family=dict(schema=1,code_sha256=sha(code),schema_sha256=sha(schema),implementation_sha256=implementation,source_rows=source_rows,hardware=provider,scope='FROZEN_SOFTWARE_AND_DECLARED_HARDWARE_NOT_PHYSICAL_PASS',gpu_raw_cycles=None)
 (program/'family.json').write_bytes(wire(family))
 def key(name):return sha(('VXNN_INT16_OP:1:'+name+':'+implementation+':'+family['code_sha256']).encode())
 def tensor(i,dtype,layout,shape,constant=False,asset=None):
  t=dict(id=i,dtype=dtype,layout=layout,shape=shape,domain='PL',access=1 if i==1 or constant else 3,alignment=64,constant=constant)
  if asset:t['asset']=asset
  return t
 def node(i,op,ins,out):return dict(id=i,operator=dict(name=op,version='1.0.0',implementation_sha=key(op)),inputs=ins,outputs=[out],parameters={},job_count=1,evidence_bytes=1024,program_requirements=[family['code_sha256']])
 def package(path,m,relu):
  k=n=8
  a=[((i*3571+12345)%65536)-32768 for i in range(m*k)];w=[((i*7919+43)%65536)-32768 for i in range(k*n)];a[0]=w[0]=-32768;a[-1]=32767
  bias=[-2147483648,2147483647,-1,1,0,27,-31,6]
  expected=[sum(a[r*k+t]*w[t*n+c] for t in range(k))+bias[c] for r in range(m) for c in range(n)]
  if relu:expected=[max(x,0) for x in expected]
  assert all(-(1<<47)<=x<(1<<47) for x in expected)
  tensors=[tensor(1,'I16','RM_I16',[m,k]),tensor(2,'I16','RM_I16',[k,n],True,'weights.bin'),tensor(3,'I48','RM_I48S64',[m,n]),tensor(4,'I32','ROW_I32',[n],True,'bias.bin'),tensor(5,'I48','RM_I48S64',[m,n])]
  nodes=[node(10,'MatMulI16.v1',[1,2],3),node(11,'BiasI48.v1',[3,4],5)]
  if relu:tensors.append(tensor(6,'I48','RM_I48S64',[m,n]));nodes.append(node(12,'ReLUI48.v1',[5],6))
  graph=dict(schema=1,tensors=tensors,nodes=nodes,inputs=[1],outputs=[6 if relu else 5],shape_profiles=[])
  programs=dict(schema=1,hardware_contract=provider['hardware_contract'],bit=provider['bit_sha256'],xsa=provider['xsa_sha256'],compatible=sha(provider['compatible'].encode()),programs=[dict(namespace=1,profile=1,program=65537,code=family['code_sha256'],schema=family['schema_sha256'])])
  reference=struct.pack('<%dq'%len(expected),*expected);threshold=sha(wire(dict(exact_integer=True,max_absolute_error=0)))
  numeric=dict(schema=1,reference_sha=sha(reference),threshold_sha=threshold,semantics='W16A16_NATIVE_I48_SIGNEXT64_EXACT_V1',known_failures=[])
  data={'graph.json':wire(graph),'program-requirements.json':wire(programs),'numeric-contract.json':wire(numeric),'weights.bin':struct.pack('<%dh'%len(w),*w),'bias.bin':struct.pack('<8i',*bias),'input.bin':struct.pack('<%dh'%len(a),*a),'reference.i64':reference}
  roles={'graph.json':'graph','program-requirements.json':'programs','numeric-contract.json':'numeric','weights.bin':'weights','bias.bin':'weights','input.bin':'input','reference.i64':'reference'}
  manifest=dict(schema=1,max_jobs=16,max_evidence_bytes=65536,assets=[dict(path=p,bytes=len(b),sha256=sha(b),type='json' if p.endswith('.json') else 'binary',role=roles[p]) for p,b in sorted(data.items())]);manifest['model_id']=sha(wire(manifest));data['manifest.json']=wire(manifest)
  path.mkdir(parents=True,exist_ok=True)
  for name,b in data.items():(path/name).write_bytes(b)
  return dict(path=str(path.relative_to(ROOT)),model_id=manifest['model_id'],M=m,N=n,K=k,relu=relu,jobs=len(nodes),input_sha256=sha(data['input.bin']),reference_sha256=sha(reference),scope='INDEPENDENT_PYTHON_INTEGER_REFERENCE_NOT_GPU',gpu_raw_cycles=None)
 rows=[package(ROOT/'data/nnrt/packages/int16-mlp-a',8,False),package(ROOT/'tests/nnrt/fixtures/external-model-b',16,True)]
 (program/'models.json').write_bytes(wire(rows));print(json.dumps(dict(models=rows,code_sha256=family['code_sha256'],schema_sha256=family['schema_sha256'],implementation_sha256=implementation,gpu_raw_cycles=None)))
if __name__=='__main__':generate()

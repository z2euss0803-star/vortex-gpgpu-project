"""Materialize actual frozen HUNet weights and SSA graph; never execute GPU.

Historical addresses only locate bytes/old producers during migration. They
are not emitted as runtime tensor addresses. Reused GN scratch becomes SSA.
"""
import hashlib,json,pathlib,re,struct,sys
from scripts.unified.hunet_program import selected_directory,program_source_paths,resolve_variant
ROOT=pathlib.Path(__file__).resolve().parents[2]
WEIGHTS_SHA='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc'
NAMES={4:'GnStats',5:'GnApply',6:'GeluErf4',7:'Shift',8:'Add',9:'Gap',10:'Sigmoid',11:'MulChannel',12:'Slice',13:'Concat',14:'Copy',15:'FusedConv'}
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf8')
def sha(x):return hashlib.sha256(x).hexdigest()
def implementation_source_rows(root,*,program_variant=None):
 """Exact host implementation inputs for the explicitly selected program."""
 root=pathlib.Path(root);resolve_variant(program_variant)
 names=['include/vxnn/hunet_registry.hpp','nnrt/ops/hunet_registry.cpp','nnrt/ops/hunet.cpp','include/vxnn/ops_hunet.hpp','nnrt/models/hunet.cpp','include/vxnn/model_hunet.hpp','scripts/unified/generate_hunet_package.py','device/vxnn/hunet_wire.h','device/vxnn/hunet_wire.cpp','contracts/nnrt/ops-hunet-v1.json','core/hunet_sha256.c']
 # The host implementation key includes transitive CPU math, graph and shared
 # budget code, not just the forwarding wrapper. Device math is separately
 # bound by the audited CODE/schema digests above.
 for folder in ['nnrt/cpu','nnrt/package','nnrt/graph','unified/scheduler']:
  names += [str(p.relative_to(root)).replace('\\','/') for p in sorted((root/folder).glob('*.cpp'))]
 names += ['include/vxnn/'+n+'.hpp' for n in ['cpu','backend','types','package','graph']]
 names += ['include/vxunified/'+n+'.hpp' for n in ['scheduler','work','types']]
 names += ['unified/hunet_request.cpp','include/vxunified/hunet_request.hpp','unified/service/native_nn.cpp','vendor/hunet/v5/include/hunet_ordered_reductions.h','vendor/hunet/v5/include/hunet_ops_baseline.h']
 names += ['include/vxunified/hunet_page_identity.hpp','unified/service/hunet_page_native.hpp','include/vxnn/request_input.hpp','include/vxnn/arena.hpp','include/vxnn/arena_storage.hpp','include/vxnn/arena_guard.hpp']
 names += [str(p.relative_to(root)).replace('\\','/') for p in sorted((root/'nnrt/memory').glob('*.cpp'))]
 names += ['scripts/unified/hunet_program.py','scripts/unified/program_identity.py']+program_source_paths(program_variant)
 rows=[];own_new=set(names[:10])
 for name in names:
  raw=(root/name).read_bytes()
  # Existing frozen dependencies include CRLF: preserve and bind exact bytes.
  # Do not rewrite them or silently normalize bytes to claim a tested SHA.
  if name in own_new and b'\r' in raw:raise ValueError('SOURCE_CHECKOUT_LF '+name)
  rows.append(dict(path=name,sha256=sha(raw),newline='CRLF_RAW' if b'\r\n' in raw else 'LF_RAW'))
 return rows

def generate(weights_path,out,provider_flag=1,*,cpu_gn_stats_nodes=()):
 allowed_stats={2,9,19,25,32,42,52,55,62,69,76,83,90,97,100,107}
 if type(cpu_gn_stats_nodes) not in (list,tuple) or any(type(n) is not int for n in cpu_gn_stats_nodes) or len(set(cpu_gn_stats_nodes))!=len(cpu_gn_stats_nodes) or not set(cpu_gn_stats_nodes)<=allowed_stats:raise ValueError('EXPLICIT_UNIQUE_FROZEN_GN_STATS_NODE_SUBSET_REQUIRED')
 cpu_stats=set(cpu_gn_stats_nodes)
 weights=pathlib.Path(weights_path).read_bytes()
 if len(weights)!=23685056 or sha(weights)!=WEIGHTS_SHA:raise ValueError('FROZEN_WEIGHTS_IDENTITY')
 out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
 family_dir=selected_directory(ROOT);family=json.loads((family_dir/'family.json').read_text())
 for name,key in [('code.bin','code_sha256'),('args-schema.bin','schema_sha256')]:
  if sha((family_dir/name).read_bytes())!=family[key]:raise ValueError('FROZEN_PROGRAM_IDENTITY')
 rows=implementation_source_rows(ROOT)
 impl=sha(wire(sorted(rows,key=lambda x:x['path'])))
 provider=next(p for p in json.loads((ROOT/'contracts/unified/providers.json').read_text())['providers'] if type(provider_flag) is int and provider_flag in (1,4) and p['provider_flags']==provider_flag)
 payload={};roles={};tensors=[];nodes=[];current={};views={};audit=[]
 def asset(name,raw,role):payload[name]=raw;roles[name]=role;return name
 def tensor(dtype,layout,shape,constant=False,source=None):
  t=dict(id=len(tensors)+1,dtype=dtype,layout=layout,shape=shape,domain='PL',access=1 if constant else 3,alignment=64,constant=constant)
  if source:t['asset']=source
  tensors.append(t);return t['id']
 def constant(address,dtype,layout,shape):
  size={'FP16':2,'FP32':4}[dtype]
  for n in shape:size*=n
  key=(address,dtype,layout,tuple(shape))
  if key not in views:
   offset=address-0x80800000
   if offset<0 or offset+size>len(weights) or offset%64:raise ValueError('WEIGHT_RANGE')
   name='weight-%08x-%d.bin'%(offset,size);raw=weights[offset:offset+size]
   if name in payload and payload[name]!=raw:raise ValueError('WEIGHT_ALIAS')
   asset(name,raw,'weights');views[key]=tensor(dtype,layout,shape,True,name)
  return views[key]
 old=[]
 for line in (ROOT/'generated/hunet/catalog.h').read_text().splitlines():
  if line.startswith('{0x'):old.append([int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)u',line)])
 if len(old)!=1023:raise ValueError('OLD_LOGICAL_CATALOG')
 jobs=list(struct.iter_unpack('<32I',(ROOT/'generated/hunet_fused/jobs.bin').read_bytes()))
 sequence=sorted(jobs+[j for j in old if j[3]==6],key=lambda j:j[4])
 desc=(ROOT/'generated/hunet_fused/descriptors.bin').read_bytes()
 first=tensor('FP32','CHW_F32',[3,128,128]);current[0x85000040]=first;last_sequence=0
 for index,j in enumerate(sequence):
  op=j[3];start=j[4];end=j[5] if op==15 else start
  if start!=last_sequence+1:raise ValueError('OLD_SEQUENCE_GAP')
  last_sequence=end;flags=0;hexdata='';ins=[current[j[8]]]
  if op==15:
   ci,co,ih,iw,oh,ow,k,stride,pad,flags=j[18:28];flags|=16 if j[10] else 0
   params=[ci,co,ih,iw,oh,ow,k,stride,pad,0,0,0];base=j[14]-0x86d0df80;raw=desc[base:base+j[7]*64]
   if base<0 or len(raw)!=j[7]*64:raise ValueError('DESCRIPTOR_RANGE')
   d=struct.unpack_from('<16I',raw);ins.append(constant(j[9],'FP16','RM_FP16_PAD8',[d[3],d[5]]))
   if j[10]:ins.append(constant(j[10],'FP32','ROW_F32',[d[0]]))
   name=asset('desc-%03d.bin'%index,raw,'weights');ins.append(tensor('U8','DESC64_BYTES',[len(raw)],True,name));hexdata=raw.hex();address=j[11]
  else:
   ci,co,ih,iw,oh,ow,k=j[16:23];stride,pad,groups,n_div,eps=j[24:29]
   params=[ci,co,ih,iw,oh,ow,k,stride,pad,groups,n_div,eps];address=j[13]
   if op==5:ins += [constant(j[11],'FP32','ROW_F32',[ci]),constant(j[12],'FP32','ROW_F32',[ci]),current[j[14]]]
   if op in (8,11,13):ins.append(current[j[9]])
  shape=[2] if op==4 else [co,oh,ow];layout='GN_MEAN_INVSTD_F32' if op==4 else 'CHW_F32'
  output=tensor('FP32',layout,shape);current[address]=output
  stats_cpu=op==4 and index+1 in cpu_stats
  name='Hunet.GnStatsCpuOrdered.v1' if stats_cpu else 'Hunet.'+NAMES[op]+'.v1';key=sha(('VXNN_HUNET_OP:1:'+name+':'+impl+':'+family['code_sha256']+':'+family['schema_sha256']).encode())
  nodes.append(dict(id=index+1,operator=dict(name=name,version='1.0.0',implementation_sha=key),inputs=ins,outputs=[output],parameters=dict(shape=params,flags=flags,descriptors=hexdata),job_count=0 if op==6 or stats_cpu else 1,evidence_bytes=4096,program_requirements=[] if op==6 or stats_cpu else [family['code_sha256']]))
  audit.append(dict(node=index+1,op=op,old_first=start,old_last=end))
 if last_sequence!=1023 or len(nodes)!=116 or sum(n['job_count'] for n in nodes)!=103-len(cpu_stats):raise ValueError('FROZEN_GRAPH_COUNTS')
 graph=dict(schema=1,tensors=tensors,nodes=nodes,inputs=[first],outputs=nodes[-1]['outputs'],shape_profiles=[])
 programs=dict(schema=1,hardware_contract=provider['hardware_contract'],bit=provider['bit_sha256'],xsa=provider['xsa_sha256'],compatible=sha(provider['compatible'].encode()),programs=[dict(namespace=1,profile=1,program=65552,code=family['code_sha256'],schema=family['schema_sha256'])])
 history=(ROOT/'tests/demo/fixtures/hunet/probe-result.json').read_bytes();reference=(ROOT/'tests/demo/fixtures/hunet/logits.f32').read_bytes();input_raw=(ROOT/'data/demo/hunet-input-roi0.f32').read_bytes()
 numeric=dict(schema=1,reference_sha=sha(reference),threshold_sha=sha(history),semantics='HUNET_V5_ORDERED_FP16_FP32_ERF4_FROZEN_HISTORY_V1',known_failures=['HISTORICAL_ORT_8105_NOT_NEW_PASS'])
 for n,value,role in [('graph.json',graph,'graph'),('program-requirements.json',programs,'programs'),('numeric-contract.json',numeric,'numeric')]:asset(n,wire(value),role)
 asset('weights-original.bin',weights,'provenance-source');asset('history.raw',history,'historical-numerics');asset('reference.f32',reference,'reference');asset('input.f32',input_raw,'input')
 binding=dict(cpu_gn_stats_nodes=sorted(cpu_stats),schema=1,code_sha256=family['code_sha256'],schema_sha256=family['schema_sha256'],implementation_sha256=impl,source_rows=rows,hardware=provider,old_weights_sha256=WEIGHTS_SHA,nodes=audit,diagnostics=False,gpu_raw_cycles=None,scope='FROZEN_GRAPH_PACKAGE_NOT_GPU_EXECUTION')
 asset('binding.json',wire(binding),'provenance')
 manifest=dict(schema=1,max_jobs=103-len(cpu_stats),max_evidence_bytes=1<<20,assets=[dict(path=n,bytes=len(raw),sha256=sha(raw),type='json' if n.endswith('.json') else 'binary',role=roles[n]) for n,raw in sorted(payload.items())]);manifest['model_id']=sha(wire(manifest));payload['manifest.json']=wire(manifest)
 for name,raw in payload.items():(out/name).write_bytes(raw)
 return dict(model_id=manifest['model_id'],nodes=116,gpu_jobs=103-len(cpu_stats),cpu_jobs=13+len(cpu_stats),constants=len(views)+35,bytes=sum(map(len,payload.values())),graph_bytes=len(payload['graph.json']),gpu_raw_cycles=None)
if __name__=='__main__':print(json.dumps(generate(sys.argv[1],sys.argv[2]),sort_keys=True))

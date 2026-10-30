"""Physical SSA rewrite of 15 exact NormApply/activation/Quant triples.

This is graph compilation only. It does not run inference or publish a CODE
identity. The caller must bind a separately built ABI2 program and schema.
"""
import copy,hashlib

def fuse(graph,audit,pointwise_budget,family):
 if type(pointwise_budget) is not int or not 1<=pointwise_budget<=4096:raise ValueError('bounded pointwise budget required')
 if len(graph['nodes'])!=77 or len(audit)!=77:raise ValueError('complete original77 phase graph required')
 result=copy.deepcopy(graph);nodes=[];rows=[];source=graph['nodes'];tensors={x['id']:x for x in graph['tensors']};index=0;fused=0
 while index<len(source):
  node=source[index];row=audit[index]
  if row['opcode']!=4:
   nodes.append(copy.deepcopy(node));rows.append(copy.deepcopy(row));index+=1;continue
  if index+2>=len(source):raise ValueError('missing activation/next quant producer')
  act,quant=source[index+1:index+3];act_row,qrow=audit[index+1:index+3]
  if act_row['opcode'] not in (5,6) or qrow['opcode']!=1 or qrow['layer']!=row['layer']+1 or len(node['inputs'])!=3 or len(node['outputs'])!=1 or act['inputs'][0]!=node['outputs'][0] or len(act['outputs'])!=1 or quant['inputs']!=act['outputs']:raise ValueError('original norm/activation/next-quant data dependencies required')
  if len(act['inputs'])!=(2 if act_row['opcode']==6 else 1) or len(quant['outputs'])!=(2 if quant['parameters']['flags']==1 else 1):raise ValueError('exact residual input/save arity required')
  norm_shape=tensors[node['inputs'][0]]['shape'];qshape=tensors[quant['outputs'][0]]['shape']
  if norm_shape!=qshape or tensors[quant['outputs'][0]]['dtype']!='I16' or node['parameters']['flags']!=2 or act['parameters']['flags']!=2:raise ValueError('fixed CHW geometry and separate original phases required')
  op=8 if act_row['opcode']==6 else 7;name='Mosaic.'+('NormAddQuant.v1' if op==8 else 'NormReluQuant.v1');key=hashlib.sha256(('VXNN_MOSAIC_OP:1:'+name+':'+family['implementation_sha256']+':'+family['code_sha256']+':'+family['readonly_sha256']+':'+family['schema_sha256']).encode()).hexdigest()
  merged=copy.deepcopy(node);merged['operator']=dict(name=name,version='1.0.0',implementation_sha=key);merged['inputs']=list(node['inputs'])+(act['inputs'][1:] if op==8 else []);merged['outputs']=list(quant['outputs']);merged['parameters']=dict(shape=list(node['parameters']['shape']),flags=quant['parameters']['flags'],scale_bits=quant['parameters']['scale_bits'],budget=pointwise_budget)
  if set(merged['inputs'])&set(merged['outputs']):raise ValueError('old skip and new save must remain distinct SSA tensors')
  items=(qshape[0]*qshape[1]*qshape[2]+255)//256;jobs=(items+pointwise_budget-1)//pointwise_budget;merged['job_count']=jobs;merged['evidence_bytes']=4096+jobs*512
  nodes.append(merged);rows.append(dict(phase=0,layer=row['layer'],opcode=op,items=items,jobs=jobs,budget=pointwise_budget,tail_items=items-(jobs-1)*pointwise_budget));fused+=1;index+=3
 if fused!=15 or len(nodes)!=47:raise ValueError('full15 fused triples and47 physical nodes required')
 for i,(node,row) in enumerate(zip(nodes,rows)):node['id']=i+1;row['phase']=i
 used=set(result['inputs']+result['outputs'])
 for node in nodes:used.update(node['inputs']);used.update(node['outputs'])
 result['nodes']=nodes;result['tensors']=[t for t in result['tensors'] if t['id'] in used]
 produced=set(result['inputs'])|{t['id'] for t in result['tensors'] if t['constant']}
 for node in nodes:
  if not set(node['inputs'])<=produced or set(node['outputs'])&produced:raise ValueError('fused SSA producer order/alias differs')
  produced.update(node['outputs'])
 if len(result['tensors'])!=100 or sum(r['opcode']==3 for r in rows)!=15 or sum(len(n['outputs'])==2 for n in nodes)!=5:raise ValueError('global stats barriers and five original FP32 residual saves required')
 return result,rows

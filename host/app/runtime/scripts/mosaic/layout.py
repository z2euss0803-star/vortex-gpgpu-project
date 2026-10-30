"""Static GPU32 capacity planning, not measured device allocation."""
import argparse,json,pathlib
from scripts.mosaic.contract import tagged
NAMES=('code','weights','fp_a','fp_b','q_input','skip','norm_tree','stats','tile_scratch')
def plan_layout(graph):
    if graph.get('version')!=1 or len(graph.get('layers',[]))!=16:raise ValueError('GRAPH')
    g=dict(graph);digest=g.pop('sha256',None)
    if tagged(g)!=digest:raise ValueError('GRAPH_SHA')
    layers=graph['layers']
    fp=max(max(_product(x['input_shape']),_product(x['output_shape']))*4 for x in layers)
    q=max(_product(x['input_shape'])*2 for x in layers)
    skip=max(_product(x['input_shape'])*4 for x in layers if x['save'])
    spatial=max(x['output_shape'][1]*x['output_shape'][2] for x in layers if x['norm'])
    tree=(1<<(spatial-1).bit_length())*8*4
    k=max(x['Kpad'] for x in layers)
    scratch=((2*8*k*2+64*8+384+32+63)//64*64)*4
    result=dict(version=1,graph_sha256=digest,scope='STATIC_CAPACITY_NOT_MEASURED_PEAK',width=graph['width'],height=graph['height'],regions=[])
    result['regions']=[dict(role='code',address=0x80000000,bytes=8*1024*1024,guard=0,readonly=True,lifetime=[0,15]),dict(role='weights',address=0x80800000,bytes=16*1024*1024,guard=0,readonly=True,lifetime=[0,15])]
    cursor=0x82000000
    for name,size in zip(NAMES[2:],(fp,fp,q,skip,tree,128*3*4,scratch)):
        size=(size+63)//64*64;cursor=(cursor+63)//64*64
        result['regions'].append(dict(role=name,address=cursor+64,bytes=size,guard=64,readonly=False,lifetime=[3,12] if name=='skip' else [0,15]))
        cursor+=size+128
    if cursor>0xfff00000:raise ValueError('STACK_OR_GPU32')
    result.update(end=cursor,dynamic_bytes=cursor-0x82000000)
    result['sha256']=tagged(result)
    return result

def _product(values):
    n=1
    for x in values:
        if type(x)!=int or not 0<x<=1280:raise ValueError('SHAPE')
        n*=x
    return n

def check_layout(layout):
    if layout.get('version')!=1 or len(layout.get('regions',[]))!=9:raise ValueError('LAYOUT')
    cursor=0x82000000
    for i,r in enumerate(layout['regions']):
        if r['role']!=NAMES[i] or type(r['address'])!=int or type(r['bytes'])!=int:raise ValueError('ROLE')
        address,size=r['address'],r['bytes']
        if size<=0 or size>=2**64 or address<0x80000000 or address>=2**32 or address%64 or size%64:raise ValueError('RANGE')
        if i<2:
            if address!=(0x80000000 if i==0 else 0x80800000) or size!=(8 if i==0 else 16)*1024*1024 or not r['readonly'] or r['guard']!=0:raise ValueError('READONLY')
        else:
            if r['guard']!=64 or r['readonly'] or address!=cursor+64 or size>0xfff00000-address-64:raise ValueError('GUARD_OR_OVERLAP')
            cursor=address+size+64
    if cursor!=layout['end'] or cursor-0x82000000!=layout['dynamic_bytes']:raise ValueError('TOTAL')
    value=dict(layout);expected=value.pop('sha256',None)
    if tagged(value)!=expected:raise ValueError('LAYOUT_SHA')
    return True

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--graph',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    value=plan_layout(json.loads(pathlib.Path(a.graph).read_text()));check_layout(value)
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=False);(out/'layout-v1.json').write_text(json.dumps(value,indent=2)+'\n')

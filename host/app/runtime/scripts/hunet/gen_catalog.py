"""Freeze original bytes; the C validator recomputes spans separately."""
import argparse,collections,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def generate_catalog(jobs:Path,matrices:Path,layout:Path,out:Path)->dict:
 raw=jobs.read_bytes();aux=matrices.read_bytes();plan=json.loads(layout.read_text(encoding='utf-8'))
 if hashlib.sha256(raw).hexdigest()!='6c3fbdaf6de98b170b813d0a2dab84f3bd76a1b22e580c55312f058a908c1765':raise ValueError('JOBS_IDENTITY')
 if hashlib.sha256(aux).hexdigest()!='8330485df5c42475c7729d037b9c68043e9a99707c7833fe20a4d8df23fee1ac':raise ValueError('AUX_IDENTITY')
 js=list(struct.iter_unpack('<32I',raw));ms=list(struct.iter_unpack('<8I',aux))
 if len(js)!=1023 or len(ms)!=314:raise ValueError('CATALOG_COUNT')
 if plan['regions']!=json.loads((ROOT/'contracts/hunet_v5.json').read_text(encoding='utf-8'))['graph']['layout']:raise ValueError('LAYOUT_IDENTITY')
 for i,j in enumerate(js):
  if j[4]!=i+1 or j[30]>99:raise ValueError('SEQUENCE_STAGE')
 lines=['#ifndef VXL_HUNET_CATALOG_H','#define VXL_HUNET_CATALOG_H',
        'static const vxl_u32 vxl_hunet_jobs[1023][32]={']
 lines+=['{'+','.join('0x%08xu'%v for v in j)+'},' for j in js];lines+=['};','static const struct vxl_hunet_matrix_v1 vxl_hunet_matrices[314]={']
 lines+=['{'+','.join('%du'%v for v in m)+'},' for m in ms];lines+=['};',
   'static const vxl_u8 vxl_hunet_aux_bytes[10048]={'+','.join('0x%02x'%v for v in aux)+'};','#endif']
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
 stages=[]
 for stage in range(100):
  members=[j for j in js if j[30]==stage];last=members[-1]
  slot=next(r for r in plan['regions'] if r['start']==last[13])
  stages.append(dict(stage_id=stage,last_sequence=last[4],gpu=last[13],bytes=32420 if stage==99 else slot['bytes']))
 report=dict(scope='STATIC_CATALOG_NOT_GPU',jobs=1023,matrices=314,stages=stages,op_counts=dict(collections.Counter(str(j[3]) for j in js)),jobs_sha256=hashlib.sha256(raw).hexdigest(),aux_sha256=hashlib.sha256(aux).hexdigest(),layout_sha256=hashlib.sha256(layout.read_bytes()).hexdigest())
 out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--jobs',required=True,type=Path);p.add_argument('--matrices',required=True,type=Path);p.add_argument('--layout',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args();generate_catalog(a.jobs,a.matrices,a.layout,a.out)

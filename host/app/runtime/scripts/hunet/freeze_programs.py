import argparse,hashlib,json,shutil,struct
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def values(raw):return '{'+','.join('0x%02x'%n for n in raw)+'}'
def freeze(attempt,out):
 root=Path(__file__).resolve().parents[2];gen=root/'generated/hunet';gen.mkdir(parents=True,exist_ok=True)
 source=root/'vendor/hunet/v5'
 expected={'src/device/kernel.cpp':'4d60e71db22449a721e6894f441cba7cc32d69a0769d8f840c741630f3e7f133',
           'src/device/gelu_math.c':'080f75fecfbb0acfaf85d76a282322983e573e024a940c31a26ff35d7d957162'}
 for name,want in expected.items():
  if sha(source/name)!=want:raise ValueError('FROZEN_MATH_SOURCE_CHANGED:'+name)
 reports=[];header=['#ifndef VXL_HUNET_PROGRAMS_H','#define VXL_HUNET_PROGRAMS_H','#include <vxl/core.h>']
 for pid,sub in [(6,''),(7,'wmma')]:
  source=attempt/sub;dest=out/sub;dest.mkdir(parents=True,exist_ok=True)
  report=json.loads((source/'program.json').read_text());reports.append(report)
  for name in ['kernel.elf','kernel.vxbin','code.bin','kernel.map','kernel.disasm','kernel.headers','program.json']:
   shutil.copyfile(str(source/name),str(dest/name))
  header += ['#define VXL_HUNET%d_WIRE_BYTES %du'%(pid,report['wire_bytes']),
             '#define VXL_HUNET%d_TLS_END 0x%08xu'%(pid,report['tls_end']),
             'static const vxl_u8 vxl_hunet%d_wire_sha256[32] __attribute__((unused))=%s;'%(pid,values(bytes.fromhex(report['wire_sha256']))),
             '#define VXL_HUNET%d_RO_COUNT %d'%(pid,len(report['immutable_spans']))]
  for i,s in enumerate(report['immutable_spans']):
   header+=['#define VXL_HUNET%d_RO%d_START 0x%08xu'%(pid,i,s['start']),
            '#define VXL_HUNET%d_RO%d_BYTES %du'%(pid,i,s['bytes']),
            'static const vxl_u8 vxl_hunet%d_ro%d_sha256[32] __attribute__((unused))=%s;'%(pid,i,values(bytes.fromhex(s['sha256'])))]
 for name in ['dependencies.sha256','source-dependencies.sha256']:shutil.copyfile(str(attempt/name),str(out/name))
 header+=['#endif'];(gen/'programs.h').write_text('\n'.join(header)+'\n')
 (gen/'segments.json').write_text(json.dumps(reports,indent=2)+'\n')
 wmmas=[]
 for index,m in enumerate([8,128]):
  words=[0x314e5548,1,128,2,index+1,0,1,m//8,
      0x88002000,0x88000100,0,0,0,0x88002100,0x88003c00,0x88000040,
      8,m,1,4,1,4,8,m*4,1,0,1,1,0,0,index,0]
  # A[M,8] and B[8,4], finite integer fixtures, independent integer reference.
  av=[(i+k)%4-1 for i in range(m) for k in range(8)]
  bv=[(k+j)%3-1 for k in range(8) for j in range(4)]
  expected=[sum(((i+k)%4-1)*((k+j)%3-1) for k in range(8)) for i in range(m) for j in range(4)]
  d=out/'wmma'/('case%d'%index);d.mkdir(exist_ok=True)
  (d/'args.bin').write_bytes(struct.pack('<32I',*words))
  (d/'matrix.bin').write_bytes(struct.pack('<8I',m,4,8,m,4,8,0,m*8))
  (d/'a.f16').write_bytes(b''.join(struct.pack('<e',v) for v in av))
  (d/'b.f16').write_bytes(b''.join(struct.pack('<e',v) for v in bv))
  (d/'expected.f32').write_bytes(struct.pack('<%df'%len(expected),*expected))
  wmmas.append(dict(id=index,m=m,n=4,k=8,tiles=m//8,owner_counts=[1,0,0,0] if m==8 else [4,4,4,4],
                    args_sha256=sha(d/'args.bin'),files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(d.iterdir())}))
 (gen/'wmma.json').write_text(json.dumps(wmmas,indent=2)+'\n')
 for i,case in enumerate(wmmas):
  b=(out/'wmma'/('case%d'%i)/'args.bin').read_bytes()
  header.insert(-1,'static const vxl_u8 vxl_hunet_wmma_args%d[128] __attribute__((unused))=%s;'%(i,values(b)))
 (gen/'programs.h').write_text('\n'.join(header)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--attempt',required=True);p.add_argument('--out',required=True);a=p.parse_args();freeze(Path(a.attempt),Path(a.out))

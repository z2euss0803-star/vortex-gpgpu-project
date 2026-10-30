#!/usr/bin/env python3
"""Independent three-op oracle and actual-SDK dispatch; HOST math only."""
import argparse,datetime,hashlib,json,pathlib,re,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--stage',choices=('relu','add'),required=True)
 p.add_argument('--expect-red',action='store_true');p.add_argument('--ubsan',action='store_true');a=p.parse_args()
 if a.expect_red and a.ubsan:p.error('RED is normal missing-feature proof; sanitizer is a GREEN gate')
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='FUSED_POINTWISE_ACTUAL_SDK_HOST_MATH_NOT_GPU',stage=a.stage,ubsan=a.ubsan,expected_red=a.expect_red,
  started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),commands=[],sources={})
 def save():(out/'receipt.json').write_text(json.dumps(rec,indent=2,sort_keys=True)+'\n')
 def run(name,cmd):
  cmd=[str(v) for v in cmd];t=time.monotonic();r=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (out/(name+'.log')).write_bytes(r.stdout);rec['commands'].append(dict(name=name,argv=cmd,natural_exit=r.returncode,elapsed_s=time.monotonic()-t));save()
  print(name+' exit='+str(r.returncode),flush=True);print(r.stdout.decode(errors='replace')[-5000:],end='',flush=True);return r.returncode
 units=['device/vxnn/mosaic_ops.cpp','device/mosaic_s8/conv.cpp','device/mosaic_s8/quant_pack.cpp','device/mosaic_s8/tile.cpp','device/mosaic_s8/unpack.cpp','device/mosaic_s8/instance_norm_compat_v2.cpp','device/mosaic_s8/instance_norm_compat_v3.cpp']
 sources=units+['device/vxnn/mosaic_ops.h','device/vxnn/mosaic_entry.cpp','tests/nnrt/test_mosaic_fusion.cpp','tests/nnrt/mosaic_fusion_spawn_host.c','tests/nnrt/mosaic_spawn_host/vx_intrinsics.h','tests/nnrt/mosaic_spawn_host/vx_print.h','vendor/hunet/sdk-linux-crt/kernel/src/vx_spawn.c','scripts/unified/check_mosaic_fusion_math.py','config/mosaic/norm-compat-v3.json']
 for rel in sources:
  source=ROOT/rel;target=out/'sources'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes());rec['sources'][rel]=sha(source)
 entry=(ROOT/'device/vxnn/mosaic_entry.cpp').read_text()
 def select(pattern):
  found=re.findall(pattern,entry,re.S)
  if len(found)!=1:raise ValueError('exact actual entry statement required')
  return found[0]
 declaration=select(r'(const uint32_t core=vx_core_id\(\),warp=vx_warp_id\(\),lane=vx_thread_id\(\);)')
 early=select(r'(if\(warp\)return;)');reject=select(r'int rc=(.*?)\?-EINVAL:vxnn_mosaic_validate(?:_fused)?\(a,true,&shape\);')
 launch=select(r'(uint32_t grid=.*?vx_spawn_threads\(1,&grid,&block,callback,&args\);fence\(\);)')
 text='''#include <vx_spawn.h>
#include <errno.h>
extern "C" void fusion_host_owner(unsigned,unsigned,unsigned,int,void*);
static bool rounding_ok(){return true;}
static void fence(){}
static void callback(void*opaque){
'''+declaration+'\n'+early+'\nint rc=('+reject+''')?-EINVAL:0;
fusion_host_owner(core,warp,lane,rc,*static_cast<void**>(opaque));
}
extern "C" int fusion_host_launch(void*opaque){void*args=opaque;
'''+launch+'\nreturn rc;\n}\n'
 (out/'actual_entry_dispatch.cpp').write_text(text)
 cfg=json.loads((ROOT/'config/mosaic/norm-compat-v3.json').read_bytes());assembly='.section .rodata.mosaic_norm,"a"\n.balign 64\n';tables=[]
 for key,symbol in (('rcp.f32','mosaic_norm_rcp_count_v2'),('rsqrt.bin','mosaic_norm_rsqrt_delta_v2')):
  row=cfg['tables'][key];win=pathlib.PureWindowsPath(row['path']);local=pathlib.Path('/mnt')/win.drive[0].lower()/pathlib.Path(*win.parts[1:])
  if local.stat().st_size!=row['bytes'] or sha(local)!=row['sha256']:raise ValueError('frozen A table identity '+key)
  assembly+='.global '+symbol+'\n.type '+symbol+',%object\n'+symbol+':\n.incbin "'+str(local)+'"\n.size '+symbol+', .-'+symbol+'\n';tables.append(dict(path=str(local),sha256=sha(local)))
 assembly+='.section .note.GNU-stack,"",%progbits\n';(out/'tables.S').write_text(assembly);rec['tables']=tables;save()
 san=['-fsanitize=undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if a.ubsan else []
 inc=['-I'+str(ROOT/'tests/nnrt/mosaic_spawn_host'),'-I'+str(ROOT/'vendor/hunet/sdk-linux-crt/kernel/include'),'-I.','-Iinclude']
 if run('sdk',['gcc','-std=c11','-O2']+san+inc+['-c','vendor/hunet/sdk-linux-crt/kernel/src/vx_spawn.c','-o',out/'sdk.o']):return 2
 if run('shim',['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror']+san+inc+['-c','tests/nnrt/mosaic_fusion_spawn_host.c','-o',out/'shim.o']):return 2
 if run('tables',['gcc']+san+['-c',out/'tables.S','-o',out/'tables.o']):return 2
 command=['g++','-std=c++17','-O2','-g1','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off']+san+inc+['tests/nnrt/test_mosaic_fusion.cpp',out/'actual_entry_dispatch.cpp']+units+[out/'sdk.o',out/'shim.o',out/'tables.o','-o',out/'test_mosaic_fusion']
 if run('compile',command):return 2
 rc=run('oracle',[out/'test_mosaic_fusion',a.stage])
 if a.expect_red:
  text=(out/'oracle.log').read_text();ok=rc==71 and ('opcode='+('7' if a.stage=='relu' else '8')) in text and 'rc=-22' in text
  rec['status']='EXPECTED_MISSING_FUSED_OPCODE_RED' if ok else 'RED_NOT_ESTABLISHED';save();return 0 if ok else 2
 if rc:return 2
 for rel,digest in rec['sources'].items():
  if sha(ROOT/rel)!=digest:raise ValueError('source drift '+rel)
 rec['status']='EXACT_OLD_ORACLE_AND_ACTUAL_SDK_HOST_PASS_NOT_GPU';save();return 0
if __name__=='__main__':raise SystemExit(main())

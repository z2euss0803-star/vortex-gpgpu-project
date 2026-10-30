#!/usr/bin/env python3
"""Test actual Mosaic launch expressions through the unchanged SDK scheduler."""
import argparse,datetime,hashlib,json,pathlib,re,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
ENTRY='device/vxnn/mosaic_entry.cpp'
SDK='vendor/hunet/sdk-linux-crt'
TEST='tests/nnrt/mosaic_spawn_host'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=pathlib.Path,required=True)
 p.add_argument('--phase',choices=('red','green'),required=True)
 p.add_argument('--source-commit',required=True,help='Full commit independently checked using Windows git')
 p.add_argument('--math-assets',type=pathlib.Path,help='Existing A-local original MS8AST1 blob; never regenerated')
 p.add_argument('--math-manifest',type=pathlib.Path,help='Trusted frozen manifest for that blob')
 p.add_argument('--math-manifest-sha256',help='Independently frozen manifest digest, required for green')
 a=p.parse_args()
 if not re.fullmatch('[0-9a-f]{40}',a.source_commit):p.error('source commit must be full lowercase hex')
 if a.phase=='green' and (not a.math_assets or not a.math_manifest or not re.fullmatch('[0-9a-f]{64}',a.math_manifest_sha256 or '')):
  p.error('green requires frozen math assets, manifest and its independently checked SHA256')
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='ACTUAL_SDK_HOST_LAUNCH_COVERAGE_NOT_RV32_GPU_OR_BOARD',phase=a.phase,
  source_commit=a.source_commit,source_commit_origin='Windows git supplied by caller; frozen file identities below',
  started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),commands=[],sources={})
 def save():(out/'receipt.json').write_text(json.dumps(rec,indent=2,sort_keys=True)+'\n')
 def freeze(rel):
  source=ROOT/rel;dest=out/'sources'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
  raw=source.read_bytes();dest.write_bytes(raw);rec['sources'][rel]=dict(sha256=sha(source),bytes=len(raw))
 def run(name,cmd):
  cmd=[str(v) for v in cmd];t=time.monotonic()
  result=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (out/(name+'.log')).write_bytes(result.stdout)
  rec['commands'].append(dict(name=name,argv=cmd,natural_exit=result.returncode,elapsed_s=time.monotonic()-t))
  save();print(name+' exit='+str(result.returncode),flush=True)
  print(result.stdout.decode(errors='replace')[-18000:],end='',flush=True)
  return result.returncode
 owned=[ENTRY,'scripts/unified/check_mosaic_launch_r5.py',TEST+'/test_launch.c',TEST+'/vx_intrinsics.h',TEST+'/vx_print.h']
 dependencies=[SDK+'/kernel/src/vx_spawn.c',SDK+'/kernel/include/vx_spawn.h',SDK+'/kernel/include/vx_intrinsics.h']
 for rel in owned+dependencies:freeze(rel)
 source=(ROOT/ENTRY).read_text()
 # Compile these exact production statements, rather than assert source text
 # or supply a test-selected block size. Only hardware-only FCSR/buffer work
 # is omitted; the launch call, warp return and owner rejection execute here.
 def select(pattern,label):
  matches=re.findall(pattern,source,re.S)
  if len(matches)!=1:raise ValueError('unambiguous actual entry '+label+' required')
  return matches[0]
 declaration=select(r'(const uint32_t core=vx_core_id\(\),warp=vx_warp_id\(\),lane=vx_thread_id\(\);)','identity declaration')
 early=select(r'(if\(warp\)return;)','owner warp return')
 reject=select(r'int rc=(.*?)\?-EINVAL:vxnn_mosaic_validate\(a,true,&shape\);','identity rejection')
 launch=select(r'(uint32_t grid=.*?vx_spawn_threads\(1,&grid,&block,callback,&args\);fence\(\);)','launch call')
 generated='''#include <vx_spawn.h>
#include <stdint.h>
#include <errno.h>
extern "C" void vxm_host_trace_enter(void);
extern "C" void vxm_host_trace_owner(unsigned,unsigned,unsigned,int);
static bool rounding_ok(){return true;}
static void fence(){}
static void callback(void*){
'''+declaration+'\nvxm_host_trace_enter();\n'+early+'\nint rc=('+reject+''')?-EINVAL:0;
vxm_host_trace_owner(core,warp,lane,rc);
}
extern "C" int vxm_host_launch(){const void *args=nullptr;
'''+launch+'\nreturn rc;\n}\n'
 (out/'actual_entry_launch.cpp').write_text(generated)
 rec['extracted_production_expressions']=dict(identity_declaration=declaration,warp_return=early,identity_rejection=reject,launch=launch)
 save()
 includes=['-I'+str(ROOT/TEST),'-I'+str(ROOT/SDK/'kernel/include')]
 def compile_and_run(label,sanitize=False):
  flags=['-std=c11','-O2']+(['-fsanitize=undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if sanitize else [])
  if run(label+'-sdk-compile',['gcc']+flags+includes+['-c',ROOT/SDK/'kernel/src/vx_spawn.c','-o',out/(label+'-sdk.o')]):return 2
  strict=['-Wall','-Wextra','-Werror']
  if run(label+'-shim-compile',['gcc']+flags+strict+includes+['-c',ROOT/TEST/'test_launch.c','-o',out/(label+'-shim.o')]):return 2
  if run(label+'-entry-compile',['g++']+[v.replace('c11','c++17') for v in flags]+strict+includes+['-c',out/'actual_entry_launch.cpp','-o',out/(label+'-entry.o')]):return 2
  if run(label+'-link',['g++']+flags[1:]+[out/(label+n+'.o') for n in ('-sdk','-shim','-entry')]+['-o',out/label]):return 2
  return run(label+'-coverage',[out/label])
 rc=compile_and_run('normal')
 if a.phase=='red':
  rec['status']='EXPECTED_V3_MISSING_OWNER_RED' if rc==1 else 'RED_NOT_ESTABLISHED'
  save();return 0 if rc==1 else 2
 if rc:return 2
 if compile_and_run('ubsan',True):return 2
 # Reuse the original HOST math suite. Bind the same frozen tables to their
 # existing A-local paths from config.path; never touch the old B remote path.
 cfg_rel='config/mosaic/norm-compat-v3.json';freeze(cfg_rel)
 cfg=json.loads((ROOT/cfg_rel).read_bytes());math=out/'original-math';math.mkdir()
 assembly='.section .rodata.mosaic_norm,"a"\n.balign 64\n';tables=[]
 for key,symbol in (('rcp.f32','mosaic_norm_rcp_count_v2'),('rsqrt.bin','mosaic_norm_rsqrt_delta_v2')):
  row=cfg['tables'][key];win=pathlib.PureWindowsPath(row['path'])
  local=pathlib.Path('/mnt')/win.drive[0].lower()/pathlib.Path(*win.parts[1:])
  if local.stat().st_size!=row['bytes'] or sha(local)!=row['sha256']:raise ValueError('frozen A table identity '+key)
  assembly+='.global '+symbol+'\n.type '+symbol+',%object\n'+symbol+':\n.incbin "'+str(local)+'"\n.size '+symbol+', .-'+symbol+'\n'
  tables.append(dict(path=str(local),sha256=sha(local),bytes=local.stat().st_size))
 assembly+='.section .note.GNU-stack,"",%progbits\n'
 (math/'mosaic_norm_tables.S').write_text(assembly);rec['frozen_math_tables']=tables;save()
 # The old fixture generator hardcodes a B-only environment file. Invoke its
 # unchanged loader/header/contract functions with explicitly pinned A bytes.
 sys.path.insert(0,str(ROOT))
 from scripts.mosaic.assets import load_assets,write_c_manifest
 from tests.mosaic_s8.contract_fixture import write as write_contract
 for rel in ('config/mosaic/model-v1.json','scripts/mosaic/assets.py','tests/mosaic_s8/contract_fixture.py'):
  freeze(rel)
 asset=a.math_assets.resolve();manifest_path=a.math_manifest.resolve()
 if sha(manifest_path)!=a.math_manifest_sha256:raise ValueError('frozen manifest SHA256')
 manifest=json.loads(manifest_path.read_bytes());raw=asset.read_bytes()
 model=json.loads((ROOT/'config/mosaic/model-v1.json').read_bytes())
 if manifest['source_sha256']!=model['sources']:raise ValueError('frozen model/source identities')
 load_assets(raw,manifest)
 generated=math/'mosaic-generated';generated.mkdir()
 write_c_manifest(manifest,generated/'mosaic_fixture.h');write_contract(generated/'contract')
 fixture=dict(scope='ORIGINAL_W16A16_WEIGHTS_SMALL_SYNTHETIC_INPUT_NOT_FORMAL21',asset=str(asset),asset_sha256=sha(asset),manifest_sha256=sha(manifest_path),gpu_raw_cycles=None)
 (generated/'fixture.json').write_text(json.dumps(fixture,indent=2)+'\n')
 rec['original_math_fixture']=fixture;save()
 cmd=['make','-f','Makefile.unified','OUT='+str(math),'DEBUG_FLAGS=-g1','test-unified-mosaic','test-unified-mosaic-coverage','test-unified-mosaic-propagation','-j1']
 if run('original-host-math',cmd):return 2
 log=(out/'original-host-math.log').read_text()
 expected=['U13_MOSAIC_PRIMITIVE_21_NATIVE_I16_I48_HOST_MATH_PASS','U13_MOSAIC_LAZY_COVERAGE_77_PHASES_5_GEOMETRIES_8_BUDGETS_PASS_HOST','U13_MOSAIC_16_LAYER_GENERIC_PROPAGATION_PASS_HOST']
 if not all(v in log for v in expected) or log.count(expected[2])!=8:raise ValueError('original HOST math semantic receipts missing')
 for rel,identity in rec['sources'].items():
  if sha(ROOT/rel)!=identity['sha256']:raise ValueError('source drift '+rel)
 rec['status']='ACTUAL_SDK_COVERAGE_NORMAL_UBSAN_AND_ORIGINAL_HOST_MATH_PASS_NOT_GPU'
 save();return 0
if __name__=='__main__':raise SystemExit(main())

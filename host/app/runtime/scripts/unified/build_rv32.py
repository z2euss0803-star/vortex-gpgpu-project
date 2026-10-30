"""Rebuild frozen NN libraries; retain original IMAGE/P8 machine-code bytes.

The coordinated task runner must hold the existing three locks/resource gate.
This helper runs compilers serially and never acquires a second device owner.
"""
import pathlib,json,hashlib,sys,argparse
from scripts.unified.cross_common import sha,snapshot,provider,verify_source_lock,new_build_output
from scripts.unified.audit_cross import inspect_elf

def verify_original_shaders(root):
 root=pathlib.Path(root);rows=[]
 p8=json.loads((root/'data/p8/programs/manifest.json').read_text())
 if p8['provider']!=provider(root,2) or p8['catalog_sha256']!=sha(root/'data/p8/programs/catalog.bin'):raise ValueError('isolated P8 provider/catalog identity')
 for row in p8['programs']:
  raw=root/'data/p8'/(row['name']+'.bin');code=root/'data/p8/programs'/(row['entry_id']+'.bin');a=raw.read_bytes();b=code.read_bytes()
  if not (0<len(a)<2048 and b==a+bytes(2048-len(a)) and sha(raw)==row['raw_sha256'] and sha(code)==row['code_sha256']):raise ValueError('original P8 raw/padded binding')
  rows.append(dict(namespace=2,profile=5,program=row['logical_program'],raw_sha256=sha(raw),object_sha256=sha(code),action='ORIGINAL_BYTES_VERIFIED_NOT_RECOMPILED'))
 if sorted(x['program'] for x in rows)!=[9,10,11,12]:raise ValueError('old P8 IDs changed')
 from scripts.unified.build_image_programs import image_programs
 from scripts.unified.build_catalog import build_catalog
 # Preserve the original 64-byte EBREAK padding, then audit the separate
 # 2048-byte zero-tail object. Never replace original EBREAK words by zero.
 base=root/'vendor/image-studio/gpu_programs';records=image_programs(root/'vendor/image-studio',provider(root,1));catalog=build_catalog(records);actual=root/'data/image/programs-v2'
 if (actual/'catalog.bin').read_bytes()!=catalog:raise ValueError('frozen IMAGE catalog/code/schema identity')
 metadata=json.loads((actual/'manifest.json').read_text())
 if metadata['provider']!=provider(root,1) or metadata['catalog_sha256']!=sha(actual/'catalog.bin') or len(metadata['programs'])!=10:raise ValueError('isolated IMAGE manifest/provider identity')
 for i,record in enumerate(records):
  raw=base/(record['name']+'.bin');padded=base/(record['name']+'.padded.bin');entry=catalog[64+512*i+256:64+512*i+288].hex();code=actual/(entry+'.bin')
  if code.read_bytes()!=record['code'] or len(record['code'])!=2048:raise ValueError('admitted IMAGE2048 object differs from original padded shader')
  declaration=metadata['programs'][i]
  if declaration['op']!=i or declaration['logical_program']!=512+i or declaration['logical_profile']!=256 or declaration['entry_id']!=entry or declaration['raw_sha256']!=sha(raw) or declaration['padded_sha256']!=sha(padded) or declaration['code_sha256']!=sha(code) or declaration['schema_sha256']!=hashlib.sha256(record['schema']).hexdigest():raise ValueError('IMAGE raw/original-padding/object/schema manifest mismatch')
  rows.append(dict(namespace=3,profile=256,program=512+i,original_image_op=i,raw_sha256=sha(raw),original_ebreak_padded_sha256=sha(padded),object_sha256=sha(code),action='ORIGINAL_BYTES_VERIFIED_NOT_RECOMPILED'))
 if len(rows)!=14 or sorted(x['original_image_op'] for x in rows if x['namespace']==3)!=list(range(10)):raise ValueError('original IMAGE operation set')
 return rows

def build_target(env_path,source_lock_path,source_lock_sha256,root,out,mosaic_variant="ro64-v2"):
 if mosaic_variant not in ("legacy-v1","ro64-v2"):raise ValueError("Mosaic validator variant")
 root=pathlib.Path(root).resolve();before=verify_source_lock(root,source_lock_path,source_lock_sha256);out=new_build_output(root,out)
 rec=dict(scope='U22_ACTUAL_RV32_REBUILD_AND_STATIC_BINDING_NOT_EXECUTED',target='rv32',source_before=before,source_lock=dict(path=str(source_lock_path),sha256=source_lock_sha256),commands=[],libraries=[],gpu_raw_cycles=None,target_executed=False,board='NOT_RUN',compiler_jobs=1)
 try:
  from scripts.unified.build_int16_rv32 import build as integer
  from scripts.unified.build_hunet_rv32 import build as hunet
  from scripts.unified.build_mosaic_rv32 import build as mosaic
  env=json.loads(pathlib.Path(env_path).read_text());reference=json.loads((root/'data/nnrt/programs/int16-family-v1/rv32-build.json').read_text());archives=[(p,d) for p,d in reference['dependencies'].items() if p.endswith('/artifacts/functional_build/kernel/libvortex.a')]
  if len(archives)!=1:raise ValueError('exact original SDK archive dependency required')
  source_archive,expected=archives[0];env.setdefault('vortex_archive',source_archive)
  if sha(env['vortex_archive'])!=expected:raise ValueError('frozen RV32 SDK archive changed')
  private_env=out/'matched-env.json';private_env.write_text(json.dumps(env,indent=2)+'\n');rec['sdk_archive']=dict(path=env['vortex_archive'],sha256=expected)
  rec['providers']=[provider(root,1),provider(root,2)]
  from scripts.unified.mosaic_program import selected_name
  from scripts.unified.hunet_program import selected_name as selected_hunet
  mosaic_name=selected_name(root) if mosaic_variant=='ro64-v2' else 'mosaic-int16-v1'
  for name,logical,function in [('int16-family-v1',65537,integer),(selected_hunet(root),65552,hunet),(mosaic_name,65568,mosaic)]:
   data=root/'data/nnrt/programs'/name;family=json.loads((data/'family.json').read_text())
   for source in family.get('source_rows',family.get('sources',[])):
    if sha(root/source['path'])!=source['sha256']:raise ValueError('frozen family source drift: '+source['path'])
   directory=out/name;built=function(root,private_env,directory,variant=mosaic_variant) if name==mosaic_name else function(root,private_env,directory)
   if built['natural_exit']!=0 or built['target_executed'] is not False or built['gpu_raw_cycles'] is not None:raise ValueError('actual RV32 build receipt scope')
   if built['flat']['sha256']!=family['code_sha256'] or sha(directory/'code.bin')!=sha(data/'code.bin'):raise ValueError('frozen NN CODE identity changed')
   runtime=built['runtime']
   if runtime['entry']!=0x80000000 or runtime['fcsr_init']!=0 or runtime['mutable_state_domain']!='PS_CRT' or runtime['args_bytes']!=128 or not built['stack'] or not built['object_ownership']:raise ValueError('entry/FCSR/stack/ownership static audit required')
   if name.startswith('mosaic-int16-') and (sha(directory/'rodata.bin')!=family['rodata_sha256'] or built['readonly']['sha256']!=family['rodata_sha256']):raise ValueError('frozen readonly InstanceNorm tables changed')
   elf=inspect_elf(directory/'kernel.elf','rv32',built['elf']['sha256'])
   rec['libraries'].append(dict(namespace=1,profile=1,logical_program=logical,name=name,elf=elf,code_sha256=built['flat']['sha256'],rodata_sha256=family.get('rodata_sha256'),runtime=runtime,build_receipt_sha256=sha(directory/'receipt.json'),action='ACTUAL_MATCHED_RECOMPILE_NOT_EXECUTED'))
  from scripts.unified.cross_assets import verify_original_assets
  rec['original_shaders']=verify_original_shaders(root);rec['original_p8_assets']=verify_original_assets(root);rec['source_after']=snapshot(root)
  if rec['source_after']!=before:raise ValueError('source changed during RV32 build')
  rec['natural_exit']=0
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--env',required=True);p.add_argument('--source-lock',required=True);p.add_argument('--source-lock-sha',required=True);p.add_argument('--out',required=True);p.add_argument('--mosaic-variant',choices=('legacy-v1','ro64-v2'),default='ro64-v2');a=p.parse_args();r=build_target(a.env,a.source_lock,a.source_lock_sha,a.root,a.out,a.mosaic_variant);print(json.dumps(dict(scope=r['scope'],natural_exit=r['natural_exit'],libraries=len(r['libraries']),original_shaders=len(r['original_shaders']),gpu_raw_cycles=None)))

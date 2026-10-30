"""Publish only an actual ABI4 reuse-v7 build with its compiled source closure."""
import argparse,json,pathlib,shutil,hashlib
from scripts.unified.program_identity import sha,compiled_sources
from scripts.unified.mosaic_schema_reuse_v4 import make_schema_reuse_v4
from scripts.unified.mosaic_reuse_program import pins,selected_directory

def publish(root,build):
 root=pathlib.Path(root).resolve();build=pathlib.Path(build).resolve()
 olddir=root/'data/nnrt/programs/mosaic-int16-v6';old=json.loads((olddir/'family.json').read_bytes());out=root/'data/nnrt/programs/mosaic-int16-v7'
 if out.exists():raise ValueError('NEW_REUSE_V7_DIRECTORY_MUST_NOT_EXIST')
 legacy={p:sha(p) for d in (root/'data/nnrt/programs').iterdir() if d.is_dir() for p in d.rglob('*') if p.is_file()}
 rec=json.loads((build/'receipt.json').read_bytes());schema=make_schema_reuse_v4();schema_sha=hashlib.sha256(schema).hexdigest()
 if (type(rec.get('natural_exit')) is not int or rec['natural_exit']!=0 or rec.get('target_executed') is not False
     or rec.get('gpu_raw_cycles',0) is not None or not rec.get('object_ownership') or not rec.get('stack')
     or not 0<rec['stack']['bytes']<8128 or any(c['natural_exit']!=0 for c in rec['commands'])
     or rec.get('variant')!='reuse-v7' or rec.get('private_abi')!=4 or rec.get('schedule_version')!=2
     or rec.get('weight_layout_version')!=2 or rec.get('work_layout_version')!=1 or rec.get('profile_wire_version')!=2):
  raise ValueError('ACTUAL_COMPLETE_ABI4_2_2_BUILD_REQUIRED')
 if (rec['flat']!=dict(bytes=65536,sha256=sha(build/'code.bin')) or (build/'code.bin').stat().st_size!=65536
     or sha(build/'kernel.elf')!=rec['elf']['sha256'] or sha(build/'code.bin')==old['code_sha256']
     or rec['readonly']!=dict(bytes=6307840,sha256=sha(build/'rodata.bin'))
     or (build/'rodata.bin').stat().st_size!=6307840 or sha(build/'rodata.bin')!=old['rodata_sha256']):
  raise ValueError('NEW_EXACT_BUILT_CODE_AND_FROZEN_RO_REQUIRED')
 sources=compiled_sources(rec,'device/vxnn/mosaic_reuse_entry.cpp')
 required={'device/vxnn/mosaic_reuse_entry.cpp','device/vxnn/mosaic_wire_reuse_v4.cpp','device/vxnn/mosaic_wire_reuse_v4.h','device/mosaic_s8/reuse_schedule_v2.h'}
 if not required<=sources.keys() or 'device/vxnn/mosaic_coop_entry.cpp' in sources:raise ValueError('ACTUAL_REUSE_ENTRY_AND_HELPER_CLOSURE_REQUIRED')
 for name,digest in sources.items():
  if sha(root/name)!=digest:raise ValueError('ACTUAL_COMPILED_SOURCE_DRIFT '+name)
 family=dict(schema=1,logical_program=65568,namespace_id=1,logical_profile=1,
             scope='R9_REUSE_SPATIAL2_WEIGHT2_BULK24_PROFILE2_ACTUAL_RV32_STATIC_NOT_EXECUTED',
             code_sha256=rec['flat']['sha256'],rodata_sha256=rec['readonly']['sha256'],schema_sha256=schema_sha,
             gpu_raw_cycles=None,program_receipt_sha256=sha(build/'receipt.json'),variant='reuse-v7',
             sources=[dict(path=p,sha256=d) for p,d in sorted(sources.items())],
             legacy=dict(directory='data/nnrt/programs/mosaic-int16-v6',family_sha256=sha(olddir/'family.json'),code_sha256=old['code_sha256']),
             implementation='spatial2-prepacked-weight2-cooperative-bulk24-lane4-profile2-r9',
             private_abi=4,schedule_version=2,weight_layout_version=2,work_layout_version=1,profile_wire_version=2,
             schema_source=dict(path='scripts/unified/mosaic_schema_reuse_v4.py',sha256=sha(root/'scripts/unified/mosaic_schema_reuse_v4.py')))
 out.mkdir()
 for source,name in ((build/'code.bin','code.bin'),(build/'rodata.bin','rodata.bin'),(build/'kernel.elf','kernel.elf'),(build/'receipt.json','rv32-build.json')):shutil.copyfile(source,out/name)
 (out/'args-schema.bin').write_bytes(schema);(out/'family.json').write_bytes((json.dumps(family,indent=2)+'\n').encode())
 text='#pragma once\n// Actual R9 RV32 static build; no target execution or performance claim.\nnamespace vxnn {\n'
 text+=''.join('inline constexpr const char*'+key+'="'+value+'";\n' for key,value in pins(family,sources).items())
 text+='inline constexpr const char*mosaic_reuse_variant="reuse-v7";\ninline constexpr const char*mosaic_reuse_directory="mosaic-int16-v7";\n}\n'
 (root/'include/vxnn/mosaic_reuse_program.hpp').write_bytes(text.encode())
 if any(sha(p)!=d for p,d in legacy.items()):raise ValueError('OLD_PUBLISHED_FILES_CHANGED')
 selected_directory(root)
 return dict(scope=family['scope'],natural_exit=0,directory=str(out),family_sha256=sha(out/'family.json'),
             code_sha256=family['code_sha256'],schema_sha256=schema_sha,compiled_sources=len(sources),
             old_published_files_unchanged=len(legacy),gpu_raw_cycles=None)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',required=True);p.add_argument('--build',required=True);a=p.parse_args()
 print(json.dumps(publish(a.root,a.build),indent=2))

"""Publish a NEW independently built validator-only family; never overwrite v1.

Run inside the coordinated resource/lock owner after the actual RV32 build.
This changes CODE identity; it asserts static build evidence, never GPU PASS.
"""
import argparse,hashlib,json,pathlib,shutil
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def publish(root,build):
 root=pathlib.Path(root).resolve();build=pathlib.Path(build).resolve();legacy=root/'data/nnrt/programs/mosaic-int16-v1';out=root/'data/nnrt/programs/mosaic-int16-v2';receipt=json.loads((build/'receipt.json').read_bytes());old=json.loads((legacy/'family.json').read_bytes())
 if out.exists():raise ValueError('NEW_V2_DESTINATION_MUST_NOT_EXIST')
 if receipt.get('natural_exit')!=0 or receipt.get('variant')!='ro64-v2' or receipt.get('target_executed') is not False or receipt.get('gpu_raw_cycles') is not None or not receipt.get('stack') or not receipt.get('object_ownership'):raise ValueError('ACTUAL_RO64_RV32_STATIC_BUILD_REQUIRED')
 if sha(build/'code.bin')!=receipt['flat']['sha256'] or receipt['flat']['bytes']!=65536 or sha(build/'code.bin')==old['code_sha256']:raise ValueError('NEW_ACTUAL_CODE_DIGEST_REQUIRED')
 if sha(build/'rodata.bin')!=old['rodata_sha256'] or sha(legacy/'rodata.bin')!=old['rodata_sha256'] or sha(legacy/'args-schema.bin')!=old['schema_sha256'] or sha(legacy/'code.bin')!=old['code_sha256']:raise ValueError('ORIGINAL_CODE_SCHEMA_RO_IDENTITIES_MUST_REMAIN_INTACT')
 for row in old['sources']:
  if sha(root/row['path'])!=row['sha256']:raise ValueError('LEGACY_SOURCE_CHANGED '+row['path'])
 for name,digest in receipt['sources'].items():
  if sha(root/name)!=digest:raise ValueError('NEW_BUILD_SOURCE_DRIFT '+name)
 validator='device/vxnn/mosaic_wire_ro64.cpp'
 if validator not in receipt['sources'] or 'device/vxnn/mosaic_wire.cpp' in receipt['sources']:raise ValueError('EXACT_NEW_VALIDATOR_SOURCE_REQUIRED')
 for row in old['sources']:
  if row['path']!='device/vxnn/mosaic_wire.cpp' and receipt['sources'].get(row['path'])!=row['sha256']:raise ValueError('ARITHMETIC_OR_ENTRY_SOURCE_CHANGED '+row['path'])
 header=root/'include/vxnn/mosaic_ro64_program.hpp';before=header.read_text()
 if 'mosaic_ro64_code_sha256=""' not in before:raise ValueError('CODE_PIN_HEADER_ALREADY_PUBLISHED')
 family=dict(old);family.update(scope='RO64_V2_ACTUAL_RV32_STATIC_NOT_EXECUTED',variant='ro64-v2',code_sha256=sha(build/'code.bin'),program_receipt_sha256=sha(build/'receipt.json'),sources=[dict(path=n,sha256=d) for n,d in sorted(receipt['sources'].items())],legacy=dict(directory='data/nnrt/programs/mosaic-int16-v1',family_sha256=sha(legacy/'family.json'),code_sha256=old['code_sha256']),gpu_raw_cycles=None)
 # Historical archive fields belong to v1 and cannot describe this new build.
 family.pop('evidence_archive',None);family.pop('evidence_sha256',None)
 out.mkdir();shutil.copyfile(build/'code.bin',out/'code.bin');shutil.copyfile(build/'rodata.bin',out/'rodata.bin');shutil.copyfile(legacy/'args-schema.bin',out/'args-schema.bin');shutil.copyfile(build/'receipt.json',out/'rv32-build.json');(out/'family.json').write_text(json.dumps(family,indent=2)+'\n')
 text='#pragma once\n// Actual independently built RO64 v2 CODE pins. Static build only; no GPU PASS.\nnamespace vxnn {\ninline constexpr const char*mosaic_ro64_code_sha256="'+family['code_sha256']+'";\ninline constexpr const char*mosaic_ro64_validator_sha256="'+receipt['sources'][validator]+'";\n}\n';header.write_bytes(text.encode())
 return dict(scope=family['scope'],directory=str(out),code_sha256=family['code_sha256'],schema_sha256=family['schema_sha256'],readonly_sha256=family['rodata_sha256'],pin_header_sha256=sha(header),legacy_unchanged=True,gpu_raw_cycles=None)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--build',required=True);a=p.parse_args();print(json.dumps(publish(a.root,a.build),indent=2))

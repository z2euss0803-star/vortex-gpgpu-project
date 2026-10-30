"""Independent R9 ABI4 GPU selection; never changes host default admission."""
import hashlib,json,pathlib
from scripts.unified.program_identity import selected
from scripts.unified.mosaic_schema_reuse_v4 import make_schema_reuse_v4

def pins(family,sources):
 return dict(mosaic_reuse_code_sha256=family['code_sha256'],
             mosaic_reuse_validator_sha256=sources['device/vxnn/mosaic_wire_reuse_v4.cpp'],
             mosaic_reuse_entry_sha256=sources['device/vxnn/mosaic_reuse_entry.cpp'],
             mosaic_reuse_schedule_sha256=sources['device/mosaic_s8/reuse_schedule_v2.h'],
             mosaic_reuse_work_layout_sha256=sources['device/vxnn/mosaic_work_layout_v1.h'],
             mosaic_reuse_schema_sha256=family['schema_sha256'])

def selected_directory(root):
 root=pathlib.Path(root)
 directory=root/'data/nnrt/programs/mosaic-int16-v7'
 expected=dict(private_abi=4,schedule_version=2,weight_layout_version=2,work_layout_version=1,profile_wire_version=2)
 for filename in ('family.json','rv32-build.json'):
  meta=json.loads((directory/filename).read_bytes())
  if any(type(meta.get(k)) is not int or meta[k]!=value for k,value in expected.items()):
   raise ValueError('EXPLICIT_REUSE_V7_METADATA_REQUIRED '+filename)
 schema_sha=hashlib.sha256(make_schema_reuse_v4()).hexdigest()
 return selected(root,'mosaic-int16-v7','reuse-v7',schema_sha,
                 'include/vxnn/mosaic_reuse_program.hpp',pins)

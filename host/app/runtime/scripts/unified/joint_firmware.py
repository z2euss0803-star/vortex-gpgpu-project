"""Materialize the versioned S8 joint boot catalog. No device or media access."""
import hashlib,json,pathlib,sys
from scripts.unified.bind_kernel import firmware_inputs

def generate(out,*,mosaic_variant=None,mosaic_fused=None):
 root=pathlib.Path(__file__).resolve().parents[2];fw=firmware_inputs(root,4,mosaic_variant=mosaic_variant,mosaic_fused=mosaic_fused)
 out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False);rows=[]
 for name,raw in [('catalog-v1.bin',fw['catalog']),('execution-v1.bin',fw['execution'])]:(out/name).write_bytes(raw)
 programs=out/'programs';programs.mkdir()
 for item in fw['entries']:
  payload=item['payload'];name=item['entry_id']+'.bin';(programs/name).write_bytes(payload)
  rows.append(dict(namespace=item['namespace'],logical_profile=item['logical_profile'],logical_program=item['logical_program'],entry_id=item['entry_id'],payload_sha256=hashlib.sha256(payload).hexdigest(),payload_bytes=len(payload),schema_sha256=hashlib.sha256(item['schema']).hexdigest()))
 rec=dict(scope='S8_JOINT_PRODUCTION_FIRMWARE_METADATA_NOT_GPU_EXECUTED',provider=fw['provider'],catalog_bytes=len(fw['catalog']),catalog_sha256=hashlib.sha256(fw['catalog']).hexdigest(),execution_bytes=len(fw['execution']),execution_sha256=hashlib.sha256(fw['execution']).hexdigest(),entries=rows,omitted_programs=[dict(namespace=1,logical_profile=1,logical_program=65537,reason='first joint image has16 entries; original source and previous provider remain')],gpu_raw_cycles=None,board='NOT_RUN')
 (out/'manifest.json').write_text(json.dumps(rec,indent=2)+'\n');return rec
if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser();parser.add_argument('out');parser.add_argument('--mosaic-variant');args=parser.parse_args()
 r=generate(args.out,mosaic_variant=args.mosaic_variant);print(json.dumps({k:r[k] for k in ('scope','catalog_bytes','catalog_sha256','execution_bytes','execution_sha256','gpu_raw_cycles')}))

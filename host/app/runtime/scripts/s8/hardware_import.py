"""Consume real A-side 2020.1 export records. Never runs an FPGA tool."""
from pathlib import Path
import argparse,json
from scripts.s8.package_release import SOURCE,DCP,sha,js,embedded_bit_matches,hardware_gate
def check_manifest(root,rows):
 root=Path(root).resolve()
 if not rows:raise ValueError('empty export manifest')
 for name,row in rows.items():
  rel=Path(name);path=root/rel
  if rel.is_absolute() or '..' in rel.parts or ':' in name or '\\' in name or path.is_symlink() or not path.is_file() or path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:raise ValueError('delivery payload mismatch '+name)
 return True
def check_native(value):
 if value.get('event')!='exited' or type(value.get('exit_code')) is not int or value['exit_code']!=0 or value.get('deadline_exceeded') is not False:raise ValueError('actual export natural exit missing/failed')
 return True
def consume(delivery,out):
 delivery=Path(delivery).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 identity=json.loads((delivery/'IDENTITY.json').read_text());manifest=json.loads((delivery/'MANIFEST.json').read_text());check_manifest(delivery,manifest)
 if identity['source_sha256']!=SOURCE or identity['routed_dcp_sha256']!=DCP or identity['tool']!='Vivado/Vitis 2020.1':raise ValueError('wrong frozen hardware/tool')
 bit=delivery/identity['bit']['file'];xsa=delivery/identity['xsa']['file']
 if sha(bit)!=identity['bit']['sha256'] or sha(xsa)!=identity['xsa']['sha256']:raise ValueError('identity payload drift')
 records={};dcp=None
 for kind in ('bit','xsa'):
  record_path=delivery/'evidence'/(kind+'_export_receipt.json');record=json.loads(record_path.read_text());native=Path(record['run'])/'natural_exit.json'
  if sha(native)!=record['natural_exit_sha256']:raise ValueError('native receipt drift')
  check_native(json.loads(native.read_text()))
  if record['source_sha256']!=SOURCE or record.get('dcp_sha256',record.get('routed_dcp_sha256'))!=DCP or record.get('errors') or record['native']['status']!='PASS':raise ValueError('export source/process failure')
  if record['bit_sha256']!=sha(bit) or kind=='xsa' and record['xsa_sha256']!=sha(xsa):raise ValueError('export bit/XSA mismatch')
  current=Path(record.get('dcp',record.get('routed_dcp')))
  if sha(current)!=DCP:raise ValueError('actual frozen routed DCP drift')
  if dcp is not None and current!=dcp:raise ValueError('export DCP paths differ')
  dcp=current;records[kind]=dict(path=str(record_path),sha256=sha(record_path),natural_exit_path=str(native),natural_exit_sha256=sha(native))
 embedded_bit_matches(xsa,bit)
 audit=dict(scope='CONSUMER_EXPORT_IDENTITY_AUDIT_NOT_NEW_EXPORT_OR_PHYSICAL_PASS',source_sha256=SOURCE,dcp_sha256=DCP,bit_sha256=sha(bit),xsa_sha256=sha(xsa),natural_exit=0,vivado_version='2020.1',physical_status=identity['physical_signoff'],simulation=identity['simulation'],board='NOT_RUN',parents=records,delivery_manifest_sha256=sha(delivery/'MANIFEST.json'))
 js(out/'export-audit.json',audit)
 descriptor=dict(source_sha256=SOURCE,dcp_sha256=DCP,dcp=str(dcp),bit=str(bit),xsa=str(xsa),export_receipt=str(out/'export-audit.json'))
 js(out/'hardware.json',descriptor);result=hardware_gate(descriptor);js(out/'receipt.json',result);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--delivery',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();print(json.dumps(consume(a.delivery,a.out)))

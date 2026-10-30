"""Candidate-specific export gate and acyclic controlled software package."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct,zipfile
from scripts.s8.binding import make_binding
from scripts.s8.assets import objects,payloads
from scripts.s8.catalog import load_catalog
from scripts.s8.crt import runtime_inventory
from scripts.hunet.audit_program import elf_info
SOURCE='708bfd8bd611beb9631cf8294e191cf57d1a6a48c777cea7d9f42b8a27c51a7d'
DCP='f550075ce584a2d2af606ff11e048d0a64c25962aa23e50e37430b4061839439'
OLD_BIT='deb74c77e7e76e9108f49307371c42c889b21f8e764c545987a7786ce543ebc8'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def js(path,data):Path(path).write_text(json.dumps(data,sort_keys=True,indent=2)+'\n',encoding='utf-8')
def match_source(digest,receipts):
 if not re.fullmatch('[0-9a-f]{64}',digest) or not receipts or any(r.get('source_sha256')!=digest for r in receipts):raise ValueError('cross source identity missing/mixed')
 return True
def verify_release(root):
 root=Path(root).resolve();manifest=json.loads((root/'manifest.json').read_text())
 if manifest.get('scope')!='S8_RELEASE' or manifest.get('profile')!=5 or manifest.get('program')!=11:raise ValueError('wrong S8 package scope')
 for name,row in manifest['files'].items():
  parts=Path(name)
  if parts.is_absolute() or '..' in parts.parts or ':' in name or '\\' in name:raise ValueError('unsafe release path')
  path=root/parts
  if path.is_symlink() or any(p.is_symlink() for p in path.parents if root in p.parents) or not path.is_file() or path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:raise ValueError('release payload changed '+name)
 binding=(root/'binding.bin').read_bytes()
 if binding!=make_binding(manifest['digests']) or sha(root/'binding.bin')!=manifest['binding_sha256']:raise ValueError('binding changed')
 embedded_bit_matches(root/'hardware/system.xsa',root/'hardware/top.bit')
 if sha(root/'hardware/top.bit')!=manifest['digests']['bit'] or sha(root/'hardware/system.xsa')!=manifest['digests']['xsa']:raise ValueError('hardware package changed')
 return manifest
def embedded_bit_matches(xsa,bit):
 raw=Path(bit).read_bytes()
 with zipfile.ZipFile(str(xsa)) as z:
  names=[n for n in z.namelist() if n.lower().endswith('.bit')]
  if len(names)!=1 or z.read(names[0])!=raw:raise ValueError('XSA does not embed exact candidate bit')
 return True
def hardware_gate(hardware):
 if type(hardware) is not dict:raise ValueError('hardware descriptor')
 if 'source_sha256' in hardware and hardware['source_sha256']!=SOURCE:raise ValueError('wrong frozen S8 source')
 if 'dcp_sha256' in hardware and hardware['dcp_sha256']!=DCP:raise ValueError('wrong frozen S8 DCP')
 missing=[n for n in ('source_sha256','dcp','bit','xsa','export_receipt') if not hardware.get(n) or (n!='source_sha256' and not Path(hardware[n]).is_file())]
 if missing:return dict(state='WAIT_HW_EXPORT',exit=2,missing=missing,physical_status='OPEN_NOT_PASS')
 if sha(hardware['dcp'])!=DCP:raise ValueError('actual routed DCP changed')
 if sha(hardware['bit'])==OLD_BIT:raise ValueError('old W4 bit substitution')
 export=json.loads(Path(hardware['export_receipt']).read_text())
 wanted=dict(source_sha256=SOURCE,dcp_sha256=DCP,bit_sha256=sha(hardware['bit']),xsa_sha256=sha(hardware['xsa']))
 if any(export.get(n)!=v for n,v in wanted.items()) or export.get('natural_exit')!=0 or export.get('vivado_version')!='2020.1':raise ValueError('missing/mismatched actual candidate export receipt')
 embedded_bit_matches(hardware['xsa'],hardware['bit'])
 return dict(state='EXPORT_IDENTITY_VERIFIED_NOT_PHYSICAL_PASS',exit=0,physical_status=export.get('physical_status','OPEN_NOT_PASS'),**wanted)
def _mutable(elf,layout):
 _,_,loads,_=elf_info(elf);base=0x80000000;spans=sorted((p['start']-base,p['end']-base) for p in loads if p['flags']&2)
 facts=layout['elf'];spans.append((facts['tls_base']-base,facts['tls_base']-base+64*facts['tbss_stride']));merged=[]
 for start,end in sorted(spans):
  if merged and start<=merged[-1][1]:merged[-1][1]=max(end,merged[-1][1])
  else:merged.append([start,end])
 return [(a,b-a) for a,b in merged]
def _array(data):return '{'+','.join('0x%02x'%v for v in data)+'}'
def release_header(flat,layout,binding,inputs,mutable):
 facts=layout['elf'];region_order=('CODE','ARGS','A','B','OUT','IDENT','CTRL','STACK')
 regions='{'+','.join('{%du,%du}'%(layout['regions'][n][0],layout['regions'][n][1]-layout['regions'][n][0]) for n in region_order)+'}'
 layout_c='{'+regions+',%du,%du,%du,%du,%du,0,%du,53}'%(layout['runtime_bytes'],facts['tls_base'],facts['tbss_bytes'],facts['tbss_stride'],facts['tbss_align'],facts['static_stack_bytes'])
 rows=['#ifndef VXL_S8_RELEASE_H','#define VXL_S8_RELEASE_H','static const vxl_u8 vxl_s8_code[]='+_array(flat)+';','static const struct vxl_s8_mutable vxl_s8_mutable_ranges[]={'+','.join('{%du,%du}'%m for m in mutable)+'};']
 rows.append('static const struct vxl_s8_input_digest vxl_s8_inputs[]={'+','.join('{%du,%du,%s,%s}'%(r['case_id'],r['slot'],_array(bytes.fromhex(r['a_sha256'])),_array(bytes.fromhex(r['b_sha256']))) for r in inputs)+'};')
 head=struct.unpack('<8I',binding[:32]);binding_c='{'+','.join(str(v)+'u' for v in head)+',{'+','.join(_array(binding[32+i*32:64+i*32]) for i in range(14))+'},{0}}'
 rows.append('static const struct vxl_s8_program vxl_s8_frozen_program={vxl_s8_code,%du,0x80000000u,%s,vxl_s8_mutable_ranges,%du,%s,%s,%s,vxl_s8_inputs,%du};'%(len(flat),layout_c,len(mutable),binding_c,_array(hashlib.sha256(flat).digest()),_array(hashlib.sha256(binding).digest()),len(inputs)))
 rows.append('#endif');return ('\n'.join(rows)+'\n').encode()
def package(root,env,hardware,out):
 root=Path(root).resolve();env=json.loads(Path(env).read_text());hardware=json.loads(Path(hardware).read_text());out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 try:
  gate=hardware_gate(hardware)
  if gate['exit']:
   receipt=dict(version=1,scope='S8_PACKAGE_GATE_NOT_IMAGE_NOT_BOARD',**gate);js(out/'receipt.json',receipt);return receipt
  device=Path(env['device_build']);cpu=Path(env['cpu_build']);freeze=json.loads(Path(env['source_freeze']).read_text());kernel=json.loads(Path(env['kernel_receipt']).read_text())
  rv=json.loads((device/'receipt.json').read_text());user=json.loads((cpu/'receipt.json').read_text())
  match_source(freeze['source_sha256'],[rv,user,kernel])
  if rv.get('integrity')!='PASS' or rv.get('audit',{}).get('integrity')!='PASS' or user.get('integrity')!='PASS' or kernel.get('integrity')!='PASS':raise ValueError('all actual cross receipts required')
  if rv.get('scope')!='FULL_RV32_CROSS_COMPILE_NOT_BOARD' or user.get('scope')!='ACTUAL_AARCH64_USERSPACE_NOT_BOARD' or not kernel.get('scope','').startswith('PRE_BINDING_KERNEL_COMPILE'):raise ValueError('wrong cross scope')
  for row in freeze['source_files']:
   name=Path(row['path'])
   if name.is_absolute() or '..' in name.parts or sha(root/name)!=row['sha256']:raise ValueError('frozen source drift')
  layout=json.loads((device/'layout.json').read_text());catalog=load_catalog(root/'contracts/s8/cases.json');runtime=runtime_inventory(root);js(out/'runtime-inputs.json',runtime)
  for src,dest in ((device/'code.bin','code.bin'),(device/'kernel.elf','kernel.elf'),(device/'layout.json','layout.json'),(root/'contracts/s8/cases.json','catalog.json'),(root/'contracts/s8/wire.json','wire.json'),(cpu/'objects/vxl_s8','vxl_s8'),(cpu/'objects/libvxl-s8.so','libvxl-s8.so')):shutil.copyfile(str(src),str(out/dest))
  (out/'hardware').mkdir();shutil.copyfile(hardware['bit'],str(out/'hardware/top.bit'));shutil.copyfile(hardware['xsa'],str(out/'hardware/system.xsa'))
  shutil.copyfile(str(Path(env['source_freeze']).parent/'source.tar.gz'),str(out/'source.tar.gz'))
  digests=dict(software_source=freeze['source_sha256'],device_flat=sha(out/'code.bin'),device_elf=sha(out/'kernel.elf'),layout=sha(out/'layout.json'),parameters=sha(out/'wire.json'),cases=sha(out/'catalog.json'),rv32_compiler=rv['compiler_sha256'],rv32_runtime=sha(out/'runtime-inputs.json'),runner=sha(out/'vxl_s8'),kernel_config=kernel['config_sha256'],hardware_source=SOURCE,routed_dcp=DCP,bit=gate['bit_sha256'],xsa=gate['xsa_sha256'])
  if digests['device_flat']!=rv['audit']['flat_sha256'] or digests['device_elf']!=rv['audit']['elf_sha256'] or digests['runner']!=user['elfs']['vxl_s8']['sha256']:raise ValueError('cross artifact drift')
  binding=make_binding(digests);(out/'binding.bin').write_bytes(binding);flat=(out/'code.bin').read_bytes();(out/'assets').mkdir();cases=[];input_rows=[]
  for case in catalog['cases']:
   rows=[]
   for obj in objects(case,flat):
    data=obj.pop('data');key=hashlib.sha256(data).hexdigest();name='assets/'+key+'.bin';target=out/name
    if not target.exists():target.write_bytes(data)
    rows.append(dict(obj,path=name,sha256=key))
   cases.append(dict(id=case['id'],objects=rows))
   if case['id']!=1:
    for slot in range(16):
     a,b=payloads(case,slot);input_rows.append(dict(case_id=case['id'],slot=slot,a_sha256=hashlib.sha256(a).hexdigest(),b_sha256=hashlib.sha256(b).hexdigest()))
  if len(input_rows)!=544:raise ValueError('controlled input count')
  mutable=_mutable(out/'kernel.elf',layout);header=release_header(flat,layout,binding,input_rows,mutable);(out/'vxl_s8_release.h').write_bytes(header)
  js(out/'inputs.json',input_rows);js(out/'mutable.json',mutable);js(out/'source-freeze.json',freeze)
  manifest=dict(version=1,scope='S8_RELEASE',profile=5,program=11,source_sha256=freeze['source_sha256'],binding_sha256=sha(out/'binding.bin'),runner_sha256=sha(out/'vxl_s8'),digests=digests,hardware=gate,cases=cases,image='NOT_BUILT',board='NOT_RUN',gpu_raw_cycles=None)
  manifest['files']={p.relative_to(out).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in out.rglob('*') if p.is_file()};js(out/'manifest.json',manifest)
  receipt=dict(version=1,state='SOFTWARE_BOUND_PACKAGE_NOT_IMAGE',scope='S8_PACKAGE_GATE_NOT_IMAGE_NOT_BOARD',exit=0,binding_sha256=manifest['binding_sha256'],files=len(manifest['files']),hardware=gate);js(out/'receipt.json',receipt);return receipt
 except Exception as exc:
  js(out/'receipt.json',dict(state='FAIL_NOT_ACCEPTED',exit=1,error=str(exc)));raise
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('.'));p.add_argument('--env',type=Path,required=True);p.add_argument('--hardware',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();r=package(a.root,a.env,a.hardware,a.out);print(json.dumps(r));raise SystemExit(r['exit'])

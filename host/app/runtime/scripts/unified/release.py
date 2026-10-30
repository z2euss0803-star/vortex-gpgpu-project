"""Offline release policy. This module never opens devices or writes SD media."""
import pathlib,json,re,stat,hashlib

def _wire(value):return json.dumps(value,sort_keys=True,separators=(',',':'))

def _digest(value):return isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None

def delivery_flags(profile):
 if profile=='LEGACY_DUAL_PROVIDER':return (1,2)
 if profile=='S8_JOINT_ONLY':return (4,)
 raise ValueError('explicit supported delivery profile required')

def extension_scope(flag):
 return {1:'HOST_AND_BUILTIN_HASH_ONLY_NOT_TARGET_EXECUTION',2:'NOT_APPLICABLE_GRAPHICS_ONLY_PROVIDER',4:'JOINT_NN_PACKAGES_HOST_ADMISSION_ONLY_NOT_TARGET_EXTENSION'}[flag]

def load_pinned(row):
 """Load a release input receipt; its provenance is never inferred from a name."""
 raw=read_pinned(row,32*1024**2)
 def unique(pairs):
  result={}
  for key,value in pairs:
   if key in result:raise ValueError('duplicate release input JSON key')
   result[key]=value
  return result
 def finite(_):raise ValueError('non-finite release input JSON number')
 return json.loads(raw.decode('utf8'),object_pairs_hook=unique,parse_constant=finite)

def read_pinned(row,limit):
 if not isinstance(row,dict) or not isinstance(row.get('path'),str) or type(row.get('bytes')) is not int or not 0<row['bytes']<=limit or not _digest(row.get('sha256')):raise ValueError('bounded file size and full SHA256 input required')
 path=pathlib.Path(row['path']);before=path.lstat()
 if not path.is_absolute() or path.is_symlink() or not stat.S_ISREG(before.st_mode) or before.st_size!=row['bytes']:raise ValueError('exact absolute regular input file required')
 raw=path.read_bytes();after=path.lstat()
 if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns) or len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('release input bytes changed or digest mismatch')
 return raw

def release_errors(document):
 """Return policy errors; actual receipt/byte admission is a separate gate."""
 if not isinstance(document,dict):return ['release document must be an object']
 errors=[]
 if type(document.get('schema')) is not int or document['schema']!=1 or document.get('scope')!='UNIFIED_RELEASE_EVIDENCE_NOT_BOARD_ACCEPTANCE':errors.append('explicit offline release scope/schema')
 if _wire(document.get('abi'))!=_wire(dict(legacy_bytes=176,v2_bytes=128)):errors.append('unchanged original176B and v2 128B ABI')
 if _wire(document.get('scheduler'))!=_wire(dict(device_owners=1,active_gpu_jobs=1,cpu_threads=4,preempt=False)):errors.append('single owner/GPU job, four CPU workers, no hardware preemption')
 def cycles(value):
  if isinstance(value,dict):
   for name,child in value.items():
    if name=='gpu_raw_cycles' and child is not None:errors.append('offline GPU raw cycles must be null')
    cycles(child)
  elif isinstance(value,list):
   for child in value:cycles(child)
 cycles(document)
 if document.get('gpu_raw_cycles','missing') is not None:errors.append('explicit null cycle field required')
 if document.get('field_commands_executed') is not False or document.get('sd')!='NOT_TOUCHED':errors.append('release generation must not execute field commands or SD writes')
 declarations=json.loads((pathlib.Path(__file__).resolve().parents[2]/'contracts/unified/providers.json').read_text())['providers']
 try:flags=delivery_flags(document.get('delivery_profile','LEGACY_DUAL_PROVIDER'))
 except ValueError:return errors+['unsupported explicit delivery profile']
 wanted={r['provider_flags']:r for r in declarations if r['provider_flags'] in flags};rows=document.get('providers')
 if not isinstance(rows,list) or len(rows)!=len(flags):return errors+['exact selected delivery providers required']
 seen=set();complete=True
 for row in rows:
  if not isinstance(row,dict):errors.append('provider object required');complete=False;continue
  decl=row.get('declaration',{});flag=decl.get('provider_flags') if isinstance(decl,dict) else None
  if type(flag) is not int or flag not in wanted or flag in seen or _wire(decl)!=_wire(wanted.get(flag)):errors.append('exact separate frozen provider and full hardware identity required')
  else:seen.add(flag)
  for name,expected in (('board','NOT_RUN'),('display','NOT_RUN'),('application_precision','NOT_RUN'),('performance','NOT_MEASURED')):
   if row.get(name)!=expected:errors.append('cannot promote offline evidence into '+name)
  if row.get('gpu_raw_cycles','missing') is not None:errors.append('explicit provider raw cycles null required')
  phases=dict(offline='PASS_HOST_ONLY',cross='PASS_NOT_EXECUTED',kernel='PASS_CONTENT_ONLY',image='PASS_CONTENT_ONLY')
  for phase,passed in phases.items():
   if row.get(phase) not in (passed,'PENDING'):errors.append('unrecognized separate phase '+phase)
   if row.get(phase)!=passed:complete=False
  if flag in wanted and row.get('extension_same_driver')!=extension_scope(flag):errors.append('extension evidence must preserve provider capability and host/target boundaries')
  if row.get('image')=='PASS_CONTENT_ONLY':
   if not isinstance(row.get('build_id'),str) or not re.fullmatch('[A-Za-z0-9_.-]{1,128}',row['build_id']):errors.append('actual bounded image build-id')
   files=row.get('files')
   if not isinstance(files,dict) or set(files)!=set(('BOOT.BIN','boot.scr','image.ub')):errors.append('matching complete SD startup file set required')
   else:
    for name,item in files.items():
     if not isinstance(item,dict) or type(item.get('bytes')) is not int or item['bytes']<=0 or not _digest(item.get('sha256')):errors.append('actual length and full SHA256 '+name)
 if seen!=set(flags):errors.append('selected hardware providers present exactly once')
 history=document.get('history_failures')
 required={'MOSAIC_WATCHDOG','HUNET_EBUSY','ORT_R2','S8_PHYSICAL_SIGNOFF'}
 if not isinstance(history,list) or {r.get('id') for r in history if isinstance(r,dict)}!=required or len(history)!=4:errors.append('all four original failure/open evidence categories required')
 elif any(r.get('state')!='PRESERVED_OPEN' or not isinstance(r.get('evidence'),list) or not r['evidence'] or any(not isinstance(p,str) or not p for p in r['evidence']) for r in history):errors.append('original historical failures must stay open and linked')
 review=document.get('review')
 if review not in ('PENDING','PASS_FRESH_WHOLE_BRANCH'):errors.append('explicit fresh review state')
 if review=='PASS_FRESH_WHOLE_BRANCH':
  evidence=document.get('review_evidence')
  if not isinstance(evidence,dict) or evidence.get('scope')!='FRESH_WHOLE_BRANCH_REVIEW' or not _digest(evidence.get('source_snapshot_id')) or not _digest(evidence.get('report_sha256')) or any(type(evidence.get(k)) is not int or evidence[k]!=0 for k in ('critical','important')):errors.append('full fresh review report and source digest with no unresolved high-impact findings required')
 if type(document.get('ready_for_sd')) is not bool or (document.get('ready_for_sd') and (not complete or review!='PASS_FRESH_WHOLE_BRANCH')):errors.append('SD readiness requires all actual offline phases and a fresh whole-branch review')
 return errors

def verify_blob(row,limit,*,allow_empty=False):
 """Stream a retained artifact, without loading images or archives into RAM."""
 from scripts.unified.cross_common import sha
 if type(allow_empty) is not bool or not isinstance(row,dict) or not isinstance(row.get('path'),str) or type(row.get('bytes')) is not int or not (0 if allow_empty else 1)<=row['bytes']<=limit or not _digest(row.get('sha256')):raise ValueError('bounded regular artifact identity required')
 p=pathlib.Path(row['path']);before=p.lstat()
 if not p.is_absolute() or p.is_symlink() or not stat.S_ISREG(before.st_mode) or before.st_size!=row['bytes'] or sha(p)!=row['sha256']:raise ValueError('retained artifact no longer matches input proof')
 after=p.lstat()
 if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):raise ValueError('artifact changed during verification')

def admit_image_producer(root,current,built,producer_root):
 """Check the actual producing cut separately from later release-only files."""
 import ast
 from scripts.unified.cross_common import snapshot
 from scripts.unified.bind_kernel import require_cross_inputs
 root=pathlib.Path(root);producer_root=pathlib.Path(producer_root)
 old=snapshot(producer_root)
 if _wire(built.get('source_before'))!=_wire(old) or _wire(built.get('source_after'))!=_wire(old):raise ValueError('exact frozen image producer required')
 require_cross_inputs(dict(natural_exit=0,target='rv32',target_executed=False,source_before=old,source_after=old,commands=[],gpu_raw_cycles=None),current,'rv32')
 def installed(s):return [r for r in s['files'] if r['path'].startswith(('data/','petalinux/','tests/nnrt/fixtures/'))]
 if _wire(installed(old))!=_wire(installed(current)):raise ValueError('model, asset, dependency recipe or external network fixture differs from actual image production')
 def image_ast(base):
  module=ast.parse((base/'scripts/unified/image.py').read_text())
  module.body=[n for n in module.body if not (isinstance(n,ast.FunctionDef) and n.name=='boot_bif') and not (isinstance(n,ast.If) and any(isinstance(x,ast.Name) and x.id=='__name__' for x in ast.walk(n.test)))]
  return ast.dump(module)
 def payload_policy(base):
  module=ast.parse((base/'scripts/unified/audit_image.py').read_text());nodes=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name=='payload_errors']
  if len(nodes)!=1:raise ValueError('one actual image installation admission policy required')
  return ast.dump(nodes[0])
 if image_ast(root)!=image_ast(producer_root) or payload_policy(root)!=payload_policy(producer_root):raise ValueError('image preparation or installed payload admission logic changed after actual build')
 if _wire(snapshot(producer_root))!=_wire(old):raise ValueError('frozen producing source changed during release audit')
 return dict(image_producer_source_snapshot_id=old['source_snapshot_id'],runtime_source_id=old['runtime_source_id'],installed_input_files=len(installed(old)),current_release_source_snapshot_id=current['source_snapshot_id'],target_rebuilt=False)

def error_ledger_rows(ledger):
 """Index legacy error schemas without rewriting any original observation."""
 if not isinstance(ledger,list) or not ledger or any(not isinstance(x,dict) for x in ledger):raise ValueError('complete original object error ledger required')
 rows=[]
 for position,item in enumerate(ledger):
  canonical=_wire(item).encode('utf8');content=hashlib.sha256(canonical).hexdigest()
  identity=hashlib.sha256(b'VXU_FIRST_ERROR_ROW_V1\0'+str(position).encode()+b'\0'+bytes.fromhex(content)).hexdigest()
  rows.append(dict(ledger_row=position,row_identity=identity,content_sha256=content,original_id=item.get('id'),original_id_present='id' in item,original_run=item.get('run')))
 return rows

def assemble_release(root,inputs_path,out):
 """Bind actual independently retained receipts and bytes; no field execution.

 Inputs name the final source lock, unchanged U20 host archives, U22 target
 receipts, both actual U23/U24 receipts, exact retained boot files and errors.
 A missing provider is an error, never a successful partial delivery.
 """
 from scripts.unified.cross_common import snapshot,verify_source_lock,new_build_output,sha,provider
 from scripts.unified.bind_kernel import require_cross_inputs
 root=pathlib.Path(root).resolve();inputs_path=pathlib.Path(inputs_path);out=new_build_output(root,out)
 rec=dict(scope='PINNED_ACTUAL_UNIFIED_OFFLINE_RELEASE_NOT_BOARD',gpu_raw_cycles=None,sd='NOT_TOUCHED')
 def successful(value,scope):
  if not isinstance(value,dict) or value.get('scope')!=scope or type(value.get('natural_exit')) is not int or value['natural_exit']!=0 or value.get('gpu_raw_cycles','missing') is not None:raise ValueError('actual successful typed scope required '+scope)
  return value
 def logs(value):
  commands=value.get('commands',[])
  if not commands:raise ValueError('nonempty actual successful command sequence required')
  for command in commands:
   if type(command.get('natural_exit')) is not int or command['natural_exit']!=0:raise ValueError('failed command cannot enter release')
   path=command.get('log');expected=command.get('log_sha256',command.get('sha256'));pin=index['logs'].get(path)
   if not isinstance(pin,dict) or pin.get('sha256')!=expected:raise ValueError('complete retained actual command log binding required')
   verify_blob(pin,1024**3,allow_empty=True)
 try:
  index_raw=inputs_path.read_bytes();index_pin=dict(path=str(inputs_path),bytes=len(index_raw),sha256=hashlib.sha256(index_raw).hexdigest());index=load_pinned(index_pin)
  lock=index['source_lock'];load_pinned(lock);before=verify_source_lock(root,lock['path'],lock['sha256'])
  software=load_pinned(index['software'])
  if software.get('scope')!='COMPLETE_MATCHED_HOST_SOFTWARE_GATES_NOT_TARGET_KERNEL_IMAGE_OR_BOARD' or software.get('gpu_raw_cycles','missing') is not None or snapshot(software['source']['root'])['runtime_source_id']!=before['runtime_source_id']:raise ValueError('unchanged actually validated host runtime source required')
  if {g.get('mode') for g in software.get('gates',[])}!={'normal','sanitize','thread'} or len(software['gates'])!=3:raise ValueError('actual normal/sanitizer/thread gates required')
  for gate in software['gates']:
   archive=gate['archive'];retained=index['software_archives'][gate['run']]
   if gate.get('scope')!='COMPLETE_REAL_HOST_GATE_SAME_FROZEN_SOURCE_NOT_BOARD' or type(gate.get('commands')) is not int or gate['commands']<=0 or archive['verified_members']!=archive['members'] or archive['members']<=0 or retained['sha256']!=archive['sha256'] or retained['bytes']!=archive['archive_bytes']:raise ValueError('complete original host archive/member proof required')
   verify_blob(retained,8*1024**3)
  rv32=load_pinned(index['cross']['rv32']);require_cross_inputs(rv32,before,'rv32')
  profile=index.get('delivery_profile','LEGACY_DUAL_PROVIDER');rows=[]
  from scripts.unified.cross_common import aarch64_target
  for flag in delivery_flags(profile):
   key=str(flag);declaration=provider(root,flag);cross=load_pinned(index['cross'][key]);require_cross_inputs(cross,before,aarch64_target(flag))
   kernel=successful(load_pinned(index['kernel'][key]),'ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE')
   require_cross_inputs(dict(natural_exit=0,target='rv32',target_executed=False,source_before=kernel['source_before'],source_after=kernel['source_after'],commands=[],gpu_raw_cycles=None),before,'rv32');logs(kernel)
   image=successful(load_pinned(index['image'][key]),'ACTUAL_PROVIDER_COMPLETE_BOOT_FIT_ROOTFS_CONTENT_NOT_BOARD_OR_APPLICATION_PASS');logs(image)
   built=successful(load_pinned(index['image_builds'][key]),'ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD');logs(built)
   payload=successful(load_pinned(index['payloads'][key]),'MATCHED_IMAGE_PAYLOAD_NOT_EXECUTED')
   producing=admit_image_producer(root,before,built,index['image_producer_source_roots'][key])
   if image.get('complete_build_receipt_sha256')!=index['image_builds'][key]['sha256'] or built.get('kernel_receipt_sha256')!=index['kernel'][key]['sha256'] or built.get('payload_receipt_sha256')!=index['payloads'][key]['sha256'] or image.get('payload_manifest_sha256')!=payload.get('payload_manifest_sha256') or any(_wire(payload.get(k))!=_wire(built['source_before']) for k in ('source_before','source_after')):raise ValueError('actual complete build, kernel, payload and audit receipt chain differs')
   if any(_wire(r.get('provider'))!=_wire(declaration) for r in (kernel,image,built)) or payload.get('provider_flags')!=flag or image.get('errors')!=[] or image.get('board_identity_verified') is not False or image.get('hardware_hash_measured') is not False:raise ValueError('actual isolated provider content audit required')
   invariant=kernel['model_B_kernel_invariance']
   if invariant.get('target_executed') is not False or invariant.get('gpu_raw_cycles','missing') is not None or invariant.get('vmlinux_sha256')!=kernel['artifacts']['vmlinux']['sha256'] or invariant.get('driver_sha256')!=kernel['artifacts']['driver']['sha256']:raise ValueError('actual unchanged built-in kernel after user package staging required')
   files={}
   for name in ('BOOT.BIN','boot.scr','image.ub'):
    retained=index['boot_files'][key][name];wanted=image['files'][name]
    if retained.get('sha256')!=wanted['sha256'] or retained.get('bytes')!=wanted['bytes']:raise ValueError('retained startup file differs from actual image audit '+name)
    verify_blob(retained,512*1024**2);files[name]=dict(bytes=wanted['bytes'],sha256=wanted['sha256'])
   extension=extension_scope(flag)
   rows.append(dict(declaration=declaration,offline='PASS_HOST_ONLY',cross='PASS_NOT_EXECUTED',kernel='PASS_CONTENT_ONLY',image='PASS_CONTENT_ONLY',board='NOT_RUN',display='NOT_RUN',application_precision='NOT_RUN',performance='NOT_MEASURED',gpu_raw_cycles=None,build_id=image['build_id'],files=files,extension_same_driver=extension,producing_image_source=image.get('source_provenance'),release_source_admission=producing,kernel_source_id=image['kernel_audit']['kernel_source_id']))
  ledger=load_pinned(index['first_errors'])
  error_rows=error_ledger_rows(ledger)
  history=index['history_failures']
  for item in history:
   for path in item['evidence']:
    p=pathlib.Path(path)
    if not p.is_absolute() or not p.is_file() or p.is_symlink():raise ValueError('retained historical failure reference required')
  review='PENDING';review_evidence=None
  if index.get('review') is not None:
   evidence=load_pinned(index['review'])
   if evidence.get('source_snapshot_id')!=before['source_snapshot_id']:raise ValueError('fresh whole-branch review belongs to another source cut')
   review='PASS_FRESH_WHOLE_BRANCH';review_evidence=dict(evidence,report_sha256=index['review']['sha256'])
  document=dict(schema=1,scope='UNIFIED_RELEASE_EVIDENCE_NOT_BOARD_ACCEPTANCE',delivery_profile=profile,providers=rows,abi=dict(legacy_bytes=176,v2_bytes=128),scheduler=dict(device_owners=1,active_gpu_jobs=1,cpu_threads=4,preempt=False),history_failures=history,review=review,review_evidence=review_evidence,field_commands_executed=False,sd='NOT_TOUCHED',gpu_raw_cycles=None,ready_for_sd=review=='PASS_FRESH_WHOLE_BRANCH',runtime_source_id=before['runtime_source_id'])
  errors=release_errors(document)
  if errors:raise ValueError('release policy failed '+repr(errors))
  if snapshot(root)!=before or sha(inputs_path)!=index_pin['sha256']:raise ValueError('release source or input index changed while inputs were checked')
  (out/'ACCEPTANCE.json').write_text(json.dumps(document,indent=2)+'\n');(out/'FIRST_ERRORS.json').write_bytes(read_pinned(index['first_errors'],32*1024**2));(out/'FIRST_ERROR_INDEX.json').write_text(json.dumps(error_rows,indent=2)+'\n')
  rec.update(natural_exit=0,input_index_sha256=index_pin['sha256'],source_snapshot_id=before['source_snapshot_id'],runtime_source_id=before['runtime_source_id'],acceptance_sha256=sha(out/'ACCEPTANCE.json'),first_errors=len(ledger),ready_for_sd=document['ready_for_sd'])
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec

if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',required=True);parser.add_argument('--inputs',required=True);parser.add_argument('--out',required=True);args=parser.parse_args()
 print(json.dumps(assemble_release(args.root,args.inputs,args.out)))

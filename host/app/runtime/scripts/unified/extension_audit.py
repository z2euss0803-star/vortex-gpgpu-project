"""Offline source/operator extension admission, never network execution proof.

Inputs are independently collected snapshots. This function checks their
consistency; it does not attest that a caller's claimed source bytes or an
execution result were measured. U20 still requires the actual model-B run.
"""
import pathlib,re
_HEX=re.compile(r'^[0-9a-f]{64}$')
_PROTECTED=('driver/','core/','include/uapi/','data/nnrt/programs/',
            'data/image/programs-v2/','data/p8/programs/')
def _check(condition,detail):
 if not condition:raise ValueError(detail)
def _digest(value):return isinstance(value,str) and bool(_HEX.fullmatch(value))
def _files(value):
 _check(isinstance(value,dict) and 0<len(value)<=100000,'bounded nonempty file snapshot required')
 for name,sha in value.items():
  _check(isinstance(name,str) and 0<len(name)<=4096 and '\\' not in name and ':' not in name,'relative POSIX file path required')
  p=pathlib.PurePosixPath(name)
  _check(not p.is_absolute() and name==str(p) and all(x not in ('.','..') for x in p.parts),'normalized source path required')
  _check(_digest(sha),'complete source SHA required')
 return value
def _hardware(value):
 _check(isinstance(value,dict) and set(value)=={'provider','contract','bit','xsa','compatible'},'complete provider identity required')
 _check(type(value['provider']) is int and value['provider'] in (1,2),'frozen S8 or W4 provider required')
 _check(all(_digest(value[k]) for k in ('contract','bit','xsa','compatible')),'complete hardware component SHA required')
 return value
def _programs(value):
 _check(isinstance(value,dict) and 0<len(value)<=64,'bounded installed program identity set required')
 _check(all(isinstance(k,str) and re.fullmatch(r'[1-9][0-9]*:[1-9][0-9]*',k) and _digest(v) for k,v in value.items()),'namespace/id and complete program SHA required')
 return value
def audit_extension(before,after,packages,run_manifest):
 _check(isinstance(before,dict) and isinstance(after,dict),'before and after source snapshots required')
 b,a=_files(before.get('files')),_files(after.get('files'))
 hardware=_hardware(before.get('hardware'))
 _check(_hardware(after.get('hardware'))==hardware,'extension changed physical hardware contract')
 programs=_programs(before.get('programs'))
 _check(_programs(after.get('programs'))==programs,'extension changed installed program identities')
 operators=before.get('operators')
 _check(isinstance(operators,list) and 0<len(operators)<=1024 and all(isinstance(k,str) and re.fullmatch(r'[A-Za-z0-9_.-]{1,128}',k) for k in operators) and len(set(operators))==len(operators),'bounded unique registered operators required')
 _check(after.get('operators')==operators,'extension must use unchanged operator registry')
 protected={name for name in set(b)|set(a) if name.startswith(_PROTECTED)}
 _check(protected and any(p.startswith('driver/') for p in protected) and any(p.startswith('core/v2/') for p in protected) and any(p.startswith('include/uapi/') for p in protected),'required driver/core/UAPI identity coverage missing')
 _check(any(pathlib.PurePosixPath(p).name=='catalog.bin' for p in protected),'installed program catalog source coverage missing')
 _check(all(name in a and name in b and a[name]==b[name] for name in protected),'driver/core/UAPI or installed program source changed')
 _check(isinstance(packages,list) and 0<len(packages)<=64,'bounded new package list required')
 ids=[]
 for package in packages:
  _check(isinstance(package,dict) and _digest(package.get('id')),'immutable model-package ID required')
  _check(package['id'] not in ids,'duplicate model-package ID')
  ids.append(package['id']);ops=package.get('operators')
  _check(isinstance(ops,list) and 0<len(ops)<=100000 and all(isinstance(k,str) and k in operators for k in ops),'new model uses unregistered operator')
  _check(_hardware(package.get('hardware'))==hardware,'model hardware differs even if program number matches')
  used=_programs(package.get('programs'))
  _check(all(k in programs and programs[k]==v for k,v in used.items()),'new model requests unknown or changed program')
 _check(isinstance(run_manifest,dict) and run_manifest.get('before_files')==b and run_manifest.get('after_files')==a,'run manifest does not match complete source snapshots')
 declared=run_manifest.get('new_models',[run_manifest.get('new_model')])
 _check(declared==ids,'run manifest model package identity differs')
 execution=run_manifest.get('execution')
 _check(isinstance(execution,dict) and isinstance(execution.get('state'),str) and execution.get('gpu_raw_cycles') is None,'offline audit requires explicit execution state and null GPU cycles')
 _check('gpu_raw_cycles' in execution,'missing raw cycle scope must not be inferred')
 return dict(state='SOURCE_AND_OPERATOR_EXTENSION_AUDIT_ONLY',new_models=len(ids),model_ids=ids,
             protected_files=len(protected),changed_user_files=sorted(n for n in set(b)|set(a) if b.get(n)!=a.get(n)),
             provider=hardware['provider'],execution_state=execution['state'],
             actual_network_execution_verified=False,board_verified=False,gpu_raw_cycles=None)

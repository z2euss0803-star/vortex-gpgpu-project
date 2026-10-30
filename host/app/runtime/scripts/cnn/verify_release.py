"""Seal OFFLINE evidence; this manifest cannot approve RTL or board execution."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path, PurePosixPath
from generate_fixtures import CASES, generate_fixture
from generate_kernels import generate_program, program_words
from source_identity import source_commit, source_tree_clean, git_bytes

SPEC_SHA='6aff0b9ef7cb743cf2ece74ea822837aa6d3b1b07fbf836939daf7ad905455df'
PLAN_SHA='ce6cf89b4b309a6c0033e11e5bd5a806c377f438aea18ba1aadfa603e87ba495'
REQUIRED_GATES=('contract','uapi','memory','jobs','probe','copy','submit','runtime',
    'cnn-reference','cnn-kernels','cnn-contract','cnn-preflight','cnn-timing',
    'cnn-session','cnn-release','tools','legacy')
REQUIRED_SOURCE_FILES=(
    'Makefile','contracts/profiles/h0-r4.json','contracts/cnn-i32-8x8-v1.json',
    'include/uapi/vx_linux.h','include/vxl/core.h','include/vxl/engine.h','include/vxl/cnn.h',
    'include/vxl/transport.h','core/cnn.c','core/program.c','core/engine.c',
    'driver/vxl_drv.c','driver/vxl_hw.c','driver/vxl_ioctl.c','driver/Makefile',
    'driver/shared_cnn.c','userspace/transport_ioctl.c','tools/vxl_cnn.cpp',
    'tools/cnn_session.cpp','tools/cnn_timing.cpp','tools/cnn_reference.cpp',
    'generated/cnn/programs.h','kernel/cnn/manifest.json',
    'kernel/cnn/id2.bin','kernel/cnn/id3.bin','kernel/cnn/id4.bin','kernel/cnn/id5.bin')
MATRIX={'CPU_REFERENCE':'PASS','RV32_MODEL':'PASS','UAPI_CONTRACT':'PASS',
    'SIMT_RTL':'NOT_RUN','AARCH64':'NOT_RUN','IMAGE':'NOT_RUN','SD_DEPLOY':'NOT_RUN',
    'H0_CNN':'NOT_RUN','H1':'NOT_RUN'}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def local(root, name):
    if not isinstance(name,str) or '\\' in name:
        raise ValueError('Nonportable manifest path')
    relative=PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts or not relative.parts or ':' in name:
        raise ValueError('Unsafe manifest path')
    result=(root/name).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError('Manifest path escapes root')
    return result

def identity(root):
    return source_commit(root)

def make_manifest(root, source_commit, files, fixtures, receipt):
    root=Path(root).resolve()
    profile=json.loads((root/'contracts/profiles/h0-r4.json').read_text())
    base=local(root,fixtures)
    return dict(scope='CNN_OFFLINE_RELEASE',source_commit=source_commit,
        spec_sha256=SPEC_SHA,plan_sha256=PLAN_SHA,contract='cnn-i32-8x8-v1',
        profile=profile['profile'],bit_sha256=profile['bit_sha256'],xsa_sha256=profile['xsa_sha256'],
        source_files={f:sha(local(root,f)) for f in sorted(files)},
        fixture_root=fixtures,fixtures={p.relative_to(base).as_posix():sha(p) for p in sorted(base.rglob('*')) if p.is_file()},
        programs=json.loads((root/'kernel/cnn/manifest.json').read_text())['programs'],
        offline_receipt=dict(path=receipt,sha256=sha(local(root,receipt))),
        matrix=dict(MATRIX))

def verify_release(root, manifest):
    root=Path(root).resolve();errors=[]
    try:
        for key,expected in [('scope','CNN_OFFLINE_RELEASE'),('contract','cnn-i32-8x8-v1'),
                ('spec_sha256',SPEC_SHA),('plan_sha256',PLAN_SHA),('matrix',MATRIX)]:
            if manifest.get(key)!=expected:errors.append(key+' mismatch')
        commit=manifest.get('source_commit','')
        if not re.fullmatch('[0-9a-f]{40}',commit) or identity(root)!=commit:
            errors.append('Source commit mismatch')
        files=manifest['source_files']
        if not set(REQUIRED_SOURCE_FILES)<=set(files):errors.append('Required source omitted')
        if (root/'.git').exists():
            inventory=set(f for f in git_bytes(root,'ls-files','-z').decode('utf-8').split('\0') if f)
            if set(files)!=inventory:errors.append('Tracked source inventory mismatch')
            if not source_tree_clean(root):errors.append('Source tree differs from frozen commit')
        for name,digest in files.items():
            if not re.fullmatch('[0-9a-f]{64}',digest) or sha(local(root,name))!=digest:
                errors.append('Source SHA mismatch: '+name)
        profile=json.loads((root/'contracts/profiles/h0-r4.json').read_text())
        for key in ('profile','bit_sha256','xsa_sha256'):
            if manifest.get(key)!=profile[key]:errors.append(key+' mismatch')
        canonical=[dict(id=i,bytes=4096,instruction_bytes=len(program_words(i))*4,
            sha256=hashlib.sha256(generate_program(i)).hexdigest()) for i in (2,3,4,5)]
        if manifest['programs']!=canonical:errors.append('Frozen program inventory mismatch')
        if json.loads((root/'kernel/cnn/manifest.json').read_text())['programs']!=canonical:
            errors.append('Program source manifest mismatch')
        for i in (2,3,4,5):
            if (root/f'kernel/cnn/id{i}.bin').read_bytes()!=generate_program(i):
                errors.append(f'Program {i} bytes differ')
        base=local(root,manifest['fixture_root']);expected_files={}
        with tempfile.TemporaryDirectory() as temporary:
            reference=Path(temporary)
            for case in CASES:
                generate_fixture(case,reference/case)
            for p in sorted(reference.rglob('*')):
                if not p.is_file():continue
                name=p.relative_to(reference).as_posix();actual=local(base,name)
                expected_files[name]=sha(actual)
                if name.endswith('.json'):
                    equal=json.loads(actual.read_text())==json.loads(p.read_text())
                else:equal=actual.read_bytes()==p.read_bytes()
                if not equal:errors.append('Fixture numeric/guard mismatch: '+name)
        if manifest['fixtures']!=expected_files:errors.append('Fixture inventory/SHA mismatch')
        ref=manifest['offline_receipt'];receipt_path=local(root,ref['path'])
        if sha(receipt_path)!=ref['sha256']:errors.append('Offline receipt SHA mismatch')
        receipt=json.loads(receipt_path.read_text())
        if receipt.get('scope')!='OFFLINE_HOST_ONLY' or receipt.get('source_commit')!=commit:
            errors.append('Offline receipt scope/source mismatch')
        if receipt.get('source_tree_clean') is not True:
            errors.append('Offline receipt source was not sealed')
        if receipt.get('board')!='NOT_RUN' or receipt.get('h1')!='NOT_RUN':
            errors.append('Offline receipt falsely promotes board/H1')
        if set(receipt.get('gates',{}))!=set(REQUIRED_GATES):errors.append('Offline gate inventory mismatch')
        for name in REQUIRED_GATES:
            gate=receipt.get('gates',{}).get(name,{})
            if type(gate.get('exit')) is not int or gate['exit']!=0 or gate.get('status')!='PASS':
                errors.append('Offline gate failed/missing: '+name)
            command=gate.get('command')
            if not isinstance(command,list) or not command or any(not isinstance(x,str) or not x for x in command) or command[0]!='make' or command[-1]!='test-'+name:
                errors.append('Gate command missing/mismatched: '+name)
            if not isinstance(gate.get('cwd'),str) or not gate['cwd']:errors.append('Gate cwd missing: '+name)
            for suffix,key in (('.log','stdout_sha256'),('.stderr.txt','stderr_sha256')):
                if not isinstance(gate.get(key),str) or not re.fullmatch('[0-9a-f]{64}',gate[key]):
                    errors.append('Gate output binding missing: '+name+suffix)
                elif sha(receipt_path.parent/(name+suffix))!=gate[key]:
                    errors.append('Gate output SHA mismatch: '+name+suffix)
    except (KeyError,TypeError,ValueError,OSError,subprocess.SubprocessError) as e:
        errors.append('Malformed/missing release evidence: '+str(e))
    return errors

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',required=True)
    parser.add_argument('--manifest',required=True);parser.add_argument('--create',action='store_true')
    parser.add_argument('--fixtures',default='build/cnn/fixtures');parser.add_argument('--receipt')
    args=parser.parse_args();root=Path(args.root).resolve();path=Path(args.manifest)
    if args.create:
        if not args.receipt:parser.error('--receipt required for creation')
        if path.exists():parser.error('Manifest already exists; preserve previous release')
        files=git_bytes(root,'ls-files','-z').decode('utf-8').split('\0')
        manifest=make_manifest(root,identity(root),[f for f in files if f],args.fixtures,args.receipt)
    else:manifest=json.loads(path.read_text())
    errors=verify_release(root,manifest)
    if errors:
        print(json.dumps(dict(status='FAIL',errors=errors),indent=2));raise SystemExit(1)
    if args.create:
        with path.open('x',encoding='utf-8',newline='\n') as output:
            output.write(json.dumps(manifest,indent=2)+'\n')
    print('CNN_OFFLINE_RELEASE_INTEGRITY_PASS_BOARD_NOT_RUN')

#!/usr/bin/env python3
"""Local packing evidence only. No target build, GPU, package, or board claim."""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import re
import subprocess
import sys
import time

ROOT=pathlib.Path(__file__).resolve().parents[2]

def main():
 parser=argparse.ArgumentParser(__doc__)
 parser.add_argument('--out',type=pathlib.Path,required=True)
 parser.add_argument('--phase',choices=('red','green'),required=True)
 parser.add_argument('--source-commit',required=True,
                     help='Full commit independently read using Windows git for this Windows-managed worktree')
 args=parser.parse_args()
 if not re.fullmatch('[0-9a-f]{40}',args.source_commit):parser.error('source commit must be 40 lowercase hex digits')
 out=args.out.resolve()
 out.mkdir(parents=True,exist_ok=False)
 sources=['device/mosaic_s8/quant_pack.cpp','tests/mosaic_s8/test_pack_r4.cpp',
          'scripts/unified/check_mosaic_pack_r4.py','tests/mosaic_s8/test_pack.cpp']
 receipt=dict(scope='HOST_PACKING_ONLY_NOT_RV32_NOT_GPU_NOT_BOARD',phase=args.phase,
              started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              host=platform.node(),platform=platform.platform(),cwd=str(ROOT),commands=[],sources={},
              source_commit=args.source_commit,source_commit_origin='Windows git supplied by caller; per-file frozen bytes below')
 def save():
  (out/'receipt.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
 def run(name,cmd):
  cmd=[str(s) for s in cmd]
  before=time.monotonic()
  result=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (out/(name+'.log')).write_bytes(result.stdout)
  receipt['commands'].append(dict(name=name,argv=cmd,natural_exit=result.returncode,elapsed_s=time.monotonic()-before))
  save()
  print(name+' exit='+str(result.returncode),flush=True)
  print(result.stdout.decode('utf-8',errors='replace'),end='',flush=True)
  return result.returncode
 for name in sources:
  raw=(ROOT/name).read_bytes()
  receipt['sources'][name]=dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
  (out/(pathlib.Path(name).name+'.source')).write_bytes(raw)
 save()
 if run('compiler',['g++','--version']):return 2
 flags=['g++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-fno-fast-math',
        '-ffp-contract=off','-Iinclude','-Idevice/mosaic_s8']
 compile_cmd=flags+['tests/mosaic_s8/test_pack_r4.cpp','device/mosaic_s8/quant_pack.cpp','-o',out/'test_pack_r4']
 if run('compile-normal',compile_cmd):return 2
 if run('exact-normal',[out/'test_pack_r4']):return 2
 speed_exit=run('host-cost',[out/'test_pack_r4','--benchmark','1.25'])
 if args.phase=='red':
  receipt['status']='EXPECTED_COST_RED' if speed_exit==1 else 'RED_NOT_ESTABLISHED'
  receipt['expected_failure']='HOST speedup <1.25 for unchanged R3 packing; exact output must already pass'
  save()
  return 0 if speed_exit==1 else 2
 if speed_exit:return 2
 sanitize=flags+['-fsanitize=undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                 'tests/mosaic_s8/test_pack_r4.cpp','device/mosaic_s8/quant_pack.cpp','-o',out/'test_pack_r4_ubsan']
 if run('compile-ubsan',sanitize):return 2
 if run('exact-ubsan',[out/'test_pack_r4_ubsan']):return 2
 # Existing public pack/quantize tests protect RNE, non-finite, and error behavior.
 legacy=flags+['tests/mosaic_s8/test_pack.cpp','device/mosaic_s8/quant_pack.cpp','-o',out/'test_pack_legacy']
 if run('compile-legacy',legacy):return 2
 if run('legacy',[out/'test_pack_legacy']):return 2
 receipt['status']='HOST_EXACT_UBSAN_AND_COST_PASS_NOT_GPU'
 save();return 0

if __name__=='__main__':
 sys.exit(main())

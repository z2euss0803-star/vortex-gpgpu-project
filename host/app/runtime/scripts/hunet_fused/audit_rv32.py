"""Actual ELF/CRT/static audit; deliberately makes no RTL or board claim."""
import hashlib,json,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
from audit_program import elf_info,instructions,package_program,audit_program

def audit(attempt):
 attempt=pathlib.Path(attempt)
 b,_,loads,symbols=elf_info(attempt/'kernel.elf')
 report=package_program(attempt/'kernel.elf',attempt,9)
 for required in ('hunet_fused_run','hunet_fused_pack','hunet_fused_gemm','hunet_v5_entry'):
  if not any(required in name for name in symbols):raise ValueError('MISSING_ENTRY:'+required)
 entry=next(n for n in symbols if 'hunet_fused_run' in n)
 words=instructions(b,loads,symbols,entry,4096)
 if sum(w==0x0ff0000f for w in words)<2:raise ValueError('FUSED_PHASE_FENCES')
 # custom0/funct3=4 is the real barrier instruction, not a text marker.
 if not any(w&0xfe007fff==0x0000400b for w in words):raise ValueError('FUSED_GLOBAL_BARRIER_INSTRUCTION')
 manifest=json.loads((attempt/'build.json').read_text())
 defines=manifest['defines']
 for define in ('-DNUM_CORES=4','-DNUM_WARPS=4','-DNUM_THREADS=4','-DDCACHE_WRITEBACK=0','-DGBAR_ENABLE'):
  if define not in defines:raise ValueError('HW4_DEFINE:'+define)
 if '-ffp-contract=off' not in manifest['compile_flags'] or any('fast-math' in x for x in manifest['compile_flags']):raise ValueError('FLOAT_CONTRACT')
 for rel,sha in manifest['sources'].items():
  if hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()!=sha:raise ValueError('SOURCE_DRIFT:'+rel)
 # Generated .su records are compiler evidence. Report unknown/dynamic frames, never guess.
 frames=[]
 for su in attempt.glob('*.su'):
  for line in su.read_text().splitlines():
   name,size,kind=line.rsplit('\t',2);frames.append(dict(function=name,bytes=int(size),kind=kind))
 if not frames or any(f['kind']!='static' or f['bytes']>=8192 for f in frames):raise ValueError('STACK_FRAME_NOT_BOUNDED')
 report.update(result='FUSED_RV32_BUILD_AUDIT_PASS',scope='RV32_COMPILE_AND_STATIC_AUDIT_NOT_RTL_NOT_BOARD',
  build=manifest,stack_frames=frames,max_compiled_frame=max(f['bytes'] for f in frames),
  note='Per-function frames and CRT stack reservation checked; dynamic execution remains RTL gate.')
 (attempt/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
 return report
if __name__=='__main__':print(json.dumps(audit(sys.argv[1])))

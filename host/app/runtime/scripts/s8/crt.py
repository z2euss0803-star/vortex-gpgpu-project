"""Strict source/fact prechecks. Actual ELF extraction and closure are T18."""
from pathlib import Path
import hashlib
import re
from scripts.s8.layout import build_layout
from scripts.s8.catalog import expected_cases

def need(ok,reason):
    if not ok:raise ValueError(reason)

def audit_facts(facts: dict) -> dict:
    layout=build_layout(facts,dict(cases=expected_cases()))
    need(facts.get('exit_address')==0x10002008,'private CRT exit address')
    need(facts.get('stack_base')==0xffff0000 and facts.get('stack_log2')==13,'fixed guarded stack')
    need((facts.get('cores'),facts.get('warps'),facts.get('lanes'))==(4,4,4),'64 physical harts')
    need(facts.get('has_exit_store') is True and facts.get('has_exit_fence') is True,'true CRT exit store/fence')
    need(facts.get('recursive') is False and facts.get('dynamic_stack') is False,'bounded call chain')
    need(facts.get('unresolved')==[],'unresolved symbols')
    need(set(('_start','init_regs','__init_tls','vx_spawn_threads','_Exit','exit','vx_perf_dump'))<=set(facts.get('runtime_symbols',[])),'full CRT symbols')
    return dict(scope='STATIC_CRT_FACTS_NOT_EXECUTION',layout=layout)

def body(source,name):
    match=re.search(r'\b(?:void|int)\s+'+re.escape(name)+r'\s*\([^)]*\)\s*\{',source)
    need(match is not None,'function absent: '+name)
    start=match.end();level=1;cursor=start
    while cursor<len(source) and level:
        level+=int(source[cursor]=='{')-int(source[cursor]=='}');cursor+=1
    need(level==0,'unbalanced function: '+name)
    return source[start:cursor-1]

def audit_source(source: str) -> dict:
    # This deliberately bounded precheck only accepts the simple tail shape
    # used here. It is not a substitute for final disassembly/CFG inspection.
    source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
    callback=body(source,'s8_callback');finish=body(source,'s8_group_finish');main=body(source,'main')
    need(not re.search(r'\b(return|goto|throw|longjmp)\b',callback),'callback early exit')
    need(re.fullmatch(r'.*\bs8_group_finish\(error\);\s*',callback,re.S) is not None,'single unified tail')
    need(callback.count('s8_group_finish(')==1,'one finish call')
    need(not re.search(r'\b(return|goto|throw|longjmp)\b',finish),'finish early exit')
    need(finish.count('s8_barrier_group();')==2,'two common group barriers')
    # Require both barriers at function scope, not under an owner condition.
    level=0;barriers=0
    for token in re.findall(r's8_barrier_group\(\);|[{}]',finish):
        if token=='{':level+=1
        elif token=='}':level-=1
        else:need(level==0,'conditional tail barrier');barriers+=1
    need(barriers==2,'barrier count')
    need(main.index('csr_read(VX_CSR_MSCRATCH)')<main.index('vx_spawn_threads('),'capture original MSCRATCH')
    need('grid=4,block=16' in main and 's8_callback,&local' in main,'full four-core launch')
    need('s8_sp()' in callback and 's8_tp()' in callback and 'stack_probe' in callback and 's8_tls_probe==0' in callback,'real identity probes')
    return dict(scope='SOURCE_CONTROL_FLOW_NOT_EXECUTION',barriers=2,harts=64)

def runtime_inventory(root: Path) -> dict:
    paths=['device/s8/crt/link32.ld','device/s8/crt/perf_stub.c']
    sdk='vendor/hunet/sdk-linux-crt/kernel/'
    paths += [sdk+'src/'+name for name in ('vx_start.S','vx_spawn.c','vx_syscalls.c')]
    paths += [sdk+'include/'+name for name in ('vx_spawn.h','vx_intrinsics.h','vx_print.h')]
    return dict(scope='RUNTIME_SOURCE_INVENTORY_NOT_BUILD',files={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths})

def audit_sync_source(source: str) -> dict:
    source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
    finish=body(source,'s8_group_finish')
    need('vxl_s8_sync *sync' in finish and 'j->control_base+64+core*64' in finish,'per-core64B sync')
    need('if(error)sync->abort=1;' in finish,'idempotent abort latch')
    need(not re.search(r'reserved\s*\[[^]]*\]\s*=',source),'reserved control write')
    need('abort[hart]' not in finish,'hart array overlaps handshake/reserved fields')
    need(finish.count('s8_barrier_group();')==2,'unchanged unified tail')
    return dict(scope='SOURCE_SYNC_NOT_EXECUTION')

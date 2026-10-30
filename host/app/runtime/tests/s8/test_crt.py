import copy
import importlib
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]

class CRT(unittest.TestCase):
    def facts(self):
        return dict(entry=0x80000000,tdata_bytes=0,tbss_bytes=32,tbss_stride=32,tbss_align=16,
                    tls_base=0x80004000,static_stack_bytes=2048,load_ranges=[dict(start=0x80000000,mem_bytes=16384,file_bytes=8192)],
                    exit_address=0x10002008,stack_base=0xffff0000,stack_log2=13,cores=4,warps=4,lanes=4,
                    has_exit_store=True,has_exit_fence=True,recursive=False,dynamic_stack=False,
                    unresolved=[],runtime_symbols=['_start','init_regs','__init_tls','vx_spawn_threads','_Exit','exit','vx_perf_dump'])
    def test_contract_rejects_tdata_stack_and_missing_real_exit(self):
        api=importlib.import_module('scripts.s8.crt')
        self.assertEqual(api.audit_facts(self.facts())['scope'],'STATIC_CRT_FACTS_NOT_EXECUTION')
        for field,value in [('tdata_bytes',1),('tbss_stride',16),('static_stack_bytes',8128),('has_exit_store',False),('has_exit_fence',False),('exit_address',0x10002088),('stack_base',0xffff0040),('recursive',True),('dynamic_stack',True),('unresolved',['memcpy'])]:
            f=self.facts();f[field]=value
            with self.assertRaises(ValueError,msg=field):api.audit_facts(f)
        f=self.facts();f['tls_base']=0x800efff0
        with self.assertRaises(ValueError):api.audit_facts(f)
    def test_all_callback_paths_reach_the_same_two_tail_barriers(self):
        api=importlib.import_module('scripts.s8.crt')
        main=(ROOT/'device/s8/main.cpp').read_text()
        self.assertEqual(api.audit_source(main)['scope'],'SOURCE_CONTROL_FLOW_NOT_EXECUTION')
        for bad in [main.replace('s8_group_finish(error);','return;'),main.replace('s8_group_finish(error);','if (error) return; s8_group_finish(error);'),main.replace('s8_barrier_group();','',1)]:
            with self.assertRaises(ValueError):api.audit_source(bad)
    def test_sdk_sources_and_private_linker_are_explicit(self):
        api=importlib.import_module('scripts.s8.crt')
        identity=api.runtime_inventory(ROOT)
        for name in ('vx_start.S','vx_spawn.c','vx_syscalls.c','device/s8/crt/link32.ld','device/s8/crt/perf_stub.c'):
            self.assertTrue(any(p.endswith(name) for p in identity['files']))
        self.assertTrue(all(len(v)==64 for v in identity['files'].values()))
    def test_control_page_has_per_core_sync_and_preserved_reserved_words(self):
        api=importlib.import_module('scripts.s8.crt')
        main=(ROOT/'device/s8/main.cpp').read_text()
        self.assertEqual(api.audit_sync_source(main)['scope'],'SOURCE_SYNC_NOT_EXECUTION')
        with self.assertRaises(ValueError):api.audit_sync_source(main.replace('if(error)sync->abort=1;','sync->reserved[0]=error;'))

if __name__=='__main__':unittest.main()

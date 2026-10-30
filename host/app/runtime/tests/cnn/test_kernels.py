"""Arithmetic/address evidence only. This serial model is not SIMT/RTL."""
import importlib.util
import struct
import sys
import os
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
FIXTURES=Path(os.environ.get('CNN_FIXTURES',ROOT/'build/cnn/fixtures'))
sys.path[:0] = [str(ROOT/'scripts/cnn'), str(ROOT/'tests/cnn')]

class Kernels(unittest.TestCase):
    def test_all_cases(self):
        self.assertTrue((ROOT/'scripts/cnn/generate_kernels.py').is_file(), 'Kernel generator missing')
        from generate_kernels import generate_program
        from audit_kernels import audit_program
        from rv32_model import Memory, run_program
        from generate_fixtures import CASES
        for case in CASES:
            memory = Memory.fixture(FIXTURES/case)
            for pid, names in ((2,('x','wconv','bconv','z')),(3,('z',None,None,'r')),
                               (4,('r',None,None,'p')),(5,('p','wfc','bfc','y'))):
                args = memory.args(pid, names)
                blob = generate_program(pid)
                report = audit_program(pid,blob,args)
                self.assertEqual(report['scope'],'STATIC_PROGRAM_AUDIT_NOT_SIMT')
                traces = [run_program(blob,args,memory,tid) for tid in range(64)]
                self.assertTrue(all(t['fence'] and t['stopped'] for t in traces))
                writes = [addr for t in traces for addr in t['writes']]
                self.assertEqual(len(writes),args[11])
                self.assertEqual(len(set(writes)),args[11])
                self.assertEqual(set(writes),set(range(args[7],args[7]+4*args[11],4)))
                memory.verify(names[-1])
                memory.verify_guards()
    def test_mutations(self):
        self.assertTrue((ROOT/'scripts/cnn/generate_kernels.py').is_file(), 'Kernel generator missing')
        from generate_kernels import generate_program
        from audit_kernels import audit_program
        from rv32_model import Memory, run_program
        m = Memory.fixture(FIXTURES/'main')
        args = m.args(2,('x','wconv','bconv','z'))
        blob = generate_program(2)
        for index in (0,100,4095):
            bad = bytearray(blob); bad[index] ^= 1
            with self.assertRaises(ValueError): audit_program(2,bad,args)
        with self.assertRaises(ValueError): audit_program(3,blob,args)
        words = list(struct.unpack('<1024I',blob))
        store = next(i for i,w in enumerate(words) if w&127==0x23)
        words[store] ^= 1<<9 # offset 4: aligned but wrong output index
        bad = struct.pack('<1024I',*words)
        trace = run_program(bad,args,m,0)
        with self.assertRaises(AssertionError): m.verify('z')
        self.assertNotEqual(trace['writes'],[args[7],args[7]+256])
        words[store] |= 2<<25 # offset68 at tid63 crosses payload end
        with self.assertRaises(ValueError): run_program(struct.pack('<1024I',*words),args,m,63)
        words = list(struct.unpack('<1024I',blob))
        branch = next(i for i,w in enumerate(words) if w&127==0x63)
        words[branch] = 0x00000063 # taken branch loops forever for disabled tail threads
        with self.assertRaises(ValueError): run_program(struct.pack('<1024I',*words),args,m,63,max_steps=1000)

if __name__=='__main__': unittest.main()

"""Synthetic ISA audit controls, not compiled program or GPU execution."""
import struct,unittest
from scripts.s8.audit_rv32 import call_graph
class TypedCallbacks(unittest.TestCase):
    def fixture(self):
        return (struct.pack('<II',0x000500e7,0x00008067),[dict(start=0x80000000,offset=0,file_bytes=8,flags=1)],{'_start':dict(value=0x80000000,size=4),'callback':dict(value=0x80000004,size=4)})
    def test_old_default_still_rejects_unresolved_indirect(self):
        raw,loads,functions=self.fixture()
        with self.assertRaisesRegex(ValueError,'unclosed indirect'):call_graph(raw,loads,functions,{},dict(_start=16,callback=32))
    def test_closed_targets_add_real_frame_and_cycles_reject(self):
        raw,loads,functions=self.fixture()
        result,_=call_graph(raw,loads,functions,{},dict(_start=16,callback=32),typed_indirect={'_start':['callback']})
        self.assertEqual(result['bytes'],48);self.assertEqual(result['edges']['_start'],['callback'])
        for targets in ([],['missing'],['_start']):
            with self.assertRaises(ValueError):call_graph(raw,loads,functions,{},dict(_start=16,callback=32),typed_indirect={'_start':targets})
if __name__=='__main__':unittest.main()

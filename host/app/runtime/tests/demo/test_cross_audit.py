import struct,unittest
from scripts.demo.audit_cross import validate_elf
def elf(machine=183):
    data=bytearray(128);data[:6]=b'\x7fELF\x02\x01';struct.pack_into('<HHIQQQIHHHHHH',data,16,2,machine,1,0x400000,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',data,64,1,5,0,0x400000,0x400000,128,128,4096)
    return data
class CrossTests(unittest.TestCase):
    def test_wrong_elf_machine_rejected(self):
        self.assertEqual(validate_elf(elf(),183)['machine'],183)
        for value in (elf(62),b'not ELF',elf()[:63]):
            with self.assertRaises(ValueError):validate_elf(value,183)
    def test_load_table_bounds_and_entry(self):
        for offset,value in [(24,0),(32,4096)]:
            data=elf();struct.pack_into('<Q',data,offset,value)
            with self.assertRaises(ValueError):validate_elf(data,183)
if __name__=='__main__':unittest.main()

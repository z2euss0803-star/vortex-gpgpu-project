import hashlib,struct,unittest
from scripts.mosaic.binding import ORDER,make_binding
from scripts.s8.binding import ORDER as OLD_ORDER,make_binding as old_binding
def inputs():return {name:hashlib.sha256(name.encode()).hexdigest() for name in ORDER}
class Binding(unittest.TestCase):
    def test_exact_512b_new_profile_and_program(self):
        d=inputs();b=make_binding(d)
        self.assertEqual(len(b),512)
        self.assertEqual(struct.unpack_from('<8I',b),(0x31424d56,1,512,6,12,1,1,0))
        for i,name in enumerate(ORDER):self.assertEqual(b[32+i*32:64+i*32],bytes.fromhex(d[name]))
        self.assertEqual(b[480:],bytes(32))
    def test_no_missing_extra_self_hash_or_zero_digest(self):
        d=inputs()
        for name,value in [('model','0'*64),('device_flat','A'*64),('bit','123'),('xsa',3)]:
            bad=dict(d);bad[name]=value
            with self.subTest(name=name),self.assertRaises(ValueError):make_binding(bad)
        bad=dict(d);bad.pop('device_elf')
        with self.assertRaises(ValueError):make_binding(bad)
        for name in ('binding_sha256','profile','program','image_sha256'):
            bad=dict(d);bad[name]='a'*64
            with self.assertRaises(ValueError):make_binding(bad)
    def test_old_program11_and_binding_unchanged(self):
        old={name:hashlib.sha256(name.encode()).hexdigest() for name in OLD_ORDER}
        b=old_binding(old)
        self.assertEqual(struct.unpack_from('<8I',b),(0x31423853,1,512,5,11,1,1,0))
        self.assertNotEqual(b,make_binding(inputs()))

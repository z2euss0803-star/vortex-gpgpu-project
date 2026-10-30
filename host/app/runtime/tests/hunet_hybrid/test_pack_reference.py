import struct,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
from pack_reference import pack_reference
class PackReferenceTests(unittest.TestCase):
 def job(self,k=1,pad=0):
  j=[0]*32;j[3]=1;j[7]=4*k*k;j[16]=1;j[18]=j[19]=j[20]=j[21]=2;j[22]=k;j[24]=1;j[25]=pad;return j
 def test_direct_padding_and_rne(self):
  data=struct.pack('<4f',1.0,1.00048828125,2.0,-0.0);j=self.job();self.assertEqual(pack_reference(j,(1,4,1,8,4,1,0,4),data),struct.pack('<4H',0x3c00,0x3c00,0x4000,0x8000))
  self.assertEqual(pack_reference(j,(1,4,1,8,4,1,2,2),data),struct.pack('<4e',2,-0.0,0,0))
 def test_im2col_spatial_padding(self):
  j=self.job(3,1);r=pack_reference(j,(1,4,9,8,4,9,0,4),struct.pack('<4f',1,2,3,4));v=struct.unpack('<36e',r);self.assertEqual(v[:4],(0,0,0,1));self.assertEqual(v[16:20],(1,2,3,4));self.assertEqual(v[-4:],(4,0,0,0))
 def test_invalid_size_or_overflow_stops(self):
  with self.assertRaises(ValueError):pack_reference(self.job(),(1,4,1,8,4,1,0,4),b'')
  with self.assertRaises((OverflowError,ValueError)):pack_reference(self.job(),(1,4,1,8,4,1,0,4),struct.pack('<4f',1e10,0,0,0))
if __name__=='__main__':unittest.main()

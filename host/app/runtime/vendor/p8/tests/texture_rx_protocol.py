"""Real native-C receiver tests. --dll accepts an independently compiled DLL."""
import argparse
import ctypes as C
import struct
import unittest
import zlib

SMALL=512*512*4
LARGE=2048*2048*4
CHUNK=1200
def packet(op,session=123,tx=1,lod=0,index=0,data=b'',small_crc=0,large_crc=0,crc=None):
    return struct.pack('!IBBH14I',0x56545831,1,op,0,session,tx,lod,index,len(data),zlib.crc32(data) if crc is None else crc,small_crc,large_crc,0,0,0,0,0,0)+data
def words(p):
    assert len(p)==64, 'receiver must return a complete bounded 64-byte status'
    return struct.unpack('!IBBH14I',p)

class Ready(C.Structure):
    _fields_=[(x,C.c_uint32) for x in ('session','tx_id','small_crc','large_crc')]

class Receiver(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(DLL)
        cls.lib.tex_rx_init.argtypes=[C.c_void_p,C.c_void_p,C.c_uint32]
        cls.lib.tex_rx_handle.argtypes=[C.c_void_p,C.c_size_t,C.c_uint32,C.c_uint16,C.c_uint64,C.c_void_p,C.c_size_t]
        cls.lib.tex_rx_handle.restype=C.c_size_t
        cls.lib.tex_rx_poll.argtypes=[C.c_uint64]
        cls.lib.tex_rx_get_ready.argtypes=[C.POINTER(Ready)]
        cls.lib.tex_rx_applied.argtypes=[C.c_uint32]*6
        cls.lib.tex_rx_failed.argtypes=[C.c_uint32]*2
    def setUp(self):
        self.small=C.create_string_buffer(SMALL+128)
        self.large=C.create_string_buffer(LARGE+128)
        C.memset(C.addressof(self.small),0xA5,SMALL+128)
        C.memset(C.addressof(self.large),0xA5,LARGE+128)
        self.lib.tex_rx_init(C.addressof(self.small)+64,C.addressof(self.large)+64,123)
        self.now=0
    def handle(self,p,peer=1,port=9,capacity=64):
        buf=C.create_string_buffer(128)
        n=self.lib.tex_rx_handle(p,len(p),peer,port,self.now,buf,capacity)
        self.assertLessEqual(n,capacity)
        return bytes(buf[:n])
    def ready(self):
        r=Ready(); return self.lib.tex_rx_get_ready(C.byref(r)),r
    def state(self): return words(self.handle(packet(4)))[12]
    def begin(self,s,l): return self.handle(packet(2,small_crc=zlib.crc32(s),large_crc=zlib.crc32(l)))
    def send_lod(self,lod,data,reverse=False):
        indices=range((len(data)+CHUNK-1)//CHUNK)
        if reverse: indices=reversed(indices)
        for i in indices:
            r=self.handle(packet(3,lod=lod,index=i,data=data[i*CHUNK:(i+1)*CHUNK]))
            self.assertEqual(len(r),64)
    def guards(self):
        for b,n in ((self.small,SMALL),(self.large,LARGE)):
            self.assertEqual(bytes(b[:64]),b'\xa5'*64)
            self.assertEqual(bytes(b[n+64:n+128]),b'\xa5'*64)
    def test_handshake_exposes_session_without_binding_peer(self):
        r=words(self.handle(packet(1,session=0)))
        self.assertEqual(r[4],123)
        self.assertEqual(r[12],0)
        self.assertEqual(self.ready()[0],0)
    def test_bad_and_truncated_packets_never_write(self):
        self.begin(bytes(SMALL),bytes(LARGE))
        good=packet(3,data=b'\0'*1200)
        for p in [b'',good[:63],good[:-1],b'\0'*64,good+b'\0',packet(3,index=0xffffffff,data=b'\0'*1200),packet(3,lod=2,data=b'\0'*1200),packet(3,data=b'\0'*1201),packet(3,data=b'\0'*1200,crc=1),packet(3,session=122,data=b'\0'*1200)]:
            self.assertEqual(self.handle(p),b'')
        self.assertEqual(bytes(self.small[64:1264]),b'\xa5'*1200)
        self.assertEqual(self.handle(good,peer=2),b'')
        self.assertEqual(self.handle(good,port=10),b'')
        self.assertEqual(self.handle(good,capacity=63),b'')
        self.guards()
    def test_reverse_order_duplicate_and_two_lod_atomic_full_bytes(self):
        s=bytes(range(256))*(SMALL//256)
        l=bytes(reversed(range(256)))*(LARGE//256)
        self.begin(s,l)
        self.send_lod(0,s,True)
        self.assertEqual(self.ready()[0],0)
        p=packet(3,data=s[:1200]); self.handle(p); self.handle(p)
        # A valid conflicting duplicate must not overwrite first accepted block.
        self.assertEqual(self.handle(packet(3,data=b'X'*1200)),b'')
        self.send_lod(1,l,True)
        valid,r=self.ready(); self.assertEqual(valid,1)
        self.assertEqual((r.small_crc,r.large_crc),(zlib.crc32(s),zlib.crc32(l)))
        self.assertEqual(bytes(self.small[64:64+SMALL]),s)
        self.assertEqual(bytes(self.large[64:64+LARGE]),l)
        self.assertEqual(self.state(),2)
        # READY locks staging against new transaction, peer and current payload.
        self.assertEqual(self.handle(packet(2,tx=2)),b'')
        self.assertEqual(self.handle(packet(3,tx=2,data=b'X'*1200)),b'')
        self.lib.tex_rx_applied(99,1,2,3,25,4)
        self.assertEqual(self.state(),2)
        self.lib.tex_rx_applied(1,0x11223344,0x55667788,3,25,17)
        r=words(self.handle(packet(4)))
        self.assertEqual(r[12:],(3,0x11223344,0x55667788,3,25,17))
        self.assertEqual(self.ready()[0],0)
        self.assertEqual(self.handle(packet(3,data=b'X'*1200)),b'')
        self.guards()
    def test_image_crc_failure_never_ready(self):
        s=bytes(SMALL);l=bytes(LARGE)
        self.handle(packet(2,small_crc=1,large_crc=zlib.crc32(l)))
        self.send_lod(0,s)
        self.assertEqual(self.ready()[0],0)
        self.assertEqual(self.state(),4)
        self.guards()
    def test_timeout_rejects_old_transaction_and_foreign_peer(self):
        self.begin(bytes(SMALL),bytes(LARGE))
        self.now=15001;self.lib.tex_rx_poll(self.now)
        self.assertEqual(self.state(),4)
        self.assertEqual(self.handle(packet(2)),b'')
        self.assertEqual(self.handle(packet(3,data=bytes(1200))),b'')
        self.assertEqual(self.handle(packet(2,tx=2,session=122)),b'')
        self.assertEqual(len(self.handle(packet(2,tx=2),peer=2)),64)
        self.assertEqual(self.handle(packet(3,tx=2,data=bytes(1200)),peer=1),b'')
        self.guards()
    def test_large_image_crc_failure_after_small_complete_never_ready(self):
        s=bytes(SMALL);l=bytes(LARGE)
        self.handle(packet(2,small_crc=zlib.crc32(s),large_crc=1))
        self.send_lod(0,s);self.send_lod(1,l)
        self.assertEqual(self.ready()[0],0)
        self.assertEqual(self.state(),4)
        self.guards()
    def test_acquired_ready_pins_cancel_and_new_begin_until_applied_or_failed(self):
        self.assertTrue(hasattr(self.lib,'tex_rx_acquire_ready'),'READY acquisition must atomically pin its transaction before reentrant network polling')
        acquire=self.lib.tex_rx_acquire_ready;acquire.argtypes=[C.POINTER(Ready)]
        s=bytes(SMALL);l=bytes(LARGE);self.begin(s,l);self.send_lod(0,s);self.send_lod(1,l)
        ready=Ready();self.assertEqual(acquire(C.byref(ready)),1)
        for _ in range(3):
            cancelled=words(self.handle(packet(5)))
            self.assertEqual((cancelled[5],cancelled[12]),(1,2))
            self.assertEqual(self.handle(packet(2,tx=2)),b'')
            self.assertEqual((self.ready()[1].tx_id,self.state()),(1,2))
        self.lib.tex_rx_applied(1,1,2,0,99,3)
        self.assertEqual(len(self.handle(packet(2,tx=2,small_crc=zlib.crc32(s),large_crc=zlib.crc32(l)))),64)
        self.send_lod_for_tx(2,0,s);self.send_lod_for_tx(2,1,l)
        self.assertEqual(acquire(C.byref(ready)),1)
        self.lib.tex_rx_failed(2,10)
        self.assertEqual(self.ready()[0],0)
        self.assertEqual(len(self.handle(packet(2,tx=3))),64)
        self.guards()
    def send_lod_for_tx(self,tx,lod,data):
        for index in range((len(data)+CHUNK-1)//CHUNK):
            self.assertEqual(len(self.handle(packet(3,tx=tx,lod=lod,index=index,data=data[index*CHUNK:(index+1)*CHUNK]))),64)
    def test_total_deadline_and_cancel_prevent_ready(self):
        self.begin(bytes(SMALL),bytes(LARGE))
        for now in range(10000,180001,10000):
            self.now=now;self.handle(packet(3,data=bytes(1200)))
        self.now=180001;self.lib.tex_rx_poll(self.now)
        self.assertEqual(self.state(),4)
        self.handle(packet(2,tx=2)); self.assertEqual(len(self.handle(packet(5,tx=2))),64)
        self.assertEqual(self.ready()[0],0)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--dll',required=True);args,rest=ap.parse_known_args();DLL=args.dll
    unittest.main(argv=['texture_rx_protocol']+rest)

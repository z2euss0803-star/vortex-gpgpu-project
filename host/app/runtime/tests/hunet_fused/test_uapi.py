"""Compare real compiled wire bytes against an independent LE fixture."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]


class FusedUapi(unittest.TestCase):
    def test_layout_and_cross_mode_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary=pathlib.Path(temporary)/'uapi'
            raw=pathlib.Path(temporary)/'wire.bin'
            compile_result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-Wl,--gc-sections',
                '-Iinclude','tests/hunet_fused/test_uapi.c','core/hunet_fused_validate.c',
                '-o',str(binary)],cwd=str(ROOT),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(compile_result.returncode,0,compile_result.stdout.decode())
            run=subprocess.run([str(binary),str(raw)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(run.returncode,0,run.stdout.decode())
            self.assertIn(b'FUSED_FIXED_ABI_AND_CROSS_MODE_REJECTION_PASS_NOT_BOARD',run.stdout)
            stage=[0x53554648,1,128,15,1,96,0,32,0x85000040,0x80800000,0,
                   0x850300c0,0x86c4df40,131072,0x86d0df80,0x86d12e40,
                   512,3072,3,24,128,128,64,64,2,2,0,0,3072,1,1,0]
            descriptor=[24,4096,12,24,128,16,0,128,0,0,0,1,3,0,0,0]
            binding=(struct.pack('<8I',0x44424c48,3,512,4,9,15,0,0)+
                     b'\x11'*224+b'\x22'*96+b'\0'*32+b'\x33'*96+b'\0'*32)
            self.assertEqual(raw.read_bytes(),struct.pack('<32I',*stage)+
                             struct.pack('<16I',*descriptor)+binding)


if __name__=='__main__':unittest.main()

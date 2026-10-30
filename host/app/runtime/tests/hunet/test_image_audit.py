"""Actual container bytes and final-DT properties, never board evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))


def boot_fixture(payload):
    data=bytearray(0x210+len(payload))
    def words(off, values):struct.pack_into('<%dI'%len(values),data,off,*values)
    words(0x20,[0xaa995566,0x584c4e58,0,0,0x200,0,0,16,16,0])
    words(0x48,[(~sum(struct.unpack_from('<10I',data,0x20)))&0xffffffff])
    words(0x98,[0x100,0x140])
    iht=[0x01020000,2,0x140//4,0,0,0]+[0]*9
    words(0x100,iht+[(~sum(iht))&0xffffffff])
    for offset, start, size, attr, next_ in [(0x140,0x200,16,0x10,0x180//4),(0x180,0x210,len(payload),0x20,0)]:
        values=[size//4]*3+[next_,0,0,0,0,start//4,attr,1,0,0,0,0]
        words(offset,values+[(~sum(values))&0xffffffff])
    data[0x200:0x210]=b'FSBL_VALID_DATA!!';data[0x210:]=payload
    return bytes(data)


class ImageAudit(unittest.TestCase):
    def test_actual_built_in_symbols(self):
        m=self.module()
        # Faithful System.map excerpt: static poll_gpu, no vxl_hunet_poll.
        symbols='\n'.join('ffff800000%06x %s %s'%(i,'t' if n=='poll_gpu' else 'T',n)
            for i,n in enumerate(('vxl_engine_poll','vxl_hunet_attach','poll_gpu','vxl_hunet_command')))
        self.assertEqual(m.kernel_symbol_errors(symbols),[])
        for name in ('vxl_engine_poll','vxl_hunet_attach','poll_gpu','vxl_hunet_command'):
            self.assertTrue(m.kernel_symbol_errors('\n'.join(s for s in symbols.splitlines() if not s.endswith(' '+name))))

    def module(self):
        path=ROOT/'scripts/hunet/image_audit.py'
        self.assertTrue(path.is_file(),'HW4 actual image auditor missing')
        spec=importlib.util.spec_from_file_location('hunet_image_audit',path)
        value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value)
        return value

    def test_wrong_boot_bit(self):
        m=self.module();expected=b'\x66\x55\x99\xaa'+b'\0'*60
        self.assertEqual(m.boot_bit_errors(boot_fixture(expected),expected),[])
        old=b'\x66\x55\x99\xaa'+b'\xff'*60
        self.assertTrue(m.boot_bit_errors(boot_fixture(old)+expected,expected))
        bad=bytearray(boot_fixture(expected));bad[0x1bc]^=1
        self.assertTrue(m.boot_bit_errors(bad,expected))
        self.assertTrue(m.boot_bit_errors(boot_fixture(expected)[:-1],expected))

    def props(self):
        return dict(compatible='vortex,hw4-hunet-v5',resources=[0xa0000000,65536,0xb0010000,65536],
            pool=[0x10000000,0x100000],pool_type='shared-dma-pool',no_map=True,placement=True,
            pool_link=True,ready_link=True,clock_id=71,reset_id=116,gpio_clock_id=71,
            cdma_owners=0,old_gpu_disabled=True,memory=[[0,0x80000000],[0x800000000,0x80000000]],
            sd_variant='sd-compat-two-flags-trial-a1',sd_no_1_8_v=True,sd_disable_wp=True,
            bootargs='earlycon console=ttyPS0,115200 root=/dev/ram0 rw')

    def test_duplicate_cdma_owner(self):
        m=self.module();good=self.props();self.assertEqual(m.dt_errors(good),[])
        for key,value in [('cdma_owners',1),('ready_link',False),('clock_id',70),('reset_id',115),('old_gpu_disabled',False),('compatible','vortex,h0-r4-v1')]:
            bad=copy.deepcopy(good);bad[key]=value;self.assertTrue(m.dt_errors(bad),key)

    def test_ps_pool_mismatch(self):
        m=self.module()
        for key,value in [('pool',[0x10000000,0x200000]),('pool_link',False),('no_map',False),('sd_disable_wp',False),('memory',[[0,0x80000000],[0x500000000,0x80000000]])]:
            bad=self.props();bad[key]=value;self.assertTrue(m.dt_errors(bad),key)

    def test_actual_rootfs_binding(self):
        m=self.module();release=dict(source_commit='ab'*20,source_digest='12'*32,bit_sha256='34'*32,xsa_sha256='56'*32,binding_sha256='78'*32)
        build='HW4-HUNET-V5-LINUX-20261011T140000Z';binding=b'B'*256
        expected={'binding.bin':binding,'program6/code.bin':b'new crt code'}
        manifest=dict(scope='HW4_HUNET_V5_LINUX_CANDIDATE_V1',build_id=build,profile=4,sd_variant='sd-compat-two-flags-trial-a1',**release)
        files={
            'etc/codex-build-id':(build+'\n').encode(),
            'etc/codex-hunet-manifest.json':json.dumps(manifest).encode(),
            'etc/network/interfaces':b'auto eth0\niface eth0 inet static\n address 192.168.13.10\n netmask 255.255.255.0\n',
            'etc/init.d/dropbear':b'start ssh service',
            'usr/sbin/dropbear':b'ELF placeholder',
            'home/root/.ssh/authorized_keys':b'ssh-rsa PUBLICKEY\n',
            **{'usr/share/codex-hunet-v5/'+k:v for k,v in expected.items()}}
        self.assertEqual(m.rootfs_binding_errors(files,manifest,expected,b'ssh-rsa PUBLICKEY\n'),[])
        for name in ('etc/codex-build-id','etc/codex-hunet-manifest.json','home/root/.ssh/authorized_keys','etc/init.d/dropbear','usr/share/codex-hunet-v5/program6/code.bin'):
            bad=dict(files);bad[name]=b'OLD'
            self.assertTrue(m.rootfs_binding_errors(bad,manifest,expected,b'ssh-rsa PUBLICKEY\n'),name)
        bad=dict(files);del bad['usr/sbin/dropbear'];self.assertTrue(m.rootfs_binding_errors(bad,manifest,expected,b'ssh-rsa PUBLICKEY\n'))


if __name__=='__main__':unittest.main()

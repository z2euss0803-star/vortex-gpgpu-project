"""Catch B aliasing, metadata/guard corruption, and missing output elements."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_fused'))
from catalog import freeze_catalog,compile_schedule


class FusedLayout(unittest.TestCase):
    def setUp(self):
        path=ROOT/'scripts/hunet_fused/layout.py'
        self.assertTrue(path.is_file(),'fusion layout compiler not implemented')
        spec=importlib.util.spec_from_file_location('fusion_layout',str(path))
        self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        self.contract=freeze_catalog(ROOT)
        self.schedule=compile_schedule(self.contract)
        self.layout=self.module.compile_layout(self.contract,self.schedule)

    def test_pinned_capacity_and_original_outputs(self):
        self.assertEqual(self.layout['workspace_bytes'],33976320)
        self.assertEqual(self.layout['packed_b']['start'],0x86c4df40)
        self.assertEqual(self.layout['packed_b']['bytes'],786432)
        self.assertEqual(self.layout['descriptors']['bytes'],20096)
        self.assertEqual(self.layout['owner_trace']['bytes'],49152)
        old=json.loads((ROOT/'generated/hunet/catalog.json').read_text())['stages']
        self.assertEqual(self.layout['stage_outputs'],old)
        self.assertEqual(self.layout['workspace_base'],0x85000000)
        self.assertEqual(self.layout['descriptors']['start'],0x86d0df80)
        self.assertEqual(self.layout['owner_trace']['start'],0x86d12e40)

    def test_partitions_guards_and_32bit_overflow(self):
        for stage in self.layout['matrix_stages']:
            end=0
            for descriptor in stage['descriptors']:
                self.assertEqual(descriptor[8]%64,0)
                self.assertGreaterEqual(descriptor[8],end)
                end=descriptor[8]+descriptor[4]*descriptor[5]*2
            self.assertLessEqual(end,786432)
        for name in ('packed_b','descriptors','owner_trace'):
            broken=copy.deepcopy(self.layout)
            broken[name]['start']=0xfffffff0
            with self.subTest(name=name):
                with self.assertRaises(ValueError):self.module.validate_layout(broken)
        broken=copy.deepcopy(self.layout)
        broken['descriptors']['start']=broken['packed_b']['start']+64
        with self.assertRaises(ValueError):self.module.validate_layout(broken)
        broken=copy.deepcopy(self.layout);broken['owner_trace']['bytes']+=4
        with self.assertRaises(ValueError):self.module.validate_layout(broken)

    def test_all_descriptor_outputs_written_once(self):
        for stage in self.layout['matrix_stages']:
            first=stage['descriptors'][0];m,n=first[:2]
            covered=bytearray(m*n)
            for descriptor in stage['descriptors']:
                dm,dn,k,mp,np,kp,offset,count=descriptor[:8]
                self.assertEqual((dm,dn),(m,n))
                for tile in range((mp//8)*(np//4)):
                    tile_row=(tile//(np//4))*8;tile_col=(tile%(np//4))*4
                    for lane in range(4):
                        for reg in range(8):
                            row=tile_row+lane//2+(reg//2)*2
                            col=tile_col+lane%2+(reg%2)*2
                            if row<m and col<count:
                                index=row*n+offset+col
                                self.assertEqual(covered[index],0)
                                covered[index]=1
            self.assertEqual(covered.count(1),m*n)

    def test_emission_deterministic_and_digest_bound(self):
        path=ROOT/'scripts/hunet_fused/generate.py'
        self.assertTrue(path.is_file(),'fusion artifact emitter not implemented')
        spec=importlib.util.spec_from_file_location('fusion_generate',str(path))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        temporary_root=ROOT/'build/test-tmp'
        temporary_root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(temporary_root)) as directory:
            out=pathlib.Path(directory)
            a=module.emit_artifacts(self.layout,self.schedule,out/'a')
            b=module.emit_artifacts(self.layout,self.schedule,out/'b')
            self.assertEqual(a,b)
            self.assertEqual(len((out/'a/jobs.bin').read_bytes()),103*128)
            self.assertEqual(len((out/'a/descriptors.bin').read_bytes()),314*64)
            stage0=struct.unpack('<32I',(out/'a/jobs.bin').read_bytes()[:128])
            self.assertEqual(stage0[:8],(0x53554648,1,128,15,1,96,0,32))
            self.assertEqual(stage0[16:18],(512,3072))
            old_digest=a['layout_sha256']
            changed=copy.deepcopy(self.layout);changed['scope']='altered identity'
            changed_digest=hashlib.sha256(module.canonical_bytes(changed)).hexdigest()
            self.assertNotEqual(old_digest,changed_digest)
            for artifact,want in a['files'].items():
                self.assertEqual(hashlib.sha256((out/'a'/artifact).read_bytes()).hexdigest(),want)


if __name__=='__main__':unittest.main()

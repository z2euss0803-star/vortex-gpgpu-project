import copy
import json
from pathlib import Path
import unittest
from demo.contracts import validate_asset, validate_frame, validate_run


def fixture():
    return dict(schema=1,run_id='1'*32,app='mosaic',boot_id='OFFLINE',generation=1,
                build_id='OFFLINE-TEST',scope='OFFLINE',diagnostics=False,shape=[1,3,8,8],
                state='RESULT_VALIDATED',numeric_class='NOT_EVALUATED',cycle_valid=False,
                timing_valid=False,raw_cycles=None,artifacts=[],
                identities={key:'a'*64 for key in ('hardware','source','program','model','input','quant','layout')})


class Contracts(unittest.TestCase):
    def test_reject_frame_truncated_or_wrong_run(self):
        f=dict(schema=1,run_id='1'*32,app='mosaic',source_result_sha='b'*64,
               width=2,height=2,stride=8,format='RGB8',bytes=16,sha256='c'*64,
               layout='HWC_RGB8',numeric_class='NOT_EVALUATED')
        self.assertEqual(validate_frame(f)['bytes'],16)
        for key,value in [('width',1921),('height',0),('bytes',15),('stride',2**64),('run_id','old'),('app','ocr')]:
            bad=dict(f);bad[key]=value
            with self.assertRaises(ValueError):validate_frame(bad)

    def test_utf8_asset_name_is_not_remote_path(self):
        a=dict(role='input',bytes=12,sha256='a'*64,name='中文 输入.bin',shape=[1,1,3],dtype='FP32')
        self.assertEqual(validate_asset(a)['name'],'中文 输入.bin')
        for name in ('../x','/x','C:/x','x\\y','x/../y','x\x00y'):
            bad=dict(a,name=name)
            with self.assertRaises(ValueError):validate_asset(bad)
        for change in ({'bytes':11},{'bytes':64*1024*1024+1},{'shape':[2**63,2**63]}):
            bad=dict(a);bad.update(change)
            with self.assertRaises(ValueError):validate_asset(bad)

    def test_u64_ns_no_float_roundtrip(self):
        r=fixture();r['timings_ns']={'decode':9007199254740993}
        self.assertEqual(validate_run(json.loads(json.dumps(r)))['timings_ns']['decode'],9007199254740993)
        r['timings_ns']['decode']=float(9007199254740993)
        with self.assertRaises(ValueError):validate_run(r)

    def test_missing_sha_app_and_diagnostics_rejected(self):
        for key in ('diagnostics','app','identities','scope','raw_cycles'):
            r=fixture();del r[key]
            with self.assertRaises(ValueError):validate_run(r)
        r=fixture();r['identities']['input']='a'*63
        with self.assertRaises(ValueError):validate_run(r)

    def test_offline_never_claims_cycles(self):
        for change in ({'raw_cycles':127273},{'cycle_valid':True},{'scope':'REAL_BOARD','boot_id':'OFFLINE'}):
            r=fixture();r.update(change)
            with self.assertRaises(ValueError):validate_run(r)

    def test_unknown_schema_boolean_integer_and_oversize_json(self):
        for key,value in [('schema',2),('generation',True),('run_id','A'*32),('raw_cycles',-1)]:
            r=fixture();r[key]=value
            with self.assertRaises(ValueError):validate_run(r)
        r=fixture();r['padding']='x'*(256*1024)
        with self.assertRaises(ValueError):validate_run(r)

    def test_golden_codec_fixture(self):
        p=Path('tests/demo/fixtures/run-v1.json')
        self.assertEqual(validate_run(json.loads(p.read_text(encoding='utf-8')))['timings_ns']['decode'],9007199254740993)
        self.assertEqual(validate_run(json.loads(p.read_text(encoding='utf-8')))['timings_ns']['counter_bound'],18446744073709551615)


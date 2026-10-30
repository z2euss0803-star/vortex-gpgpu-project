import importlib.util,pathlib,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]

class TimingAnalysis(unittest.TestCase):
    def module(self):
        p=ROOT/'scripts/hunet_timing/analyze.py'
        self.assertTrue(p.exists(),'missing diagnostic timing analyzer')
        s=importlib.util.spec_from_file_location('timing_analyze',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
    def row(self):
        return dict(generation=2,job_id=1,sequence=1,op=1,stage=0,result=0,flags=0,guard_depth=0,
          submit_enter_ns=100,submit_return_ns=200,wait_enter_ns=210,wait_return_ns=350,
          gpu_start_ns=180,gpu_done_observed_ns=300,ack_end_ns=340,elapsed_cycles=100,
          guard_init_ns=30,guard_check_ns=20,guard_cdma_ns=40,guard_cdma_calls=130,guard_cdma_bytes=8320,
          guard_cdma_sleep_ns=15,cdma_ns=40,cdma_calls=130,cdma_bytes=8320,cdma_sleep_ns=15,
          state_poll_ns=10,state_poll_calls=4,wait_sleep_ns=80,worker_poll_ns=40)
    def test_exclusive_timeline_keeps_polling_and_worker_nested(self):
        d=self.module().analyze_rows([self.row()],1000000000)
        self.assertEqual(d['timeline_ns'],dict(guard_init=30,handoff_before_start=50,gpu_active_observed=120,guard_check=20,handoff_after_done=30))
        self.assertEqual(d['total_kernel_span_ns'],250)
        self.assertEqual(d['nested_ns']['state_poll'],10)
        self.assertEqual(d['non_hardware_conditional_ns'],150)
    def test_fault_or_incomplete_record_rejected_without_synthetic_pass(self):
        m=self.module()
        for key,value in [('flags',1),('guard_depth',1),('wait_return_ns',0),('guard_cdma_calls',129),('guard_cdma_ns',1000),('result',(1<<64)-5)]:
            r=self.row();r[key]=value
            with self.subTest(field=key),self.assertRaises(ValueError):m.analyze_rows([r],1000000000)
    def test_duplicate_jobs_and_missing_clock_rejected_or_explicit_unknown(self):
        m=self.module()
        with self.assertRaises(ValueError):m.analyze_rows([self.row(),self.row()],1000000000)
        d=m.analyze_rows([self.row()],None)
        self.assertIsNone(d['gpu_active_conditional_ns'])
        self.assertIsNone(d['non_hardware_conditional_ns'])

    def test_fused345_contract_and_real_saved_cycle_identity(self):
        m=self.module();rows=[r for r in m.read_trace(ROOT/'tests/hunet_timing/fixtures/fused-generation17.csv') if r['generation']==17]
        result=m.analyze_rows(rows,None,program_id=9)
        self.assertEqual(result['jobs'],103)
        self.assertEqual(result['raw_gpu_cycles'],835466086)
        self.assertEqual(result['total_kernel_span_ns'],13172797827)
        with self.assertRaises(ValueError):m.analyze_rows(rows,None,program_id=8)
    def test_uint64_ranges_and_fast_poll_containment(self):
        m=self.module()
        for key,value in [('elapsed_cycles',-1),('cdma_sleep_ns',2**64),('generation',-1),('cdma_fast_poll_ns',30)]:
            r=self.row();r[key]=value
            with self.subTest(field=key),self.assertRaises(ValueError):m.analyze_rows([r],None)
    def test_explicit_v2_columns_and_length(self):
        import tempfile
        m=self.module()
        with tempfile.TemporaryDirectory() as t:
            p=pathlib.Path(t)/'trace.csv';fields=list(m.TRACE_V2_FIELDS)
            body=','.join(fields)+'\n'+','.join('0' for _ in fields)+'\n'
            p.write_text('#VXL_SUBMIT_TIMING_V2 capacity=2048 used=1 flags=0 active=0\n'+body)
            self.assertEqual(len(m.read_trace(p)),1)
            p.write_text('#VXL_SUBMIT_TIMING_V2 capacity=2048 used=1 flags=0 active=0\n'+body.replace('cdma_fast_poll_ns','unknown_fast'))
            with self.assertRaises(ValueError):m.read_trace(p)

    def test_fast_poll_units_bounds_and_aggregate_overflow(self):
        m=self.module();r=self.row();r.update(cdma_fast_poll_ns=20,cdma_fast_poll_calls=3)
        result=m.analyze_rows([r],None)
        self.assertNotIn('cdma_fast_poll_calls',result['nested_ns'])
        self.assertEqual(result['counts']['cdma_fast_poll_calls'],3)
        r['cdma_fast_poll_calls']=64*r['cdma_calls']+1
        with self.assertRaises(ValueError):m.analyze_rows([r],None)
        r.update(cdma_fast_poll_ns=0,cdma_fast_poll_calls=2**64-1,cdma_calls=2**64-1)
        second=dict(r,job_id=2)
        with self.assertRaises(ValueError):m.analyze_rows([r,second],None)

if __name__=='__main__':unittest.main()

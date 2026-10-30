import copy,json,struct,tempfile,unittest
from scripts.hunet_opt.summary import partition,validate_management,internal_cycles,replay_history,validate_submit,summarize,gn_internal,management_epoch,load_breakdown,digest_breakdown,kernel_digest_breakdown
from scripts.hunet_opt.catalog import compile_catalog
from pathlib import Path
class Timing(unittest.TestCase):
    def test_two_generations_select_without_rewriting_raw(self):
        row=dict(generation=2,owner_cookie=1,token=1,command=19,start_ns=100,end_ns=200,total_ns=100,result=0,flags=0,exclusive=dict(read=20),nested=dict(cdma=15))
        previous=dict(row,generation=1,start_ns=1,end_ns=20,total_ns=19)
        raw=[previous,row];selected=management_epoch(raw,2)
        self.assertEqual(validate_management(selected,2,[(19,1)])['rows'],1);self.assertEqual(len(raw),2)
        with self.assertRaises(ValueError):management_epoch(raw,3)
    def test_load_and_digest_evidence_fail_closed(self):
        rows=[]
        for asset in range(13):
            size=512 if asset==0 else 23685056 if asset==12 else 10
            rows.append((asset,0,1,size,1,0))
            if asset:rows.append((asset,1,1,size,1,0))
        rows.extend([(13,2,1,100,4,0),(11,3,1,10,1,0),(12,4,1,23685056,1,0),(14,5,1,0,1,0)])
        raw=bytearray(16+2616);struct.pack_into('<4I',raw,0,0x4c4f5856,1,len(raw),0);struct.pack_into('<3I',raw,16,len(rows),0,14);struct.pack_into('<4Qi',raw,32,100,1000,999,0,0)
        for i,row in enumerate(rows):struct.pack_into('<IIQQQi',raw,72+40*i,*row)
        r=load_breakdown(bytes(raw),1000);self.assertGreater(r['residual_ns'],0)
        for offset in (0,4,8,20,64):
            bad=bytearray(raw);bad[offset]^=1
            with self.assertRaises(ValueError):load_breakdown(bytes(bad),1000)
        with self.assertRaises(ValueError):load_breakdown(bytes(raw),1)
        d=bytearray(16+392);struct.pack_into('<4I',d,0,0x44475856,1,len(d),0);struct.pack_into('<4Q',d,16+96,13,80000000,0,0);d[16+96+32:16+96+35]=b'sha'
        self.assertEqual(digest_breakdown(bytes(d))['backends'][0]['calls'],13)
        for offset in (0,16+384,16+96+16):
            bad=bytearray(d);bad[offset]^=1
            with self.assertRaises(ValueError):digest_breakdown(bytes(bad))
        csv='generation,backend,implementation,calls,bytes,failures,fallbacks,overflow\n2,3,sha256-ce,14,123,0,0,0\n'
        self.assertEqual(kernel_digest_breakdown(csv,2,True)['rows'][0]['backend'],3)
        for bad in (csv.replace('2,3,','1,3,'),csv.replace(',0,0,0\n',',1,0,0\n'),csv.splitlines()[0]+'\n'):
            with self.assertRaises(ValueError):kernel_digest_breakdown(bad,2,True)
    def test_exclusive_nested_closure(self):
        r=partition(100,dict(read=20,sha=30),dict(cdma=18,wait_sleep=80))
        self.assertEqual(r['residual_ns'],50);self.assertFalse(r['nested_additive'])
        for args in [(100,dict(read=101),{}),(100,dict(cdma=10),dict(cdma=5)),(2**64,{},{}),(10,dict(read=-1),{})]:
            with self.assertRaises(ValueError):partition(*args)
    def test_management_identity_and_completeness(self):
        row=dict(generation=2,owner_cookie=1,token=1,command=19,start_ns=100,end_ns=200,total_ns=100,result=0,flags=0,exclusive=dict(read=20),nested=dict(cdma=15))
        self.assertEqual(validate_management([row],2,[(19,1)])['rows'],1)
        for changes in [dict(flags=32),dict(flags=4),dict(flags=16),dict(generation=3),dict(end_ns=99),dict(exclusive=dict(read=101))]:
            bad=dict(row,**changes)
            with self.assertRaises(ValueError):validate_management([bad],2,[(19,1)])
        with self.assertRaises(ValueError):validate_management([],2,[(19,1)])
        with self.assertRaises(ValueError):validate_management([row,row],2,[(19,1)])
        with self.assertRaises(ValueError):validate_management([row,dict(row,token=2,start_ns=200,end_ns=300)],2,[(19,1)])
    def test_internal_is_not_summed_wall_time(self):
        r=internal_cycles([[10,20],[9,21],[11,19],[8,22]],[[[50,5]]*4]*4)
        self.assertEqual(r['per_core_wall_cycles'][0],[10,20]);self.assertIsNone(r['global_wall_cycles'])
        self.assertFalse(r['warp_activity_additive'])
    def test_real_historical_raw_replay(self):
        root=Path(__file__).parent/'fixtures/c0-timing';r=replay_history(root)
        self.assertEqual(r['timing']['raw_gpu_cycles'],686600903)
        self.assertEqual(r['timing']['T_load_ns'],4031236701)
        self.assertEqual(r['timing']['T_resident_ns'],9270748114)
        self.assertEqual(r['operators']['4']['raw_cycles'],364941320)
        self.assertEqual(r['operators']['15']['raw_cycles'],302110456)
        self.assertIsNone(r['new_candidate_gpu_cycles'])
    def test_catalog_trace_flags_count_and_identity(self):
        c=compile_catalog('G3_NORM');rows=[]
        for i,e in enumerate(x for x in c['entries'] if x['kind']=='GPU'):
            start=1000+i*10000;count=349+(2 if e['op']==16 else 0)
            rows.append(dict(generation=2,job_id=i+1,sequence=e['first'],op=e['op'],stage=e['first_stage'],flags=0,result=0,guard_depth=0,submit_enter_ns=start,submit_return_ns=start+2100,wait_enter_ns=start+2200,gpu_start_ns=start+2000,gpu_done_observed_ns=start+6000,ack_end_ns=start+7000,wait_return_ns=start+8000,elapsed_cycles=100,guard_cdma_calls=count,guard_cdma_bytes=count*64,guard_init_ns=100,guard_check_ns=200,guard_cdma_sleep_ns=5,guard_cdma_ns=20,cdma_ns=100,cdma_sleep_ns=5,cdma_calls=count+5,state_poll_ns=50,wait_sleep_ns=3800,worker_poll_ns=10,prepare_ns=100))
        self.assertEqual(validate_submit(rows,c,2)['raw_gpu_cycles'],8700)
        for field,value in [('flags',4),('sequence',999),('generation',3),('guard_cdma_calls',348),('state_poll_ns',5000),('elapsed_cycles',0)]:
            bad=copy.deepcopy(rows);bad[0][field]=value
            with self.assertRaises(ValueError):validate_submit(bad,c,2)
        with self.assertRaises(ValueError):validate_submit(rows[:-1],c,2)
    def test_host_scope_cannot_become_target_timing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'run.json').write_text(json.dumps(dict(scope='HOST_SOFTWARE_NOT_GPU',error=0)))
            (root/'timing.json').write_text(json.dumps(dict(scope='HOST_SOFTWARE_NOT_GPU',gpu_cycles=686600903)))
            with self.assertRaises(ValueError):summarize(root,compile_catalog('G1'))
    def test_gn_internal_partitions_and_no_global_sum(self):
        c=compile_catalog('G1');e=next(e for e in c['entries'] if e['kind']=='GPU' and e['op']==4)
        words=e['words'];n=words[16]*words[18]*words[19]
        workers=1 if n<4096 else 16 if n<65536 else 64
        raw=bytearray(448)
        for i in range(workers):struct.pack_into('<I',raw,i*4,(n*(i+1)//workers)-(n*i//workers))
        struct.pack_into('<2f3I',raw,256,0.0,1.0,0,workers,n)
        struct.pack_into('<16Q',raw,320,*range(1,17))
        r=gn_internal(bytes(raw),e);self.assertIsNone(r['global_wall_cycles'])
        for offset in (0,264,268,276,316):
            bad=bytearray(raw);bad[offset]^=1
            with self.assertRaises(ValueError):gn_internal(bytes(bad),e)
    def test_untrusted_raw_run_cannot_become_board_pass(self):
        from scripts.hunet_opt.audit_run import audit_run
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);r=audit_run(root,root/'missing-release')
            self.assertEqual(r['execution'],'FAIL');self.assertFalse(r['publish_performance'])
            self.assertFalse(r['formal_eligible']);self.assertEqual(r['h1'],'NOT_RUN')
if __name__=='__main__':unittest.main()

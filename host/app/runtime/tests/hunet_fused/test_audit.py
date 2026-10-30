"""Synthetic receipt rejection and immutable historical tensors; no GPU execution."""
import importlib.util, json, os, pathlib, shutil, struct, sys, tempfile, types, unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_fused'))
import audit_run as audit

class AuditTests(unittest.TestCase):
 def test_runtime_handoff_json_matches_actual_binary_receipts(self):
  folders=sorted((ROOT/os.environ.get('FUSED_OUT','build')).glob('fused-runtime-*/diagnostic'));self.assertTrue(folders)
  p=folders[-1];data=json.loads((p/'timing.json').read_bytes());detail=data['handoff_v1']
  self.assertEqual((detail['version'],len(detail['cpu'])),(1,13));raw=(p/'cpu-records.bin').read_bytes()
  for i,(old,d) in enumerate(zip(data['cpu'],detail['cpu'])):
   self.assertEqual(d['sequence'],struct.unpack_from('<I',raw,i*712+36)[0]);self.assertEqual(old['T_compute_ns'],struct.unpack_from('<Q',raw,i*712+656)[0])
   self.assertEqual(d['T_gelu_exterior_ns'],d['T_gelu_call_ns']-old['T_compute_ns']);self.assertGreater(d['T_input_sha_ns'],0);self.assertGreater(d['T_output_sha_ns'],0)
  m=dict(T_resident_ns=180000000000,T_checkpoint_ns=sum(x['T_checkpoint_ns'] for x in detail['cpu']),T_diagnostic_capture_ns=0,T_final_read_ns=0,T_load_ns=0,timing_extension=detail)
  summary=audit.load('runtime_handoff_summary',ROOT/'scripts/hunet_fused/summary.py')
  gpu=[dict(elapsed_cycles=1,**{k:x[k] for k in ('T_submit_wait_ns','T_evidence_ns','T_reap_ns')}) for x in data['gpu']]
  self.assertGreaterEqual(summary.timing_summary(gpu,data['cpu'],m,96969696)['T_unclassified_ns'],0)
 def test_saved_board_stage0_accepts_ready_snapshot(self):
  # Regression: a real open-time binding is READY=1, unlike canonical state=0.
  parent=ROOT/'evidence/fused-task14/55a4f7869ae743fe88de70da2556804a'
  release=ROOT/'release/fused-2f15f5a-20261014T130735087651Z'
  baseline=ROOT.parent/'vortex_linux_backend_hunet_hybrid_20261013/evidence/task14-full-diagnostic/a08373c0ad524f2584283d4623293703/raw'
  result=audit.audit_run(parent/'raw',release,baseline)
  self.assertEqual(result['execution'],'PASS',result)
  self.assertEqual(result['coverage'],'PREFIX_ONLY')
  self.assertEqual(result['baseline_equal'],'PASS')
  self.assertEqual(result['r2']['status'],'NOT_EVALUATED')
  self.assertEqual(result['ort']['status'],'NOT_EVALUATED')
  self.assertFalse(result['formal_eligible'])
 def test_saved_board_binding_rejects_wrong_state_generation_or_identity(self):
  # Rehash each synthetic mutation so rejection must come from semantics,
  # not simply from the file integrity gate. Never change original evidence.
  parent=ROOT/'evidence/fused-task14/55a4f7869ae743fe88de70da2556804a'
  release=ROOT/'release/fused-2f15f5a-20261014T130735087651Z'
  baseline=ROOT.parent/'vortex_linux_backend_hunet_hybrid_20261013/evidence/task14-full-diagnostic/a08373c0ad524f2584283d4623293703/raw'
  mutations=[('state_'+str(state),28,state) for state in (0,2,3,4,5,0xffffffff)]
  mutations += [('generation',24,6),('profile',12,3),('program',16,8),('features',20,7),('identity_hash',32,0)]
  for name,offset,value in mutations:
   with self.subTest(mutation=name),tempfile.TemporaryDirectory() as d:
    temporary=pathlib.Path(d);shutil.copy2(parent/'readonly-preflight.json',temporary/'readonly-preflight.json')
    run=temporary/'raw';shutil.copytree(parent/'raw',run)
    binding=bytearray((run/'binding-fused.bin').read_bytes());struct.pack_into('<I',binding,offset,value)
    (run/'binding-fused.bin').write_bytes(binding)
    meta=json.loads((run/'run.json').read_bytes());meta['files']['binding-fused.bin']['sha256']=audit.digest(binding)
    (run/'run.json').write_text(json.dumps(meta),encoding='utf-8')
    result=audit.audit_run(run,release,baseline)
    self.assertEqual(result['execution'],'FAIL',result)
    self.assertEqual(result['reason'],'BINDING_V3',result)
    self.assertFalse(result['formal_eligible'])
 def test_exact_stage_comparison_reports_first_float_bits(self):
  a=struct.pack('<3f',1,2,3); b=struct.pack('<3f',1,2.25,3)
  self.assertEqual(audit.bitwise(a,a)['status'],'PASS')
  r=audit.bitwise(a,b);self.assertEqual(r['status'],'FAIL');self.assertEqual(r['first_difference']['index'],1)
  self.assertEqual(r['first_difference']['actual_bits'],'0x40000000')
  with self.assertRaises(ValueError):audit.bitwise(a,b'')
 def test_raw_record_padding_and_metadata_not_trusted(self):
  args=(ROOT/'generated/hunet_fused/jobs.bin').read_bytes()[:128]
  meta=audit.expected_meta(args);rid=bytes.fromhex('12'+'00'*15)
  e=bytearray(256);struct.pack_into('<4I',e,0,0x454a4c48,1,256,0);e[16:32]=rid
  struct.pack_into('<4Q',e,32,10,100,200,250)
  struct.pack_into('<4I',e,64,meta[0],meta[3],meta[2],9)
  struct.pack_into('<8I',e,80,2,0,0,0,7,8,4000000000,100)
  struct.pack_into('<4I',e,112,1,1,2,31)
  struct.pack_into('<4I',e,144,3,0,2,0);e[160:192]=bytes.fromhex(audit.digest(args))
  struct.pack_into('<2Q',e,192,60000000100,0);struct.pack_into('<I',e,208,3)
  active=min((meta[4]+3)//4,4)
  struct.pack_into('<4I',e,212,*[meta[4]//active+(i<meta[4]%active) if i<active else 0 for i in range(4)])
  raw=args+struct.pack('<7I',*meta)+bytes(4)+e+struct.pack('<4Q',200,20,10,0)
  self.assertEqual(audit.gpu_record(raw,args,rid,2)['job_id'],10)
  for offset in (128,156,160+68,160+104,160+152,160+208,160+232):
   q=bytearray(raw);q[offset]^=1
   with self.subTest(offset=offset),self.assertRaises(ValueError):audit.gpu_record(q,args,rid,2)
  q=bytearray(raw);struct.pack_into('<I',q,160+112,0)
  with self.assertRaises(ValueError):audit.gpu_record(q,args,rid,2)
 def test_real_historical_baseline_retains_failure(self):
  path=ROOT.parent/'vortex_linux_backend_hunet_hybrid_20261013/evidence/task14-full-diagnostic/a08373c0ad524f2584283d4623293703/raw'
  baseline=audit.historical_baseline(path)
  self.assertEqual(len(baseline['stages']),100);self.assertEqual(len(baseline['cpu']),13)
  result=audit.frozen_numeric(baseline['logits'])
  self.assertEqual(result['R2_numeric']['status'],'PASS')
  self.assertEqual(result['ORT_application_numeric']['mismatch_count'],8105)
  self.assertFalse(audit.formal_eligible(True,True,result,True))
 def test_no_real_run_without_readonly_gate(self):
  import tempfile
  with tempfile.TemporaryDirectory() as p:
   r=audit.audit_run(pathlib.Path(p),pathlib.Path(p),pathlib.Path(p))
   self.assertEqual(r['execution'],'FAIL');self.assertFalse(r['formal_eligible'])
 def test_actual_transport_fixture_coverage_not_model_execution(self):
  folders=sorted((ROOT/os.environ.get('FUSED_OUT','build')).glob('fused-runtime-*/diagnostic'))
  self.assertTrue(folders,'Run test-fused-runtime first');p=folders[-1]
  payload={x.relative_to(p).as_posix():x.read_bytes() for x in p.rglob('*') if x.is_file()}
  gb=payload['gpu-records.bin'];cb=payload['cpu-records.bin'];jobs=(ROOT/'generated/hunet_fused/jobs.bin').read_bytes();rid=bytes.fromhex('12'+'00'*15)
  gpu=[audit.gpu_record(gb[i*448:(i+1)*448],jobs[i*128:(i+1)*128],rid,2) for i in range(103)]
  schedule=json.loads((ROOT/'generated/hunet_fused/schedule.json').read_bytes());sequence=1;gi=ci=0
  while sequence<=1023:
   if ci<13 and sequence==schedule['cpu'][ci]['sequence']:
    audit.cpu_record(cb[ci*712:(ci+1)*712],schedule['cpu'][ci],rid,2,gpu[gi-1]['jobs_after'],schedule['logical_sha256'],payload,True);ci+=1;sequence+=1
   else:
    self.assertEqual(gpu[gi]['first'],sequence);sequence=gpu[gi]['last']+1;gi+=1
  self.assertEqual((sequence,gi,ci),(1024,103,13))
  stages=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
  for s in stages:self.assertEqual(len(payload['stages/%03u.f32'%s['stage_id']]),s['bytes'])
  for broken in [gb[:-1],gb+bytes(448)]:self.assertNotEqual(len(broken),103*448)
 def test_full_entry_and_measured_gate_synthetic_protocol_only(self):
  folders=sorted((ROOT/os.environ.get('FUSED_OUT','build')).glob('fused-runtime-*/diagnostic'));self.assertTrue(folders)
  parent=folders[-1].parent;original_load=audit.load;schedule=json.loads((ROOT/'generated/hunet_fused/schedule.json').read_bytes())
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);release=root/'release';release.mkdir();(release/'cpu').mkdir()
   jobs=(ROOT/'generated/hunet_fused/jobs.bin').read_bytes();(release/'jobs.bin').write_bytes(jobs)
   canonical=bytearray(512);struct.pack_into('<8I',canonical,0,0x44424c48,3,512,4,9,15,0,0);canonical[160:192]=bytes.fromhex(audit.digest(jobs));(release/'binding-fused.bin').write_bytes(canonical)
   identity=dict(source_digest='a'*64,binding_sha256=audit.digest(canonical),bit_sha256='b'*64,xsa_sha256='c'*64)
   (release/'release-fused.json').write_text(json.dumps(identity))
   runtime=dict(compiler='TEST_PROTOCOL_ONLY',algorithm='ERF_NATIVE_V1',threads=4,precision_flags=['-fno-fast-math','-ffp-contract=off'],libm_bytes=1,libm_sha256='e'*64)
   (release/'cpu/runtime.json').write_text(json.dumps(runtime))
   def loader(name,path):
    if name=='fused_release_auditor':return types.SimpleNamespace(verify_fused_release=lambda p:dict(integrity='PASS',scope='HUNET_FUSED_OFFLINE_RELEASE'))
    return original_load(name,path)
   for mode,folder in [('diagnostic','diagnostic'),('measured','measured-TEST-ONLY-gate')]:
    run=root/mode;shutil.copytree(parent/folder,run);raw={p.relative_to(run).as_posix():p.read_bytes() for p in run.rglob('*') if p.is_file()}
    binding=bytearray(canonical);struct.pack_into('<2I',binding,24,2,1);raw['binding-fused.bin']=binding;raw['cpu-runtime.json']=json.dumps(runtime).encode()
    end=bytearray(raw['end.bin']);end[144:176]=bytes.fromhex(identity['binding_sha256']);raw['end.bin']=end
    kind=4 if mode=='diagnostic' else 1;b=bytearray(raw['begin.bin']);struct.pack_into('<I',b,36,kind);raw['begin.bin']=b
    # The transport fixture has no physical cycle counter. Supply explicit
    # synthetic positive counts only for schema/gate testing, never evidence.
    gb=bytearray(raw['gpu-records.bin'])
    for i in range(103):struct.pack_into('<I',gb,i*448+160+108,100)
    raw['gpu-records.bin']=gb;cb=raw['cpu-records.bin'];rid=bytes.fromhex('12'+'00'*15)
    gpu=[audit.gpu_record(gb[i*448:(i+1)*448],jobs[i*128:(i+1)*128],rid,2) for i in range(103)]
    cpu=[]
    for i,s in enumerate(schedule['cpu']):
     previous=next(g for g in reversed(gpu) if g['last']<s['sequence'])
     cpu.append(audit.cpu_record(cb[i*712:(i+1)*712],s,rid,2,previous['jobs_after'],schedule['logical_sha256'],raw,mode=='diagnostic'))
    timing=dict(gpu=[{k:g[v] for k,v in dict(first='first',last='last',stage='stage',op='op',elapsed_raw='elapsed_cycles',T_submit_wait_ns='T_submit_wait_ns',T_evidence_ns='T_evidence_ns',T_reap_ns='T_reap_ns').items()} for g in gpu],cpu=[{k:c[k] for k in ('sequence','stage','T_begin_call_ns','T_compute_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns')} for c in cpu])
    raw['timing.json']=json.dumps(timing).encode()
    base=dict(stages={i:raw['stages/%03u.f32'%i] for i in range(100)} if mode=='diagnostic' else {},cpu={s['stage']:{k:raw['cpu/%03u-%s.f32'%(s['stage'],k)] for k in ('input','output','writeback')} for s in schedule['cpu']} if mode=='diagnostic' else {},logits=raw['logits.f32'],manifest_sha256='TEST_ONLY')
    for n,b in raw.items():(run/n).write_bytes(b)
    meta=dict(scope='HUNET_FUSED_LINUX_V1',profile=4,program_id=9,error=0,boot_id='TEST_BOOT',build_id='TEST_BUILD',kernel='TEST_KERNEL',generation=2,run_id=rid.hex(),roi_index=0,sample_kind=kind,sample_index=0,mode=mode,state='RAW_COMPLETE_REQUIRES_AUDIT',gpu_record_bytes=448,cpu_record_bytes=712,gpu_records=103,cpu_records=13,logical_completed=1023,stage_count=100,captured_stages=100 if mode=='diagnostic' else 0,T_load_ns=0,T_resident_ns=180000000000,T_checkpoint_ns=0,T_diagnostic_capture_ns=0,T_final_read_ns=0,files={n:dict(bytes=len(b),sha256=audit.digest(b)) for n,b in raw.items()})
    (run/'run.json').write_text(json.dumps(meta))
    approved=dict(formal_eligible=True,execution='PASS',coverage='PASS',mode='diagnostic',baseline_equal='PASS',r2=dict(status='PASS'),ort=dict(status='PASS'),boot_id='TEST_BOOT',binding_sha256=identity['binding_sha256'])
    gate=dict(integrity='PASS',scope='HW4_FUSED_READONLY_PREFLIGHT',boot_id='TEST_BOOT',build_id='TEST_BUILD',kernel='TEST_KERNEL',expected_run_id=rid.hex(),minimum_generation=2,formal_authorized=True,diagnostic_audit=approved,**identity)
    gate_path=root/'readonly-preflight.json';gate_path.write_text(json.dumps(gate))
    numeric={'R2_numeric':dict(status='PASS'),'ORT_application_numeric':dict(status='PASS')}
    with patch.object(audit,'load',side_effect=loader),patch.object(audit,'historical_baseline',return_value=base),patch.object(audit,'frozen_numeric',return_value=numeric):
     result=audit.audit_run(run,release,root/'TEST_BASELINE');self.assertEqual(result['execution'],'PASS',result);self.assertTrue(result['formal_eligible'],result)
     if mode=='measured':
      self.assertEqual(result['baseline_equal'],'NOT_CAPTURED');gate['diagnostic_audit']['baseline_equal']='FAIL';gate_path.write_text(json.dumps(gate))
      self.assertEqual(audit.audit_run(run,release,root/'TEST_BASELINE')['execution'],'FAIL')
     else:
      n='stages/017.f32';(run/n).unlink()
      self.assertEqual(audit.audit_run(run,release,root/'TEST_BASELINE')['execution'],'FAIL')

if __name__=='__main__':unittest.main()

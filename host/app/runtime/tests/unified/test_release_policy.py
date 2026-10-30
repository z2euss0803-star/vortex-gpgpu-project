"""Synthetic release documents test claim boundaries, never hardware success."""
import unittest,pathlib,json,copy
from scripts.unified.release import release_errors
ROOT=pathlib.Path(__file__).resolve().parents[2]

def fixture(profile='LEGACY_DUAL_PROVIDER'):
 declared=json.loads((ROOT/'contracts/unified/providers.json').read_text())['providers']
 providers=[]
 for declaration in declared:
  if declaration['provider_flags'] not in ({4} if profile=='S8_JOINT_ONLY' else {1,2}):continue
  providers.append(dict(declaration=declaration,offline='PASS_HOST_ONLY',cross='PASS_NOT_EXECUTED',kernel='PASS_CONTENT_ONLY',image='PASS_CONTENT_ONLY',board='NOT_RUN',display='NOT_RUN',application_precision='NOT_RUN',performance='NOT_MEASURED',gpu_raw_cycles=None,build_id='EXPLICIT_UNIT_FIXTURE_'+str(declaration['provider_flags']),files={n:dict(bytes=10,sha256='a'*64) for n in ('BOOT.BIN','boot.scr','image.ub')},extension_same_driver='HOST_AND_BUILTIN_HASH_ONLY_NOT_TARGET_EXECUTION'))
 if profile=='S8_JOINT_ONLY':providers[0]['extension_same_driver']='JOINT_NN_PACKAGES_HOST_ADMISSION_ONLY_NOT_TARGET_EXTENSION'
 else:providers[1]['extension_same_driver']='NOT_APPLICABLE_GRAPHICS_ONLY_PROVIDER'
 return dict(schema=1,scope='UNIFIED_RELEASE_EVIDENCE_NOT_BOARD_ACCEPTANCE',delivery_profile=profile,providers=providers,abi=dict(legacy_bytes=176,v2_bytes=128),scheduler=dict(device_owners=1,active_gpu_jobs=1,cpu_threads=4,preempt=False),history_failures=[dict(id=k,state='PRESERVED_OPEN',evidence=['EXPLICIT_UNIT_FIXTURE']) for k in ('MOSAIC_WATCHDOG','HUNET_EBUSY','ORT_R2','S8_PHYSICAL_SIGNOFF')],review='PENDING',review_evidence=None,field_commands_executed=False,sd='NOT_TOUCHED',gpu_raw_cycles=None,ready_for_sd=False)

class ReleasePolicy(unittest.TestCase):
 def test_s8_joint_only_delivery_keeps_exact_new_admission_without_a_second_image(self):self.assertEqual(release_errors(fixture('S8_JOINT_ONLY')),[])
 def test_legacy_provider_cannot_masquerade_as_joint_s8(self):
  doc=fixture('S8_JOINT_ONLY');doc['providers']=fixture()['providers'];self.assertTrue(release_errors(doc))
 def test_joint_still_needs_actual_offline_gates_and_review_before_sd(self):
  doc=fixture('S8_JOINT_ONLY');doc['ready_for_sd']=True;self.assertTrue(release_errors(doc))
 def test_offline_only_evidence_is_valid_but_not_board_acceptance(self):self.assertEqual(release_errors(fixture()),[])
 def test_any_offline_layer_cannot_claim_board_display_precision_or_performance(self):
  for name,value in (('board','PASS'),('display','PASS'),('application_precision','PASS'),('performance','PASS'),('gpu_raw_cycles',0)):
   with self.subTest(name=name):
    doc=fixture();doc['providers'][0][name]=value;self.assertTrue(release_errors(doc))
 def test_provider_isolation_and_full_hardware_identity_are_mandatory(self):
  for field,value in (('provider_flags',True),('namespaces',[1,2,3]),('preempt',True),('contexts',2),('compatible','vortex,old'),('board_identity_verified',True),('bit_sha256','abcd')):
   with self.subTest(field=field):
    doc=fixture();doc['providers'][0]['declaration'][field]=value;self.assertTrue(release_errors(doc))
  doc=fixture();doc['providers'][1]['declaration']=copy.deepcopy(doc['providers'][0]['declaration']);self.assertTrue(release_errors(doc))
 def test_one_incomplete_provider_prevents_sd_readiness(self):
  doc=fixture();doc.update(review='PASS_FRESH_WHOLE_BRANCH',ready_for_sd=True);doc['providers'][1]['image']='PENDING';self.assertTrue(release_errors(doc))
 def test_missing_review_or_matching_boot_component_cannot_write_sd(self):
  doc=fixture();doc['ready_for_sd']=True;self.assertTrue(release_errors(doc))
  doc=fixture();doc.update(review='PASS_FRESH_WHOLE_BRANCH',ready_for_sd=True);del doc['providers'][0]['files']['BOOT.BIN'];self.assertTrue(release_errors(doc))
 def test_history_failures_and_original_abi_remain_visible(self):
  for key,value in (('history_failures',[]),('abi',dict(legacy_bytes=128,v2_bytes=128)),('scheduler',dict(device_owners=1,active_gpu_jobs=2,cpu_threads=4,preempt=False))):
   with self.subTest(key=key):
    doc=fixture();doc[key]=value;self.assertTrue(release_errors(doc))
 def test_field_commands_and_cycles_do_not_run_during_release_generation(self):
  for key,value in (('field_commands_executed',True),('sd','WRITTEN'),('gpu_raw_cycles',0)):
   with self.subTest(key=key):
    doc=fixture();doc[key]=value;self.assertTrue(release_errors(doc))
 def test_complete_offline_gate_can_only_authorize_next_sd_step(self):
  doc=fixture();doc.update(review='PASS_FRESH_WHOLE_BRANCH',ready_for_sd=True,review_evidence=dict(scope='FRESH_WHOLE_BRANCH_REVIEW',source_snapshot_id='b'*64,report_sha256='c'*64,critical=0,important=0));self.assertEqual(release_errors(doc),[])
  self.assertTrue(all(p['board']=='NOT_RUN' for p in doc['providers']))
 def test_review_label_without_full_report_and_source_identity_is_not_approval(self):
  doc=fixture();doc.update(review='PASS_FRESH_WHOLE_BRANCH',ready_for_sd=True)
  self.assertTrue(release_errors(doc))
 def test_neural_extension_evidence_does_not_grant_graphics_provider_nn_capability(self):
  doc=fixture();doc['providers'][1]['extension_same_driver']='NOT_APPLICABLE_GRAPHICS_ONLY_PROVIDER'
  self.assertEqual(release_errors(doc),[])
  doc['providers'][1]['extension_same_driver']='HOST_AND_BUILTIN_HASH_ONLY_NOT_TARGET_EXECUTION'
  self.assertTrue(release_errors(doc))
  doc['providers'][1]['extension_same_driver']='NOT_APPLICABLE_GRAPHICS_ONLY_PROVIDER'
  doc['providers'][0]['extension_same_driver']='NOT_APPLICABLE_GRAPHICS_ONLY_PROVIDER'
  self.assertTrue(release_errors(doc))

class ReleaseInputBinding(unittest.TestCase):
 def fixture(self,raw=b'{"natural_exit":0}'):
  import tempfile,hashlib
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);path=pathlib.Path(temp.name)/'receipt.json';path.write_bytes(raw)
  return path,dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
 def test_exact_pinned_bytes_load_without_implying_board_success(self):
  from scripts.unified.release import load_pinned
  path,row=self.fixture();self.assertEqual(load_pinned(row),dict(natural_exit=0))
 def test_changed_bytes_digest_size_or_duplicate_keys_are_rejected(self):
  from scripts.unified.release import load_pinned
  path,row=self.fixture()
  for bad in (dict(row,sha256='0'*64),dict(row,bytes=row['bytes']+1),dict(row,bytes=True)):
   with self.subTest(bad=bad),self.assertRaises(ValueError):load_pinned(bad)
  path.write_bytes(b'{"natural_exit":1}')
  with self.assertRaises(ValueError):load_pinned(row)
  path,row=self.fixture(b'{"natural_exit":1,"natural_exit":0}')
  with self.assertRaises(ValueError):load_pinned(row)
 def test_exact_empty_command_log_is_allowed_only_for_explicit_log_admission(self):
  from scripts.unified.release import verify_blob,load_pinned
  path,row=self.fixture(b'')
  with self.assertRaises(ValueError):verify_blob(row,1024)
  with self.assertRaises(ValueError):load_pinned(row)
  verify_blob(row,1024,allow_empty=True)
  with self.assertRaises(ValueError):verify_blob(dict(row,sha256='0'*64),1024,allow_empty=True)
  with self.assertRaises(ValueError):verify_blob(row,1024,allow_empty=1)
  path.write_bytes(b'x')
  with self.assertRaises(ValueError):verify_blob(row,1024,allow_empty=True)

class ProducerInstallInputs(unittest.TestCase):
 def fixture(self):
  import tempfile
  from scripts.unified.cross_common import snapshot
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);base=pathlib.Path(temp.name);old=base/'producer';new=base/'release'
  for root in (old,new):
   for name,raw in {'driver/native.c':'int native;\n','data/model.bin':'original model','petalinux/unified/runtime.bb':'original dependency','scripts/unified/image.py':'def boot_bif(): return "old metadata"\ndef prepare_payload(): return "original installation"\n','scripts/unified/audit_image.py':'def payload_errors(): return []\n'}.items():
    p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(raw)
  s=snapshot(old);return old,new,dict(source_before=s,source_after=s)
 def test_release_docs_and_independent_bif_metadata_do_not_relabel_a_rebuild(self):
  from scripts.unified.release import admit_image_producer
  from scripts.unified.cross_common import snapshot
  old,new,built=self.fixture();p=new/'docs/unified/RELEASE.md';p.parent.mkdir(parents=True);p.write_text('new host-only release notes')
  (new/'scripts/unified/image.py').write_text('def boot_bif(): return "correct metadata"\ndef prepare_payload(): return "original installation"\n')
  self.assertEqual(admit_image_producer(new,snapshot(new),built,old)['image_producer_source_snapshot_id'],built['source_before']['source_snapshot_id'])
 def test_new_model_recipe_native_runtime_or_installation_logic_requires_matching_build(self):
  from scripts.unified.release import admit_image_producer
  from scripts.unified.cross_common import snapshot
  for name,raw in (('data/model.bin','changed model'),('petalinux/unified/runtime.bb','changed dependency'),('driver/native.c','int changed;\n'),('scripts/unified/image.py','def boot_bif(): return "old metadata"\ndef prepare_payload(): return "different installation"\n'),('scripts/unified/audit_image.py','def payload_errors(): return ["changed policy"]\n')):
   with self.subTest(name=name):
    old,new,built=self.fixture();(new/name).write_text(raw)
    with self.assertRaises(ValueError):admit_image_producer(new,snapshot(new),built,old)

class LegacyErrorLedger(unittest.TestCase):
 def test_legacy_rows_without_id_and_repeated_observations_remain_unchanged(self):
  from scripts.unified.release import error_ledger_rows
  ledger=[dict(id='named',run='first'),dict(run='legacy-without-id',natural_exit=1),dict(id='named',run='later-observation')];original=copy.deepcopy(ledger)
  rows=error_ledger_rows(ledger);self.assertEqual(len(rows),3);self.assertEqual(ledger,original)
  self.assertEqual([r['ledger_row'] for r in rows],[0,1,2]);self.assertEqual([r['original_id'] for r in rows],['named',None,'named'])
  self.assertEqual(len({r['row_identity'] for r in rows}),3)
 def test_empty_or_non_object_ledger_is_refused(self):
  from scripts.unified.release import error_ledger_rows
  for ledger in (None,[],[None]):
   with self.subTest(ledger=ledger),self.assertRaises(ValueError):error_ledger_rows(ledger)

class IncompleteReleaseCannotPass(unittest.TestCase):
 def test_host_fixture_or_bad_source_lock_never_produces_acceptance(self):
  import tempfile,hashlib
  from scripts.unified.cross_common import snapshot
  from scripts.unified.release import assemble_release
  base=ROOT/'build';base.mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='U25-release-negative-',dir=str(base)) as name:
   folder=pathlib.Path(name).resolve();self.assertIn(base.resolve(),folder.parents)
   def put(name,value):
    path=folder/name;raw=(json.dumps(value,sort_keys=True)+'\n').encode();path.write_bytes(raw)
    return dict(path=str(path),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
   lock=put('source-lock.json',dict(schema=1,snapshot=snapshot(ROOT)))
   software=put('host-fixture.json',dict(scope='EXPLICIT_HOST_FIXTURE_NOT_A_REAL_GATE',natural_exit=0,gpu_raw_cycles=None))
   for label,pin in (('wrong-source',dict(lock,sha256='0'*64)),('host-fixture',lock)):
    index=folder/(label+'.json');index.write_text(json.dumps(dict(source_lock=pin,software=software)))
    target=folder/label
    with self.subTest(label=label),self.assertRaises(ValueError):assemble_release(ROOT,index,target)
    result=json.loads((target/'receipt.json').read_text());self.assertEqual(result['natural_exit'],1);self.assertIsNone(result['gpu_raw_cycles']);self.assertEqual(result['sd'],'NOT_TOUCHED');self.assertFalse((target/'ACCEPTANCE.json').exists())

if __name__=='__main__':unittest.main()

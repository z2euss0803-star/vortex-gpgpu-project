import importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class SessionMenu(unittest.TestCase):
 def module(self):
  s=importlib.util.spec_from_file_location('page_menu_new',ROOT/'tools/demo.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);self.assertTrue(hasattr(m,'run_hunet_page_session'));return m
 def run_case(self,lost=False,wrong=False,failed=False,unknown=False,display_failed=False,reap_lost=False,save_failed=False,root=None,before_submit=None):
  m=self.module();page=dict(id='page1',title='page',items=[dict(input_id='example-p1_002',roi_id='p1_002',source_format='PNG',source_sha256='1'*64,tensor_sha256='2'*64),dict(input_id='example-p1_001',roi_id='p1_001',source_format='PNG',source_sha256='3'*64,tensor_sha256='4'*64)]);model=dict(entry_id='hunet',model='5'*64,plan='6'*64)
  plan,input_sha=m.hunet_page_session_identity(model['plan'],page)
  class Peer:
   boot='boot';client_id='serial'
   def __init__(self):self.calls=[];self.count=0;self.submits=0
   def call(self,command,**kw):
    self.calls.append((command,kw))
    if command=='inputs':return dict(inputs=[dict(input_id='reviewed-page1',sha256=input_sha,bytes=393216,plan=plan)])
    if command in ('mode','mode-status'):return dict(current='nn',switching=False)
    if command=='submit':
     if before_submit:before_submit()
     self.submits+=1
     if lost:raise OSError('response lost after admission')
     return dict(status='QUEUED')
    if command=='status':
     self.count+=1
     if failed:return dict(status='FAILED',application_progress=dict(completed_words=1,total_words=2))
     return dict(status='RUNNING' if self.count==1 or unknown else 'COMPLETED',device_pending=False,retirement_unknown=False,application_progress=dict(completed_words=1 if self.count==1 or unknown else 2,total_words=2))
    if command=='result':
     rows=[dict(index=i,input_id=x['input_id'],topk_class_ids=[7,8,9,10,11],topk_raw_logits=[5.,4.,3.,2.,1.],logits_sha256='7'*64) for i,x in enumerate(page['items'])]
     if wrong:rows.reverse()
     return dict(execution_complete=True,model=model['model'],plan=plan,numeric_class='NOT_EVALUATED',artifacts=[dict(layout='HUNET_PAGE_F32',shape=[2,8105,1],bytes=64840,sha256='8'*64,path='outputs/page-logits.f32')],raw_legacy_json=json.dumps(dict(format='native-hunet-page-result-v1',page_id='page1',total=2,completed=2,order='REVIEWED_MANIFEST_ORDER',score_kind='RAW_LOGIT_NOT_PROBABILITY',words=rows)))
    if command=='display':
     if display_failed:raise OSError('display restoration failed; preserve old front')
     return dict(displayed=True)
    if command=='reap':
     summaries=list(pathlib.Path(tmp).glob('multichar/session-*/summary.json'))
     if len(summaries)!=1:raise AssertionError('one actual durable summary before reap')
     durable=json.loads(summaries[0].read_bytes())
     if durable['status']!='COMPLETED' or len(durable['characters'])!=2:raise AssertionError('complete validated summary must be durable before reap')
     if reap_lost:raise OSError('reap response lost')
     return dict(client_reaped=True)
    raise AssertionError(command)
  peer=Peer();self.last_peer=peer
  original_save=m.save
  def failing_save(path,value):
   if save_failed and pathlib.Path(path).name=='summary.json' and value.get('status')=='COMPLETED':raise OSError('summary storage full')
   return original_save(path,value)
  m.save=failing_save
  import contextlib
  with (tempfile.TemporaryDirectory() if root is None else contextlib.nullcontext(root)) as tmp:
   result=m.run_hunet_page_session(peer,model,page,tmp,{i:chr(0x4e00+i) for i in range(7,12)},timeout=.01 if unknown else 1,interval=.001);saved=json.loads(pathlib.Path(result['summary_path']).read_bytes());self.assertEqual(saved['status'],result['status'])
  return result,peer
 def test_one_submit_original_order_one_final_display_and_word_progress(self):
  s,p=self.run_case();self.assertEqual(p.submits,1);self.assertEqual(s['status'],'COMPLETED');self.assertEqual([x['roi_id'] for x in s['characters']],['p1_002','p1_001']);self.assertEqual(len([x for x in p.calls if x[0]=='display']),1);self.assertEqual(s['progress']['completed_words'],2);self.assertEqual(s['score_kind'],'RAW_LOGIT_NOT_PROBABILITY')
 def test_lost_submit_is_not_replayed(self):
  s,p=self.run_case(lost=True);self.assertEqual(p.submits,1);self.assertEqual(s['status'],'UNKNOWN');self.assertFalse(any(x[0]=='display' for x in p.calls))
 def test_failed_page_keeps_prefix_count_without_display(self):
  s,p=self.run_case(failed=True);self.assertEqual(s['status'],'FAILED');self.assertEqual(s['progress']['completed_words'],1);self.assertFalse(any(x[0]=='display' for x in p.calls))
 def test_word_mapping_error_is_rejected_before_display(self):
  s,p=self.run_case(wrong=True);self.assertEqual(s['status'],'STOPPED');self.assertFalse(any(x[0]=='display' for x in p.calls))
 def test_timeout_is_unknown_without_cancel_or_resubmit(self):
  s,p=self.run_case(unknown=True);self.assertEqual(s['status'],'UNKNOWN');self.assertEqual(p.submits,1);self.assertFalse(any(x[0] in ('display','cancel','recover') for x in p.calls))
 def test_completed_page_releases_one_client_hold_after_durable_summary(self):
  s,p=self.run_case();self.assertEqual([x[0] for x in p.calls].count('reap'),1);self.assertTrue(s['client_reap']['client_reaped']);self.assertLess([x[0] for x in p.calls].index('display'),[x[0] for x in p.calls].index('reap'))
 def test_display_failure_still_reaps_completed_durable_page(self):
  s,p=self.run_case(display_failed=True);self.assertEqual(s['status'],'COMPLETED');self.assertIn('display_error',s);self.assertEqual([x[0] for x in p.calls].count('reap'),1)
 def test_lost_reap_response_preserves_complete_result_and_never_retries(self):
  s,p=self.run_case(reap_lost=True);self.assertEqual(s['status'],'COMPLETED');self.assertIn('client_reap_error',s);self.assertEqual([x[0] for x in p.calls].count('reap'),1);self.assertEqual(p.submits,1)
 def test_durable_summary_write_failure_keeps_client_hold(self):
  with self.assertRaisesRegex(OSError,'summary storage full'):self.run_case(save_failed=True)
  self.assertFalse(any(x[0]=='reap' for x in self.last_peer.calls))
 def test_unknown_bad_mapping_and_lost_submit_do_not_reap(self):
  for options in (dict(unknown=True),dict(wrong=True),dict(lost=True)):
   with self.subTest(options=options):
    s,p=self.run_case(**options);self.assertFalse(any(x[0]=='reap' for x in p.calls))
 def test_result_record_rename_fsyncs_parent_directory(self):
  import os,stat
  if not hasattr(os,'O_DIRECTORY'):self.skipTest('target Linux directory fsync')
  m=self.module();original=os.fsync;directories=[]
  def observe(fd):
   if stat.S_ISDIR(os.fstat(fd).st_mode):directories.append(fd)
   return original(fd)
  try:
   os.fsync=observe
   with tempfile.TemporaryDirectory() as tmp:
    m.save(pathlib.Path(tmp)/'summary.json',dict(status='COMPLETED'))
    self.assertEqual(json.loads((pathlib.Path(tmp)/'summary.json').read_bytes())['status'],'COMPLETED')
   self.assertTrue(directories,'a flushed file alone does not commit the renamed directory entry')
  finally:os.fsync=original
 def test_new_page_directory_ancestry_is_committed(self):
  import os,stat
  if not hasattr(os,'O_DIRECTORY'):self.skipTest('target Linux directory fsync')
  original=os.fsync;synced=[]
  def observe(fd):
   if stat.S_ISDIR(os.fstat(fd).st_mode):synced.append(os.readlink('/proc/self/fd/'+str(fd)))
   return original(fd)
  try:os.fsync=observe;s,p=self.run_case()
  finally:os.fsync=original
  session=pathlib.Path(s['summary_path']).parent
  self.assertIn(str(session.parent),synced,'new session directory entry must be durable in multichar')
  self.assertIn(str(session.parent.parent),synced,'new multichar entry must be durable in state directory')
 def test_ancestor_directory_fsync_failure_never_submits_or_reaps(self):
  import os,stat
  if not hasattr(os,'O_DIRECTORY'):self.skipTest('target Linux directory fsync')
  original=os.fsync
  def fail(fd):
   if stat.S_ISDIR(os.fstat(fd).st_mode) and os.readlink('/proc/self/fd/'+str(fd)).endswith('/multichar'):raise OSError('ancestor directory sync failed')
   return original(fd)
  try:
   os.fsync=fail
   with self.assertRaisesRegex(OSError,'ancestor directory sync failed'):self.run_case()
  finally:os.fsync=original
  self.assertFalse(any(x[0] in ('submit','reap') for x in self.last_peer.calls))
 def test_later_independent_attempt_recommits_previously_failed_ancestor(self):
  import os,stat
  if not hasattr(os,'O_DIRECTORY'):self.skipTest('Linux directory durability')
  original=os.fsync
  with tempfile.TemporaryDirectory() as tmp:
   def fail(fd):
    if stat.S_ISDIR(os.fstat(fd).st_mode) and os.readlink('/proc/self/fd/'+str(fd))==tmp:raise OSError('state directory sync failed once')
    return original(fd)
   try:
    os.fsync=fail
    with self.assertRaisesRegex(OSError,'state directory sync failed once'):self.run_case(root=tmp)
   finally:os.fsync=original
   self.assertTrue((pathlib.Path(tmp)/'multichar').is_dir(),'failed fsync leaves the new directory present')
   self.assertFalse(any(x[0] in ('submit','reap') for x in self.last_peer.calls))
   successful=[]
   def observe(fd):
    result=original(fd)
    if stat.S_ISDIR(os.fstat(fd).st_mode):successful.append(os.readlink('/proc/self/fd/'+str(fd)))
    return result
   def before_submit():self.assertIn(tmp,successful,'existing directory from a failed attempt must be committed before a NEW submit')
   try:os.fsync=observe;s,p=self.run_case(root=tmp,before_submit=before_submit)
   finally:os.fsync=original
   self.assertEqual(s['status'],'COMPLETED');self.assertEqual(p.submits,1);self.assertEqual([x[0] for x in p.calls].count('reap'),1)
if __name__=='__main__':unittest.main()

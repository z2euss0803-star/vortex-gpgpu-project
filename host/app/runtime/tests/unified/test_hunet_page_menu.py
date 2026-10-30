import importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ReviewedPageMenu(unittest.TestCase):
 def module(self):
  spec=importlib.util.spec_from_file_location('hunet_menu_tested',str(ROOT/'tools/demo.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);self.assertTrue(hasattr(m,'run_hunet_page'),'reviewed full-page menu runner required');return m
 def fixture(self,wrong=False,lost=False,failed=False):
  page=dict(id='page1',title='reviewed',items=[dict(input_id='example-p1_002',roi_id='p1_002',tensor_sha256='a'*64,tensor_bytes=196608),dict(input_id='example-p1_010',roi_id='p1_010',tensor_sha256='b'*64,tensor_bytes=196608)])
  class Peer:
   boot='boot';client_id='serial-demo'
   def __init__(self):self.calls=[];self.requests={};self.submits=0;self.available=[dict(input_id=x['input_id'],sha256=x['tensor_sha256'],bytes=196608,plan=str(i+1)*64) for i,x in enumerate(page['items'])]
   def call(self,command,**v):
    self.calls.append((command,v))
    if command=='inputs':return dict(inputs=self.available)
    if command in ('mode','mode-status'):return dict(current='nn',switching=False)
    if command=='submit':
     self.submits+=1;self.requests[v['request_id']]=v['input_id']
     if lost:raise OSError('lost response after possible admission')
     return dict(status='QUEUED')
    if command=='status':return dict(status='FAILED' if failed else 'COMPLETED',device_pending=False,retirement_unknown=False)
    if command=='result':
     index=next(i for i,x in enumerate(page['items']) if x['input_id']==self.requests[v['request_id']]);return dict(execution_complete=True,model='c'*64,plan='f'*64 if wrong else str(index+1)*64,numeric_class='NOT_EVALUATED',artifacts=[dict(bytes=32420,layout='CHW_F32',shape=[8105,1,1],sha256='d'*64)],raw_legacy_json=json.dumps(dict(topk_class_ids=[379],topk_raw_logits=[2.25])))
    if command=='display':return dict(presented=True,old_retired=True)
    if command=='reap':return dict(reaped=True)
    raise AssertionError(command)
  return page,Peer()
 def run_page(self,**options):
  m=self.module();page,peer=self.fixture(**options);base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   summary=m.run_hunet_page(peer,dict(entry_id='hunet',model='c'*64),page,pathlib.Path(temp),{379:'臣'},timeout=1,interval=.001)
   saved=json.loads(pathlib.Path(summary['summary_path']).read_text(encoding='utf8'));self.assertEqual(saved['status'],summary['status']);return summary,peer
 def test_box_order_prediction_summary_and_independent_numeric_status(self):
  s,p=self.run_page();self.assertEqual(s['status'],'COMPLETED');self.assertEqual(s['predicted_text'],'臣臣');self.assertEqual([x['roi_id'] for x in s['characters']],['p1_002','p1_010']);self.assertEqual(s['numeric_class'],'NOT_EVALUATED');self.assertEqual(p.submits,2);self.assertEqual(len({x['request_id'] for x in s['characters']}),2)
 def test_changed_result_plan_stops_before_next_character(self):
  s,p=self.run_page(wrong=True);self.assertEqual(s['status'],'STOPPED');self.assertEqual(p.submits,1);self.assertFalse(s['characters'][0].get('predicted_text'))
 def test_lost_submit_response_preserves_original_child_without_replay(self):
  s,p=self.run_page(lost=True);self.assertEqual(s['status'],'UNKNOWN');self.assertEqual(p.submits,1);self.assertTrue(s['characters'][0]['request_id']);self.assertEqual(s['characters'][0]['status'],'UNKNOWN')
 def test_failed_child_stops_the_remaining_page(self):
  s,p=self.run_page(failed=True);self.assertEqual(s['status'],'STOPPED');self.assertEqual(p.submits,1)
 def test_missing_fixed_input_does_not_submit(self):
  m=self.module();page,peer=self.fixture();page['items'][0]['tensor_sha256']='e'*64;base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   with self.assertRaises(ValueError):m.run_hunet_page(peer,dict(entry_id='hunet',model='c'*64),page,pathlib.Path(temp),{379:'臣'},timeout=1,interval=.001)
   self.assertEqual(peer.submits,0)
if __name__=='__main__':unittest.main()

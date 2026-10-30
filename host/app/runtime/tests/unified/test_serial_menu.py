import importlib.util,json,pathlib,tempfile,unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
class SerialMenu(unittest.TestCase):
 def module(self):
  path=ROOT/'tools/demo.py';self.assertTrue(path.is_file(),'serial three-application client required')
  spec=importlib.util.spec_from_file_location('serial_demo_tested',str(path));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
 def test_peer_failure_never_repeats_submit_and_keeps_original_identity(self):
  module=self.module();base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  class Peer:
   boot='boot';client_id='serial'
   def __init__(self):self.calls=[]
   def call(self,command,**values):
    self.calls.append((command,values))
    if command=='submit':raise OSError('lost reply after server may have accepted')
    return {'current':'nn','switching':False}
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   peer=Peer()
   with self.assertRaises(OSError):module.run_request(peer,'hunet','image-a',pathlib.Path(temp),1,.001)
   self.assertEqual(sum(c=='submit' for c,v in peer.calls),1)
   files=list(pathlib.Path(temp).glob('*.json'));self.assertEqual(len(files),1)
   record=json.loads(files[0].read_text());self.assertEqual(record['status'],'UNKNOWN');self.assertTrue(record['request_id']);self.assertEqual(record['input_id'],'image-a')
 def test_success_polls_same_request_then_preserves_completion_and_display_separately(self):
  module=self.module();base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  class Peer:
   boot='boot';client_id='serial'
   def __init__(self):self.calls=[];self.polls=0
   def call(self,command,**values):
    self.calls.append((command,values))
    if command in ('mode','mode-status'):return {'current':'nn','switching':False}
    if command=='submit':return {'status':'QUEUED'}
    if command=='status':self.polls+=1;return {'status':'RUNNING' if self.polls==1 else 'COMPLETED'}
    if command=='result':return {'execution_complete':True,'numeric_class':'NOT_EVALUATED','gpu_raw_cycles':None}
    if command=='display':raise RuntimeError('no display receipt')
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   peer=Peer();record=module.run_request(peer,'mosaic','image-b',pathlib.Path(temp),1,.001)
   self.assertEqual(sum(c=='submit' for c,v in peer.calls),1)
   request_ids={v['request_id'] for c,v in peer.calls if c in ('submit','status','result','display')}
   self.assertEqual(request_ids,{record['request_id']});self.assertEqual(record['status'],'COMPLETED');self.assertIn('display_error',record);self.assertIsNone(record['result']['gpu_raw_cycles'])
 def test_texture_command_waits_for_actual_applied(self):
  module=self.module();base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  class Peer:
   boot='boot';client_id='serial'
   def __init__(self):self.polls=0
   def call(self,command,**values):
    if command in ('mode','mode-status'):return {'current':'p8','switching':False}
    if command in ('submit','status'):return {'status':'COMPLETED'}
    if command=='result':return {'execution_complete':True,'raw_legacy_json':'{"phase":"FILE_ACCEPTED","applied":false}'}
    if command=='export':self.polls+=1;return {'phase':'QUEUED' if self.polls==1 else 'APPLIED','applied':self.polls>1,'gpu_raw_cycles':None}
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   peer=Peer();record=module.run_request(peer,'p8-texture','style',pathlib.Path(temp),1,.001)
   self.assertEqual(peer.polls,2);self.assertEqual(record['status'],'APPLIED');self.assertTrue(record['texture']['applied'])
 def test_control_rejection_and_scene_status_remain_visible(self):
  module=self.module();base=ROOT/'build/menu-tests';base.mkdir(parents=True,exist_ok=True)
  class Peer:
   boot='boot';client_id='serial'
   def call(self,command,**values):
    if command in ('mode','mode-status'):return {'current':'p8','switching':False}
    if command in ('submit','status'):return {'status':'COMPLETED'}
    if command=='result':return {'execution_complete':True,'raw_legacy_json':'{"format":"native-p8-control-v1","applied":false,"command_error":95,"object":2,"pose":25,"paused":true}'}
  with tempfile.TemporaryDirectory(dir=str(base)) as temp:
   record=module.run_request(Peer(),'p8-control','l',pathlib.Path(temp),1,.001)
   self.assertEqual(record['status'],'COMMAND_REJECTED');self.assertEqual(record['scene']['command_error'],95);self.assertEqual(record['scene']['object'],2)
 def test_default_scene_inventory_does_not_query_missing_texture_entry(self):
  module=self.module()
  class Peer:
   def call(self,command,**values):
    if command=='scenes':return {'entries':[{'entry_id':'p8-control'}]}
    raise AssertionError('nonexistent p8-texture must not be queried')
  self.assertEqual(module.registered_textures(Peer()),[])

if __name__=='__main__':unittest.main()

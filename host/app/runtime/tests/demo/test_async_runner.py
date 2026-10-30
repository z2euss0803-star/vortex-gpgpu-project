import json,os,socket,time,unittest
from pathlib import Path
from .test_local_runner import LocalRunner
class AsyncRunner(unittest.TestCase):
    runtime_root='TEST_ONLY_ASYNC_CALLBACKS'
    setUp=LocalRunner.setUp
    stop=LocalRunner.stop
    call=LocalRunner.call
    def request(self,run_id):
        selected=self.call('select-app',{'app':'mosaic'})
        self.assertEqual(selected.returncode,0,selected.stdout+selected.stderr)
        r=json.loads(Path('tests/demo/fixtures/run-v1.json').read_text())
        r.update(run_id=run_id,generation=json.loads(selected.stdout)['generation'],state='STAGED',scope='OFFLINE',boot_id='OFFLINE',build_id='SOFTWARE_CALLBACKS_NOT_GPU',cycle_valid=False,timing_valid=False,raw_cycles=None)
        return r
    def wait(self,id):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            p=self.call('status',{'run_id':id});reply=json.loads(p.stdout)
            if reply['state']!='RUNNING':return p,reply
            time.sleep(.1)
        self.fail('bounded software callback did not complete')
    def test_long_run_reconnect_same_id_and_busy_switch(self):
        r=self.request('abababababababababababababababab');t=time.monotonic();p=self.call('run-mosaic',r)
        self.assertEqual(p.returncode,0,p.stdout);self.assertLess(time.monotonic()-t,2)
        self.assertTrue((self.root/r['run_id']/'request.json').is_file())
        self.assertEqual(self.call('run-mosaic',r).returncode,0)
        self.assertFalse(json.loads(self.call('select-app',{'app':'hunet'}).stdout)['ok'])
        p,reply=self.wait(r['run_id']);self.assertEqual(p.returncode,0,reply)
        self.assertEqual(reply['result']['model_call_count'],1);self.assertIsNone(reply['raw_cycles'])
        self.assertEqual(self.call('run-mosaic',r).returncode,0)
        self.assertEqual(self.call('select-app',{'app':'hunet'}).returncode,0)
        self.assertFalse(json.loads(self.call('display',{'run_id':r['run_id'],'source_result_sha':reply['source_result_sha'],'composition':'RESULT'}).stdout)['ok'])
        export_call=self.call('export',{'run_id':r['run_id']});exported=json.loads(export_call.stdout)
        self.assertEqual(export_call.returncode,0,exported)
        self.assertEqual(exported['result']['model_call_count'],1)
    def test_disconnect_after_command_does_not_cancel_or_resubmit(self):
        r=self.request('cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd');c=socket.socket(socket.AF_UNIX);c.connect(self.sock);c.sendall((json.dumps({'action':'run-mosaic','request':r})+'\n').encode());c.close()
        deadline=time.monotonic()+3
        while not (self.root/r['run_id']/'request.json').exists():
            self.assertLess(time.monotonic(),deadline);time.sleep(.01)
        p,reply=self.wait(r['run_id']);self.assertEqual(p.returncode,0,reply);self.assertEqual(reply['result']['model_call_count'],1)
    def test_fault_quarantines_and_does_not_restart(self):
        r=self.request('efefefefefefefefefefefefefefefef');r['fail_start']=True
        self.assertEqual(self.call('run-mosaic',r).returncode,0)
        p,reply=self.wait(r['run_id']);self.assertNotEqual(p.returncode,0);self.assertEqual(reply['state'],'QUARANTINED')
        self.assertNotEqual(self.call('run-mosaic',r).returncode,0)

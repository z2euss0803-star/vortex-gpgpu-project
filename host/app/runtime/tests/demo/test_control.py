import unittest
from demo.control import control
from demo.transport import TransportError
from tests.demo.test_contracts import fixture


class Calls:
    def __init__(self):self.calls=[];self.disconnected=True
    def call(self,action,request):
        self.calls.append((action,request))
        if self.disconnected:raise TransportError('connection lost',ambiguous=action.startswith('run-'))
        return {'ok':True,'scope':'MOCK','state':'NOT_RUN','raw_cycles':None}


class Control(unittest.TestCase):
    def test_disconnect_run_then_status_only(self):
        t=Calls();request=fixture();request['state']='STAGED'
        with self.assertRaises(TransportError):control('run-mosaic',request,t)
        self.assertEqual(len(t.calls),1)
        t.disconnected=False;result=control('status',{'run_id':request['run_id']},t)
        self.assertEqual([x[0] for x in t.calls],['run-mosaic','status']);self.assertIsNone(result['raw_cycles'])

    def test_wrong_app_and_unknown_action_never_sent(self):
        t=Calls();request=fixture();request['state']='STAGED'
        for action in ('run-hunet','reboot','sd','reset'):
            with self.assertRaises(ValueError):control(action,request,t)
        self.assertEqual(t.calls,[])


    def test_select_requires_literal_application(self):
        t=Calls();t.disconnected=False
        control('select-app',{'app':'mosaic'},t)
        control('select-app',{'app':'hunet'},t)
        for app in ('shell','mosaic;reboot','',None):
            with self.assertRaises(ValueError):control('select-app',{'app':app},t)
        self.assertEqual([x[1]['app'] for x in t.calls],['mosaic','hunet'])

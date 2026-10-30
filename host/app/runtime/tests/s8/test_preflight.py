"""Catch admission bypasses; resource fixtures are literal, not generated."""
from pathlib import Path
import copy
import importlib
import unittest
import subprocess
import tempfile

class PreflightTests(unittest.TestCase):
    def api(self):
        try:
            return importlib.import_module('scripts.s8.preflight')
        except ModuleNotFoundError:
            self.fail('S8 production preflight admission is not implemented')

    def snapshot(self):
        return dict(host='z2euss-virtual-machine',account='z2euss',owner='s8',
                    coordination='CLEAR',other_heavy=[],memory_available_bytes=8<<30,
                    disk_available_bytes=4<<30,captured_at=1000,cpu_available=4)

    def request(self):
        return dict(host='z2euss-virtual-machine',account='z2euss',owner='s8',
                    memory_bytes=2<<30,disk_bytes=1<<30,cpu_jobs=1,now=1001,max_age=300)

    def test_wrong_parent_and_busy_owner_refuse(self):
        api=self.api()
        with self.assertRaises(ValueError):
            api.verify_parent(Path('.'), '0'*40)
        for key,value in [('owner','C'),('coordination','UNKNOWN'),('other_heavy',['bitbake']),
                          ('memory_available_bytes',0),('disk_available_bytes',0),
                          ('account','root'),('captured_at',0),('cpu_available',0)]:
            snap=self.snapshot();snap[key]=value
            receipt=api.admit_resources(snap,self.request())
            self.assertFalse(receipt['admitted'], (key,receipt))
            self.assertTrue(receipt['reasons'])

    def test_valid_budget_and_staleness(self):
        api=self.api()
        self.assertTrue(api.admit_resources(self.snapshot(),self.request())['admitted'])
        for key,value in [('cpu_jobs',0),('cpu_jobs',3),('memory_bytes',-1),
                          ('disk_bytes','1GB'),('now',float('nan')),('max_age',100000)]:
            req=self.request();req[key]=value
            self.assertFalse(api.admit_resources(self.snapshot(),req)['admitted'],key)
        snap=self.snapshot();snap['captured_at']=1002
        self.assertFalse(api.admit_resources(snap,self.request())['admitted'])

    def test_parent_identity_does_not_mutate_repository(self):
        api=self.api()
        # Source archives deliberately omit .git. Exercise actual Git on an
        # isolated fixture; the real frozen parent is checked before freezing.
        with tempfile.TemporaryDirectory() as tmp:
            repo=Path(tmp)
            def git(*args):
                return subprocess.check_output(['git','-C',str(repo),*args],stderr=subprocess.STDOUT).decode().strip()
            git('init');git('config','user.name','S8 software fixture')
            git('config','user.email','s8-fixture@invalid')
            (repo/'input').write_text('TEST_ONLY parent identity\n')
            git('add','input');git('commit','-m','fixture parent')
            parent=git('rev-parse','HEAD')
            (repo/'input').write_text('TEST_ONLY dirty bytes preserved\n')
            before=git('status','--porcelain')
            row=api.verify_parent(repo,parent)
            self.assertEqual(row['parent'],parent)
            self.assertTrue(row['ancestor_verified'])
            self.assertEqual(before,git('status','--porcelain'))
            self.assertEqual((repo/'input').read_text(),'TEST_ONLY dirty bytes preserved\n')

if __name__ == '__main__':unittest.main()

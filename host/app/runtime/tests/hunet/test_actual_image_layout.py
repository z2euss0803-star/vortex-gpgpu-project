import importlib.util,pathlib,stat,subprocess,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
import image_audit as audit

class ActualImageLayout(unittest.TestCase):
 def test_out_of_tree_builtin_source_header(self):
  tmp=ROOT/'build/test-tmp';tmp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=tmp) as td:
   p=pathlib.Path(td);src=p/'source';build=p/'build';build.mkdir()
   rel='drivers/misc/vxl/driver';header=src/rel/'vxl_hunet_source.h';header.parent.mkdir(parents=True);header.write_text('/* binding */')
   probe=p/'probe.mk';probe.write_text('print:\n\t@printf "%s\\n" "$(ccflags-y)"\n')
   def flags(s):return subprocess.check_output(['make','-s','-f',str(ROOT/'driver/Makefile'),'-f',str(probe),'print','src='+s,'srctree='+str(src)],cwd=build,universal_newlines=True)
   self.assertIn('-DVXL_HUNET_SOURCE_BOUND',flags(rel))
   self.assertIn(str(header.parent),flags(rel))
   self.assertIn('-DVXL_HUNET_SOURCE_BOUND',flags(str(header.parent)))
   header.unlink();self.assertNotIn('-DVXL_HUNET_SOURCE_BOUND',flags(rel))

 def test_disabled_cdma_channel_is_not_an_owner(self):
  parent='/amba_pl@0/dma@b0010000'
  nodes={'/':{},'/amba_pl@0':{},parent:dict(compatible='xlnx,axi-cdma-4.1 xlnx,axi-cdma-1.00.a',status='disabled'),parent+'/dma-channel@b0010000':dict(compatible='xlnx,axi-cdma-channel')}
  self.assertEqual(audit.cdma_driver_owners(nodes),0)
  nodes[parent]['status']='okay';self.assertEqual(audit.cdma_driver_owners(nodes),1)
  nodes['/amba_pl@0']['status']='disabled';self.assertEqual(audit.cdma_driver_owners(nodes),0)

 def entries(self):
  def e(kind,mode,data=b''):return dict(kind=kind,mode=mode,uid=0,data=data)
  return {'home/root':e(stat.S_IFDIR,0o700),'home/root/.ssh':e(stat.S_IFDIR,0o700),'home/root/.ssh/authorized_keys':e(stat.S_IFREG,0o600,b'KEY'),
   'usr/sbin/dropbear':e(stat.S_IFLNK,0o777,b'./dropbearmulti'),'usr/sbin/dropbearmulti':e(stat.S_IFREG,0o755,b'ELF payload'),
   'etc/init.d/dropbear':e(stat.S_IFREG,0o755,b'#!/bin/sh\nstart service\n'),
   'etc/inittab':e(stat.S_IFREG,0o644,b'id:5:initdefault:\nl5:5:wait:/etc/init.d/rc 5\n'),
   'etc/rc5.d/S10dropbear':e(stat.S_IFLNK,0o777,b'../init.d/dropbear')}

 def test_dropbear_multicall_and_real_default_runlevel(self):
  entries=self.entries();self.assertEqual(audit.ssh_entry_errors(entries),[])
  self.assertEqual(audit.resolved_regular_files(entries)['usr/sbin/dropbear'],b'ELF payload')
  entries['home/root']['mode']=0o755;self.assertEqual(audit.ssh_entry_errors(entries),[])
  entries['home/root']['mode']=0o777;self.assertTrue(audit.ssh_entry_errors(entries))
  entries=self.entries();entries['home/root/.ssh']['uid']=1;self.assertTrue(audit.ssh_entry_errors(entries))
  entries=self.entries();entries['etc/rc2.d/S10dropbear']=entries.pop('etc/rc5.d/S10dropbear');self.assertTrue(audit.ssh_entry_errors(entries))
  entries=self.entries();entries['usr/sbin/dropbear']['data']=b'../../../../outside';self.assertTrue(audit.ssh_entry_errors(entries))
  entries=self.entries();entries['usr/sbin/dropbear']['data']=b'./dropbear';self.assertTrue(audit.ssh_entry_errors(entries))

if __name__=='__main__':unittest.main()

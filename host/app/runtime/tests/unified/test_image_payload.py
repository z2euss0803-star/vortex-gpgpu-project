"""U24 rootfs rejection probes; no device, SD or fabricated inference."""
import hashlib,json,pathlib,copy,unittest,tempfile
from scripts.unified.audit_image import payload_errors

class InstalledPayload(unittest.TestCase):
 def test_failed_kernel_cannot_create_any_payload_or_image_output(self):
  from scripts.unified.image import prepare_payload,prepare_image
  from scripts.unified.cross_common import snapshot
  with tempfile.TemporaryDirectory() as temp:
   base=pathlib.Path(temp);root=base/'source';root.mkdir();(root/'real-input').write_bytes(b'unchanged')
   lock=base/'source-lock.json';raw=json.dumps(dict(schema=1,snapshot=snapshot(root)),sort_keys=True,separators=(',',':')).encode();lock.write_bytes(raw);lock.chmod(0o400)
   env=base/'env.json';env.write_text(json.dumps(dict(root=str(root),source_lock=str(lock),source_lock_sha256=hashlib.sha256(raw).hexdigest())))
   bound=base/'kernel';bound.mkdir();(bound/'receipt.json').write_text(json.dumps(dict(scope='HOST_FIXTURE_NOT_BUILT_KERNEL',natural_exit=1,gpu_raw_cycles=None)))
   for function,args in [(prepare_payload,[str(env),1,str(bound),str(root/'build/rejected-payload')]),(prepare_image,[str(env),1,str(bound),str(base/'no-payload'),str(root/'build/rejected-image')])]:
    with self.subTest(function=function),self.assertRaisesRegex(ValueError,'actual completed matching U23'):function(*args)
   self.assertFalse((root/'build').exists())
 def fixture(self):
  source={
   'etc/codex-build-id':b'VXU-S8-TEST-DECLARATION\n',
   'etc/default/vxgpud':b'UNIFIED_SERVICE_ENABLED=0\n',
   'etc/vxgpud/config.json':b'{"backend_mode":"compute-v2"}\n',
   'etc/vxgpud/installation.json':b'{"version":2}\n',
   'etc/vxgpud/installation.sha256':b'declared\n',
   'etc/init.d/codex-direct-network':b'#!/bin/sh\nip link set dev eth0 up || exit $?\nif ip -4 addr show dev eth0 | grep -q "inet 192.168.13.10/24"; then exit 0; fi\nip addr add 192.168.13.10/24 dev eth0\n',
   'etc/network/interfaces':b'auto lo\niface lo inet loopback\nauto eth0\niface eth0 inet static\n address 192.168.13.10\n netmask 255.255.255.0\n',
   'etc/init.d/vxgpud':b'#!/bin/sh\n. /etc/default/vxgpud\n[ "$UNIFIED_SERVICE_ENABLED" = 1 ] || exit 0\nexec /usr/bin/vxgpud --serve /etc/vxgpud/config.json\n',
   'usr/bin/vxgpud':b'actual target ELF placeholder FOR UNIT ONLY',
  }
  entries={};expected={}
  for name,raw in source.items():
   mode=0o400 if name in ('etc/vxgpud/installation.json','etc/vxgpud/installation.sha256') else 0o600 if name.startswith('etc/vxgpud/') else 0o755 if name.startswith(('usr/bin/','etc/init.d/')) else 0o644
   entries[name]=dict(kind='file',data=raw,mode=mode,uid=0,gid=0)
   expected[name]=dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),mode=mode)
  entries['etc/rcS.d/S40codex-direct-network']=dict(kind='symlink',link='../init.d/codex-direct-network',mode=0o777,uid=0,gid=0)
  entries['etc/rc5.d/S95vxgpud']=dict(kind='symlink',link='../init.d/vxgpud',mode=0o777,uid=0,gid=0)
  return entries,dict(schema=1,scope='MATCHED_IMAGE_PAYLOAD_NOT_EXECUTED',provider_flags=1,build_id='VXU-S8-TEST-DECLARATION',files=expected,gpu_raw_cycles=None)
 def test_exact_sealed_files_and_disabled_service_accepted(self):
  e,m=self.fixture();self.assertEqual(payload_errors(e,m),[])
 def test_wrong_bytes_size_mode_owner_or_symlink_are_rejected(self):
  for key,value in [('data',b'different'),('mode',0o666),('uid',1000),('kind','symlink')]:
   with self.subTest(key=key):
    e,m=self.fixture();e['etc/vxgpud/config.json'][key]=value;self.assertTrue(payload_errors(e,m),'installed trust file must reject '+key)
 def test_mutated_manifest_cannot_enable_boot_gpu_jobs(self):
  e,m=self.fixture();raw=b'UNIFIED_SERVICE_ENABLED=1\n';e['etc/default/vxgpud']['data']=raw;m['files']['etc/default/vxgpud'].update(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
  self.assertTrue(payload_errors(e,m),'service stays OFF even with self-consistent changed file hash')
 def test_old_busybox_replace_and_dhcp_conflict_rejected(self):
  for name,raw in [('etc/init.d/codex-direct-network',b'#!/bin/sh\nip addr replace 192.168.13.10/24 dev eth0\n'),('etc/network/interfaces',b'auto eth0\niface eth0 inet dhcp\n')]:
   e,m=self.fixture();e[name]['data']=raw;m['files'][name].update(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest());self.assertTrue(payload_errors(e,m),'known missing IPv4 failure must not recur')
 def test_factory_gpu_autostart_or_bad_init_order_rejected(self):
  for name,link in [('etc/rc5.d/S20demo-viewer','../init.d/demo-viewer'),('etc/rcS.d/S10vxgpud','../init.d/vxgpud')]:
   e,m=self.fixture();e[name]=dict(kind='symlink',link=link,mode=0o777,uid=0,gid=0);self.assertTrue(payload_errors(e,m),'one display/device owner and explicit startup order required')
 def test_offline_manifest_cannot_invent_cycles_or_provider(self):
  for key,value in [('gpu_raw_cycles',123),('provider_flags',3),('scope','REAL_BOARD_PASS')]:
   e,m=self.fixture();m[key]=value;self.assertTrue(payload_errors(e,m),'software image cannot invent live acceptance')

if __name__=='__main__':unittest.main()

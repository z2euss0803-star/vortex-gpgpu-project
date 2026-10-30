import pathlib,tempfile,unittest,hashlib,struct,re
from scripts.unified.image import bootgen_tool,boot_bif,image_bootgen_reprepare
from scripts.unified.audit_image import client_runtime_errors
ROOT=pathlib.Path(__file__).resolve().parents[2]
def elf(machine):
 raw=bytearray(64);raw[:7]=b'\x7fELF\x02\x01\x01';struct.pack_into('<H',raw,18,machine);return bytes(raw)
def complete_client():
 files={'usr/bin/vxgpuctl':b'#!/usr/bin/env python3\n','usr/bin/python3':elf(183)}
 for name in ('argparse.py','json/__init__.py','pathlib.py','socket.py','struct.py','os.py','stat.py','encodings/__init__.py','codecs.py','collections/__init__.py','re.py','hashlib.py'):files['usr/lib/python3.7/'+name]=b'frozen standard-library fixture\n'
 for name in ('_socket','_struct','_sha256'):files['usr/lib/python3.7/lib-dynload/'+name+'.cpython-37m-aarch64-linux-gnu.so']=elf(183)
 return files
class ActualImageRuntimeClosure(unittest.TestCase):
 def tool_fixture(self):
  d=tempfile.TemporaryDirectory();self.addCleanup(d.cleanup);root=pathlib.Path(d.name).resolve();settings=root/'settings.sh';settings.write_text('actual settings fixture');tool=root/'components/yocto/buildtools/sysroots/x86_64-petalinux-linux/usr/bin/bootgen';tool.parent.mkdir(parents=True);tool.write_bytes(elf(62));tool.chmod(0o755);return settings,tool,hashlib.sha256(tool.read_bytes()).hexdigest()
 def test_pinned_actual_buildtools_bootgen_not_ambient_path(self):
  settings,tool,digest=self.tool_fixture();self.assertEqual(bootgen_tool(settings,digest),tool)
 def test_changed_or_wrong_arch_bootgen_refused(self):
  for machine in (62,183):
   with self.subTest(machine=machine):
    settings,tool,digest=self.tool_fixture();tool.write_bytes(elf(machine)+b'changed')
    with self.assertRaises(ValueError):bootgen_tool(settings,digest)
 def test_explicit_bif_has_all_six_inputs_and_actual_dt_load(self):
  im=pathlib.PurePosixPath('/candidate/project/images/linux');bit='/frozen/hardware/top.bit';got=boot_bif(im,bit)
  self.assertIn('[load=0x100000, destination_cpu=a53-0] "'+str(im/'system.dtb')+'"',got)
  for name in ('zynqmp_fsbl.elf','pmufw.elf','bl31.elf','u-boot.elf'):self.assertEqual(got.count(str(im/name)),1)
  self.assertEqual(got.count(bit),1);self.assertLess(got.index('system.dtb'),got.index('u-boot.elf'));self.assertIn('[pmufw_image]',got)
 def test_dt_partition_targets_a53_as_in_actual_native_boot_header(self):
  # Real 1504/1514: native DT attributes0x116, old verifier0x16; the
  # destination CPU bit and its header checksum were the only differences.
  got=boot_bif('/candidate/project/images/linux','/frozen/hardware/top.bit')
  line=next(row for row in got.splitlines() if row.endswith('/system.dtb"'))
  attributes={part.strip() for part in line.split('[',1)[1].split(']',1)[0].split(',')}
  self.assertIn('load=0x100000',attributes)
  self.assertIn('destination_cpu=a53-0',attributes)
 def test_bif_quoted_path_injection_refused(self):
  with self.assertRaises(ValueError):boot_bif('/candidate/images','/hardware/bad\"\nother.bit')
 def test_complete_python_controller_fixture_is_only_content_check(self):self.assertEqual(client_runtime_errors(complete_client()),[])
 def test_missing_interpreter_never_becomes_image_pass(self):
  files=complete_client();del files['usr/bin/python3'];self.assertTrue(client_runtime_errors(files))
 def test_each_required_standard_module_is_checked(self):
  for path in [x for x in complete_client() if x.startswith('usr/lib/python3.7/')]:
   with self.subTest(path=path):
    files=complete_client();del files[path];self.assertTrue(client_runtime_errors(files))
 def test_wrong_arch_python_and_extension_refused(self):
  for path in ('usr/bin/python3','usr/lib/python3.7/lib-dynload/_socket.cpython-37m-aarch64-linux-gnu.so'):
   with self.subTest(path=path):
    files=complete_client();files[path]=elf(62);self.assertTrue(client_runtime_errors(files))
 def test_target_wrapper_keeps_real_aarch64_interpreter(self):
  files=complete_client();files['usr/bin/.python3.7-real']=files['usr/bin/python3'];files['usr/bin/python3']=b'#!/bin/sh\nexec /usr/bin/.python3.7-real "$@"\n';self.assertEqual(client_runtime_errors(files),[])
 def test_recipe_declares_actual_controller_module_packages(self):
  raw=(ROOT/'petalinux/unified/runtime.bb').read_text();depends=re.findall(r'RDEPENDS_\$\{PN\}\s*\+?=\s*"([^"]*)"',raw);tokens=set(' '.join(depends).split())
  self.assertTrue({'python3-core','python3-json','python3-io','python3-misc'}<=tokens);self.assertNotIn('python3-subprocess',tokens)
class ExactObservedBootgenRepair(unittest.TestCase):
 def fixture(self):
  original=dict(scope='ACTUAL_COMPLETE_PROVIDER_BOOT_CHAIN_BUILD_NOT_BOARD',natural_exit=1,project='/private/owned',source_before=dict(id='closed'),commands=[dict(argv=['bash','-c','petalinux-build'],natural_exit=0),dict(argv=['bash','-c','petalinux-package --boot --fsbl /actual.elf'],natural_exit=0),dict(argv=['bash','-c','bootgen -arch zynqmp -image /verification.bif -o /verification.BOOT.BIN -w'],natural_exit=127)])
  proof=dict(scope='ACTUAL_UNCHANGED_SOURCE_AFTER_BOOTGEN_PATH_ERROR',natural_exit=0,project='/private/owned',snapshot=original['source_before'],project_metadata=[dict(path='/private/owned/project-spec/configs/config',sha256='1'*64,bytes=1)])
  return original,proof,'PetaLinux environment set\nbash: bootgen: command not found\n'
 def test_observed_postbuild_packaging_failure_may_reprepare_but_is_not_prior_pass(self):
  old,proof,log=self.fixture();self.assertTrue(image_bootgen_reprepare(old,proof,log));self.assertEqual(old['natural_exit'],1)
 def test_failed_compiler_unknown_error_changed_source_or_untyped_code_refused(self):
  import copy
  old,proof,log=self.fixture();cases=[]
  for index,value in ((0,255),(1,1),(2,True),(0,False)):
   changed=copy.deepcopy(old);changed['commands'][index]['natural_exit']=value;cases.append((changed,proof,log))
  cases.extend([(old,dict(proof,snapshot=dict(id='changed')),log),(old,dict(proof,project='/other'),log),(old,dict(proof,project_metadata=[]),log),(old,dict(proof,natural_exit=True),log),(old,proof,'ERROR: compile failed'),(dict(old,natural_exit=0),proof,log)])
  for args in cases:
   with self.subTest(args=args):self.assertFalse(image_bootgen_reprepare(*args))

class ProducerAuditorIdentity(unittest.TestCase):
 def fixture(self):
  import tempfile,json
  from scripts.unified.cross_common import snapshot,sha
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);base=pathlib.Path(temp.name)
  old=base/'producer';new=base/'auditor'
  rows={'driver/native.c':'int actual_runtime;\n',
        'scripts/unified/image.py':'def boot_bif(): return "old DT metadata"\ndef prepare_image(): return "actual producer"\n',
        'scripts/unified/audit_image.py':'def payload_errors(): return []\ndef fit_payloads(): return "old reader"\n',
        'tests/unified/test_image_boot.py':'# old test\n'}
  for root in (old,new):
   for name,raw in rows.items():
    path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(raw)
  before=snapshot(old);built=dict(source_before=before,source_after=before);payload=dict(source_before=before,source_after=before)
  lock=base/'producer-lock.json';lock.write_text(json.dumps(dict(schema=1,snapshot=before)))
  return old,new,built,payload,lock,sha(lock)
 def test_explicit_frozen_producer_and_checker_identity_are_separate(self):
  from scripts.unified.audit_image import image_source_provenance
  from scripts.unified.cross_common import snapshot
  old,new,built,payload,lock,digest=self.fixture()
  (new/'scripts/unified/audit_image.py').write_text('def payload_errors(): return []\ndef fit_payloads(): return "corrected reader"\n')
  got=image_source_provenance(new,built,payload,old,lock,digest)
  self.assertEqual(got['producer_source_snapshot_id'],snapshot(old)['source_snapshot_id'])
  self.assertEqual(got['auditor_source_snapshot_id'],snapshot(new)['source_snapshot_id'])
  self.assertNotEqual(got['producer_source_snapshot_id'],got['auditor_source_snapshot_id'])
  self.assertEqual(got['changed_checker_paths'],['scripts/unified/audit_image.py'])
 def test_metadata_only_generator_fix_does_not_claim_a_rebuild(self):
  from scripts.unified.audit_image import image_source_provenance
  old,new,built,payload,lock,digest=self.fixture()
  (new/'scripts/unified/image.py').write_text('def boot_bif(): return "corrected DT metadata"\ndef prepare_image(): return "actual producer"\n')
  self.assertEqual(image_source_provenance(new,built,payload,old,lock,digest)['changed_checker_paths'],['scripts/unified/image.py'])
 def test_runtime_payload_policy_or_producer_changes_are_rejected(self):
  from scripts.unified.audit_image import image_source_provenance
  cases={'driver/native.c':'int changed_runtime;\n',
         'scripts/unified/image.py':'def boot_bif(): return "corrected DT metadata"\ndef prepare_image(): return "different producer"\n',
         'scripts/unified/audit_image.py':'def payload_errors(): return ["changed policy"]\ndef fit_payloads(): return "corrected reader"\n'}
  for name,raw in cases.items():
   with self.subTest(name=name):
    old,new,built,payload,lock,digest=self.fixture();(new/name).write_text(raw)
    with self.assertRaises(ValueError):image_source_provenance(new,built,payload,old,lock,digest)
 def test_missing_lock_changed_producer_or_payload_cannot_reuse_build(self):
  from scripts.unified.audit_image import image_source_provenance
  old,new,built,payload,lock,digest=self.fixture()
  for arguments in ((old,None,None),(old,lock,'0'*64)):
   with self.assertRaises(ValueError):image_source_provenance(new,built,payload,*arguments)
  payload['source_after']=dict(payload['source_after'],source_snapshot_id='bad')
  with self.assertRaises(ValueError):image_source_provenance(new,built,payload,old,lock,digest)
  payload['source_after']=built['source_after'];(old/'driver/native.c').write_text('int changed_after_build;\n')
  with self.assertRaises(ValueError):image_source_provenance(new,built,payload,old,lock,digest)

if __name__=='__main__':unittest.main()

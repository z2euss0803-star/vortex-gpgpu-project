"""Actual archived HOST export metadata, never a GPU or numeric reference."""
import copy,hashlib,json,pathlib,unittest
from scripts.unified.native_origin import validate_native_origin

class Origin(unittest.TestCase):
 def setUp(self):
  raw=pathlib.Path('tests/unified/fixtures/native-mosaic-export-v1.json').read_bytes()
  self.assertEqual(hashlib.sha256(raw).hexdigest(),'21f65c40ab5a519451f3209302fd861338937de755deaeb0c38d617abf841a88')
  self.meta=json.loads(raw)
 def reject(self,change):
  value=copy.deepcopy(self.meta);change(value)
  with self.assertRaises((ValueError,KeyError,TypeError)):validate_native_origin(value)
 def test_actual_host_origin_retained(self):
  n=validate_native_origin(self.meta);self.assertEqual(n['evidence_scope'],'HOST_PROTOCOL_TEST');self.assertEqual(self.meta['scope'],'OFFLINE');self.assertIsNone(self.meta['raw_cycles']);self.assertEqual(n['numeric_class'],'NOT_EVALUATED');self.assertIn('numeric_contract_original',json.loads(n['raw_legacy_json']))
 def test_old_envelope_remains_supported(self):
  value=copy.deepcopy(self.meta);value.pop('native_origin');self.assertIsNone(validate_native_origin(value))
 def test_joint_flag_cannot_reuse_an_old_or_unbound_platform(self):
  self.reject(lambda m:m['native_origin'].update(provider_flags=4))
 def test_host_cannot_become_real_board(self):self.reject(lambda m:m['native_origin'].update(evidence_scope='REAL_BOARD'))
 def test_host_cannot_inherit_nonnull_cycles(self):self.reject(lambda m:m.update(cycle_valid=True,raw_cycles=123))
 def test_original_numeric_class_cannot_be_promoted(self):self.reject(lambda m:m['native_origin'].update(numeric_class='PASS'))
 def test_raw_receipt_cannot_be_replaced(self):self.reject(lambda m:m['native_origin'].update(raw_legacy_json='{}'))
 def test_original_tensor_digest_required(self):self.reject(lambda m:m['native_origin'].update(tensor='ac'*32))
 def test_envelope_model_agrees_with_origin(self):self.reject(lambda m:m['identities'].update(model='ac'*32))
 def test_epoch_cannot_change(self):self.reject(lambda m:m['native_origin'].update(device_generation=2))
 def test_provider_cannot_change(self):self.reject(lambda m:m['native_origin'].update(provider_flags=2))
 def test_request_cannot_change(self):self.reject(lambda m:m['native_origin'].update(request_id='different'))
 def test_quant_binding_cannot_change(self):self.reject(lambda m:m['identities'].update(quant='ac'*32))
 def test_layout_binding_cannot_change(self):self.reject(lambda m:m['identities'].update(layout='ac'*32))
 def test_source_input_extent_bounded(self):self.reject(lambda m:m['native_origin'].update(input_bytes=67108865))
 def test_missing_origin_fields_refused(self):self.reject(lambda m:m['native_origin'].pop('xsa'))
 def test_unknown_origin_fields_refused(self):self.reject(lambda m:m['native_origin'].update(force_ready=True))
if __name__=='__main__':unittest.main()

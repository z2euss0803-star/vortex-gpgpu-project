import copy
import importlib.util
import pathlib
import json
import unittest

S = pathlib.Path(__file__).resolve().parents[2]
path = S / 'scripts/gen_contract.py'
validator = None
if path.exists():
    module = importlib.util.spec_from_file_location('contracts', path)
    validator = importlib.util.module_from_spec(module)
    module.loader.exec_module(validator)

class Contracts(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(validator, 'contract validator is not implemented')
        self.profile = validator.extract_h0(S)

    def test_h0_actual_resources(self):
        validator.validate(self.profile)
        self.assertEqual(self.profile['registers'][0]['base'], 0xa0000000)
        self.assertEqual(self.profile['cdma']['address_bits'], 40)
        self.assertEqual(self.profile['cdma']['data_bits'], 128)
        self.assertFalse(self.profile['cdma']['irq_connected'])

    def test_wrong_sha(self):
        self.profile['xsa_sha256'] = '0' * 64
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_duplicate_register(self):
        self.profile['registers'].append(copy.deepcopy(self.profile['registers'][0]))
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_overlapping_windows(self):
        self.profile['windows'][1]['bus_base'] = self.profile['windows'][0]['bus_base']
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_no_invented_irq(self):
        self.profile['cdma']['irq_connected'] = True
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_proposed_is_not_board_ready(self):
        self.profile['status'] = 'PROPOSED'
        with self.assertRaises(ValueError): validator.validate(self.profile, board=True)

    def test_major_incompatible(self):
        self.profile['contract_major'] = 2
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_changed_done_semantics(self):
        self.profile['completion']['requires_jobs_increment'] = False
        with self.assertRaises(ValueError): validator.validate(self.profile)

    def test_generated_matches_inputs(self):
        published=json.loads((S/'contracts/profiles/h0-r4.json').read_text(encoding='utf-8'))
        self.assertEqual(published,self.profile)

if __name__ == '__main__': unittest.main()

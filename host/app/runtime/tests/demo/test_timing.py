import copy, unittest
from scripts.demo.audit_timing import audit_document

def fixture():
    return dict(schema=1, scope='OFFLINE_TIMELINE', raw_gpu_cycles=None, gpu_jobs=[], intervals=[
        dict(phase='gpu_submit_wait', clock='A53_MONOTONIC', begin_ns=100, end_ns=500, valid=True, parent=-1),
        dict(phase='guard_cdma', clock='A53_MONOTONIC', begin_ns=110, end_ns=200, valid=True, parent=0),
        dict(phase='poll', clock='A53_MONOTONIC', begin_ns=210, end_ns=300, valid=True, parent=0),
        dict(phase='cpu_post', clock='A53_MONOTONIC', begin_ns=500, end_ns=600, valid=True, parent=-1)])

class TimingTests(unittest.TestCase):
    def test_nested_times_not_double_added(self):
        r = audit_document(fixture())
        self.assertEqual(r['status'], 'HOST_AUDIT_PASS_NOT_NEW_TIMING')
        self.assertEqual(r['root_ns_by_clock']['A53_MONOTONIC'], 500)
        self.assertEqual(r['unclassified_ns'][0], 220)
        self.assertIsNone(r['raw_gpu_cycles'])

    def test_cross_host_and_outside_parent_rejected(self):
        for key, value in [('clock', 'B_MONOTONIC'), ('begin_ns', 90), ('end_ns', 501), ('parent', 2)]:
            d = fixture(); d['intervals'][1][key] = value
            self.assertEqual(audit_document(d)['status'], 'FAIL')

    def test_overlap_reversed_unknown_clock_and_duplicate(self):
        for mutation in ('overlap', 'reversed', 'unknown', 'duplicate'):
            d = fixture()
            if mutation == 'overlap': d['intervals'][2]['begin_ns'] = 150
            elif mutation == 'reversed': d['intervals'][0]['end_ns'] = 99
            elif mutation == 'unknown': d['intervals'][0]['clock'] = 'DRM_CLOCK_UNKNOWN'
            else: d['intervals'].append(copy.deepcopy(d['intervals'][0]))
            self.assertEqual(audit_document(d)['status'], 'FAIL')

    def test_bad_raw_is_retained_never_zeroed(self):
        d = fixture(); d['scope'] = 'REAL_BOARD_CAPTURE'
        d['gpu_jobs'] = [dict(observed_raw_u32=127273, raw_cycles=None, valid=False, generation=1, job_id=1, pre_ack=True, submit_enter=False, wrapped=False)]
        r = audit_document(d)
        self.assertEqual(r['status'], 'FAIL'); self.assertEqual(r['gpu_jobs'][0]['observed_raw_u32'], 127273)
        self.assertIsNone(r['gpu_jobs'][0]['raw_cycles'])
        d['scope'] = 'OFFLINE_TIMELINE'; self.assertEqual(audit_document(d)['status'], 'FAIL')

    def test_submitted_never_presented(self):
        d = fixture(); d['intervals'].append(dict(phase='display_submit', clock='A53_MONOTONIC', begin_ns=600, end_ns=610, valid=True, parent=-1))
        self.assertFalse(audit_document(d)['display_presented_proven'])

if __name__ == '__main__': unittest.main()

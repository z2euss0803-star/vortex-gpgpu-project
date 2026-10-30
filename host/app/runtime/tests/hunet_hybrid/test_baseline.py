import copy,pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
from freeze import freeze_contract,verify_baseline

class Baseline(unittest.TestCase):
    def test_unchanged_100_stage_1023_catalog(self):
        c=freeze_contract(ROOT);verify_baseline(c)
        self.assertEqual((c['logical_jobs'],c['gpu_jobs'],c['cpu_jobs'],c['stages']),(1023,1010,13,100))
        self.assertEqual(sum(x['elements'] for x in c['cpu_stages']),1869056)
        self.assertEqual(c['cpu_stages'][0]['sequence'],296)
        self.assertEqual(c['maximum_cpu_bytes'],1572864)
    def test_no_new_hardware_or_relaxed_numeric(self):
        c=freeze_contract(ROOT)
        for mutation in [lambda x:x['cpu_stages'][0].update(sequence=295),
                         lambda x:x['identities'].update(weights='00'*32),
                         lambda x:x['identities'].update(original_acceptance='00'*32),
                         lambda x:x['profile'].update(ps_size=2097152),
                         lambda x:x.update(gpu_jobs=1023)]:
            changed=copy.deepcopy(c);mutation(changed)
            with self.assertRaises(ValueError):verify_baseline(changed)

if __name__=='__main__':unittest.main()

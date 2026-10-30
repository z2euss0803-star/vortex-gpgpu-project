import hashlib,importlib.util,json,pathlib,tarfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
ARCHIVE=ROOT.parent/'s8_300s_optimization_20261026/evidence/a-r4-mosaic-batch-admission-291/mosaic-c256-p4096.tar.gz'
class MosaicPhysicalFusion(unittest.TestCase):
 def inputs(self):
  with tarfile.open(ARCHIVE) as archive:
   def load(name):
    rows=[m for m in archive.getmembers() if m.isfile() and pathlib.PurePosixPath(m.name).name==name];self.assertEqual(len(rows),1);return archive.extractfile(rows[0]).read()
   manifest=json.loads(load('manifest.json'));data={name:load(name) for name in ('graph.json','binding.json')}
   for name,raw in data.items():self.assertEqual(hashlib.sha256(raw).hexdigest(),next(x for x in manifest['assets'] if x['path']==name)['sha256'])
   return json.loads(data['graph.json']),json.loads(data['binding.json'])
 def module(self):
  path=ROOT/'scripts/unified/fuse_mosaic_pointwise.py';self.assertTrue(path.is_file(),'physical Mosaic fusion transform required');spec=importlib.util.spec_from_file_location('fuse_tested',str(path));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
 def test_actual_full_graph_reduces_nodes_jobs_and_intermediate_tensors(self):
  m=self.module();graph,binding=self.inputs();out,audit=m.fuse(graph,binding['nodes'],4096,binding);self.assertEqual(len(out['nodes']),47);self.assertEqual(len(out['tensors']),100);self.assertEqual(sum(x['job_count'] for x in out['nodes']),14643);self.assertEqual(sum('NormStats' in x['operator']['name'] for x in out['nodes']),15)
  old_conv=[x for x in graph['nodes'] if 'Conv' in x['operator']['name']];new_conv=[x for x in out['nodes'] if 'Conv' in x['operator']['name']];self.assertEqual([x['job_count'] for x in old_conv],[x['job_count'] for x in new_conv]);self.assertEqual(sum(len(x['outputs'])==2 for x in out['nodes']),5);self.assertEqual(len(audit),47)
  produced=set(out['inputs']);tensors={x['id'] for x in out['tensors']};produced|={x['id'] for x in out['tensors'] if x['constant']}
  for node in out['nodes']:
   self.assertTrue(set(node['inputs'])<=produced);self.assertFalse(set(node['outputs'])&produced);produced.update(node['outputs']);self.assertTrue(set(node['inputs']+node['outputs'])<=tensors)
  self.assertEqual(out['outputs'],graph['outputs'])
 def test_wrong_dependency_or_missing_phase_is_refused(self):
  m=self.module();graph,binding=self.inputs();graph['nodes'][4]['inputs']=[1]
  with self.assertRaises(ValueError):m.fuse(graph,binding['nodes'],4096,binding)
 def test_five_residual_reads_remain_disjoint_from_new_saved_outputs(self):
  m=self.module();graph,binding=self.inputs();out,_=m.fuse(graph,binding['nodes'],4096,binding);adds=[n for n in out['nodes'] if 'NormAddQuant' in n['operator']['name']];self.assertEqual(len(adds),5)
  for n in adds:self.assertEqual(len(n['inputs']),4);self.assertTrue(set(n['inputs']).isdisjoint(n['outputs']))
if __name__=='__main__':unittest.main()

"""Fresh internal capacity facts; this function never acquires or drops locks."""
from scripts.demo.resources import admit_resources
import pathlib

def build_output_resource_path(root,out):
 """Read-only capacity probe on the actual new output's filesystem.

 Match new_build_output's existing path boundary before measuring anything;
 use the nearest existing ancestor so admission still precedes mkdir. A real
 mount at source/build is measured there rather than at a read-only parent.
 """
 root=pathlib.Path(root).resolve();base=(root/'build').resolve();target=pathlib.Path(out).resolve()
 if base!=root/'build':raise ValueError('source/build must be a direct real workspace directory')
 try:relative=target.relative_to(base)
 except ValueError:raise ValueError('cross outputs must stay under the explicit source/build workspace')
 if not relative.parts:raise ValueError('a new named cross build directory is required')
 while not target.exists():target=target.parent
 return target

def resource_gate(env,phase):
 workers=env.get('worker_count',1)
 if type(workers) is not int or workers not in (1,2) or (phase=='image' and workers!=1):raise ValueError('worker configuration refused')
 gate=admit_resources(env,phase);ram=gate['required_ram']*workers
 if env['available_ram']<ram:raise ValueError('parallel compiler RAM reserve refused')
 gate.update(worker_count=workers,compiler_jobs=workers,test_runner_jobs=1,required_ram=ram,gpu_raw_cycles=None)
 return gate

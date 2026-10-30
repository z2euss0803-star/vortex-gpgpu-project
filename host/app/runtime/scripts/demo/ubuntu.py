"""Command outcome admission is independent from resource admission and board PASS."""
from scripts.demo.resources import admit_resources
def run_checked(env, kind, command, execute, required_marker=b''):
    try:gate=admit_resources(env,kind)
    except ValueError as e:return dict(state='RESOURCE_REFUSED',error=str(e),spawned=False)
    if not isinstance(command,list) or not command or any(not isinstance(v,str) for v in command):raise ValueError('literal argv required')
    code,output=execute(command)
    bad=any(v in output for v in (b'AddressSanitizer:',b'UndefinedBehaviorSanitizer:',b'runtime error:',b'FAIL',b'Fatal'))
    ok=code==0 and required_marker in output and not bad
    return dict(state='SOFTWARE_COMMAND_PASS_NOT_BOARD' if ok else 'FAILED',spawned=True,natural_exit=code,resource_gate=gate,raw_gpu_cycles=None)

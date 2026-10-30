"""Diagnostic experiment preserves each mode's original output and exit."""
import subprocess,sys
for mode in ('aggregate','named','constructed','result-aggregate','result-named'):
 p=subprocess.run([sys.argv[1],mode]);print('OOM_PROBE mode=%s natural_exit=%d'%(mode,p.returncode),flush=True)
# A probe reports data; its natural exit is never aggregate application PASS.

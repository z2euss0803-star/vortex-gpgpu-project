"""Record all independent allocation-failure exit paths, including first failures."""
import subprocess,sys
failed=False
for mode in ('result','scheduler','runtime','result-empty','scheduler-empty','runtime-empty'):
 p=subprocess.run([sys.argv[1],mode])
 print('PINNED_EXIT mode=%s natural_exit=%d'%(mode,p.returncode),flush=True)
 failed=failed or p.returncode!=0
sys.exit(1 if failed else 0)

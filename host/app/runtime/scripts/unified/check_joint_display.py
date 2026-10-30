"""Actual host Runtime and original fake KMS regression, never physical DP."""
import pathlib,subprocess,sys
out=pathlib.Path('build')/sys.argv[1]
subprocess.check_call(['make','-f','Makefile.unified','OUT='+str(out),str(out/'test_native_display'),'-j1'])
subprocess.check_call([str(out/'test_native_display'),'joint-cookie'])
for mode in ('flow','roots'):subprocess.check_call([str(out/'test_native_display'),mode])

"""Compile only: native boundary regression plus actual matched AArch64 module."""
import subprocess,sys
def main():
 name=sys.argv[1]
 rc=subprocess.call(['make','-f','Makefile.unified','OUT=build/'+name+'-unit','test-v2-execution','test-v2-provider','-j1'])
 if rc:return rc
 return subprocess.call([sys.executable,'scripts/unified/check_components_cross.py',name])
if __name__=='__main__':sys.exit(main())

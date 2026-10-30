#!/usr/bin/env python3
"""Explicit Ubuntu GCC7 software-test adapter, never a cross compiler."""
import subprocess,sys
def command(args):
 return ['/usr/bin/g++','-Iscripts/s8/gcc7compat']+list(args)+([] if any(x in args for x in ('-c','-E','-S','--version')) else ['-lstdc++fs'])
if __name__=='__main__':raise SystemExit(subprocess.call(command(sys.argv[1:])))

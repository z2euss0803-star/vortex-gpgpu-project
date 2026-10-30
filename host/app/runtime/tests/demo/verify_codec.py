import json,sys
from pathlib import Path
from demo.contracts import validate_run

def verify(original,roundtrip):
    left=validate_run(json.loads(Path(original).read_text(encoding='utf-8')))
    right=validate_run(json.loads(Path(roundtrip).read_text(encoding='utf-8')))
    if left!=right:raise ValueError('C/Python codec disagreement')
    print('DEMO_C_PYTHON_VALUES_EXACT_NOT_BOARD')

if __name__=='__main__':verify(*sys.argv[1:])

import inspect
from scripts.unified.generate_mosaic_package import generate
parameters=inspect.signature(generate).parameters
assert 'cooperative' in parameters and 'profiling_options' in parameters, 'EXPECTED_RED_EXPLICIT_COOPERATIVE_PACKAGE_POLICY_MISSING'

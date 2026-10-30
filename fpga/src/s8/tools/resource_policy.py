"""Explicit user resource-policy override; no RTL/numeric contract change."""
from pathlib import Path
import hashlib,json

def resource_policy():
    path=Path(__file__).resolve().parents[1]/'config/resource_policy.json'
    data=path.read_bytes();p=json.loads(data)
    if (p['schema_version']!=1 or p['physical_comparison']!='>=' or p['physical_min_gib']!=4
        or p['commit_min_gib']!=8 or p['other_heavy_required']!=0 or p['max_threads']!=4):
        raise ValueError('resource policy differs from current user authorization')
    return {**p,'path':str(path),'sha256':hashlib.sha256(data).hexdigest()}

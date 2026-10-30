"""Explicit Mosaic family selection. Historical publication remains immutable."""
from scripts.unified.program_identity import selected
CURRENT="mosaic-int16-v8"
DEFAULT_VARIANT='reuse-v8'
RO64_CURRENT='mosaic-int16-v3'
LEGACY="mosaic-int16-v2"
VARIANTS=('pack32-ro64-v3','fused-pointwise-v4','cooperative-v6','reuse-v7','reuse-v8')

def resolve_variant(variant=None,*,fused=None,cooperative=None):
    if any(v is not None and type(v) is not bool for v in (fused,cooperative)):
        raise ValueError('EXACT_MOSAIC_VARIANT_BOOL_REQUIRED')
    if variant is not None and (type(variant) is not str or variant not in VARIANTS):
        raise ValueError('UNKNOWN_EXPLICIT_MOSAIC_VARIANT')
    if variant is None:
        if cooperative is True:
            if fused is not True:raise ValueError('EXPLICIT_COOPERATIVE_FUSED47_FAMILY_REQUIRED')
            variant='cooperative-v6'
        elif fused is not None or cooperative is not None:
            variant='fused-pointwise-v4' if fused is True else 'pack32-ro64-v3'
        else:variant=DEFAULT_VARIANT
    if fused is not None and fused!=(variant!='pack32-ro64-v3'):
        raise ValueError('CONFLICTING_MOSAIC_FUSED_AND_VARIANT')
    if cooperative is not None and cooperative!=(variant=='cooperative-v6'):
        raise ValueError('CONFLICTING_MOSAIC_COOPERATIVE_AND_VARIANT')
    return variant

def requires_fusion(variant):return resolve_variant(variant)!='pack32-ro64-v3'

def selected_directory(root,*,variant=None,fused=None,cooperative=None):
    variant=resolve_variant(variant,fused=fused,cooperative=cooperative)
    if variant=='reuse-v8':
        from scripts.unified.mosaic_reuse_v8_program import selected_directory as reuse_v8
        return reuse_v8(root)
    if variant=='reuse-v7':
        # Keep the dedicated real family/build ABI4/schedule2/weight2/layout1/
        # profile2 metadata gates. The generic selector alone is insufficient.
        from scripts.unified.mosaic_reuse_program import selected_directory as reuse
        return reuse(root)
    if variant=='cooperative-v6':
        return selected(root,'mosaic-int16-v6',variant,
            '82455fa2e26f497c761da92c8c6c8bb4c2cd73e4c57f7cc9552ceafe0e3e8faf',
            'include/vxnn/mosaic_coop_program.hpp',
            lambda family,sources:dict(mosaic_coop_code_sha256=family['code_sha256'],
                mosaic_coop_validator_sha256=sources['device/vxnn/mosaic_wire_coop_v3.cpp'],
                mosaic_coop_work_layout_sha256=sources['device/vxnn/mosaic_work_layout_v1.h']))
    if variant=='fused-pointwise-v4':
        return selected(root,'mosaic-int16-v5',variant,
            'd383bac61157cda8463a61a40a3c542a99cc307863775bcd5f90c7d99f3a3bde',
            'include/vxnn/mosaic_fused_program.hpp',
            lambda family,sources:dict(mosaic_fused_code_sha256=family['code_sha256'],mosaic_fused_validator_sha256=sources['device/vxnn/mosaic_wire_fused_v1.cpp']))
    return selected(root,RO64_CURRENT,variant,
        'c6564333cf23245cedd82391cdf5054fdcf3fa677388e7ba6d05ca3d795df43d',
        'include/vxnn/mosaic_ro64_program.hpp',
        lambda family,sources:dict(mosaic_ro64_code_sha256=family['code_sha256'],
            mosaic_ro64_validator_sha256=sources['device/vxnn/mosaic_wire_ro64.cpp'],
            mosaic_pack32_source_sha256=sources['device/mosaic_s8/quant_pack.cpp']))

def selected_name(root,*,variant=None,fused=None,cooperative=None):
    return selected_directory(root,variant=variant,fused=fused,cooperative=cooperative).name

def resolve_requested_variant(env,*,variant=None,fused=None):
    """Explicit CLI/keyword and environment must agree before any output write."""
    configured=env.get('mosaic_variant');legacy=env.get('mosaic_fused')
    if configured is not None and variant is not None and configured!=variant:
        raise ValueError('CONFLICTING_ENVIRONMENT_MOSAIC_VARIANT')
    if legacy is not None and fused is not None and (type(legacy) is not bool or legacy!=fused):
        raise ValueError('CONFLICTING_ENVIRONMENT_MOSAIC_FUSED')
    return resolve_variant(configured if variant is None else variant,
                           fused=legacy if fused is None else fused)

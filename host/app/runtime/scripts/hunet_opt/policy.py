"""Explicit per-candidate build flags; not a performance ranking."""
def build_policy(candidate):
    from scripts.hunet_opt.graph import CANDIDATES
    if candidate not in CANDIDATES:raise ValueError('CANDIDATE')
    combined=candidate=='COMBINED';scalar=candidate.startswith('G4') or combined
    return dict(load_fastwait=candidate=='C1_H' or combined,
        parallel_gn=candidate in ('G1','G3_NORM','G3_VIEWS') or combined,
        control_page=candidate in ('G3_VIEWS','G2_TIMING') or scalar,
        user_kernel_sha='auto' if candidate=='C1_H' or combined else 'portable',
        diagnostics_default=False,locate=candidate=='G2_LOCATE' or combined,
        pack=candidate=='G2_PACK' or combined,
        tile_n=4 if candidate=='G2_WMMA4' else 2 if candidate=='G2_WMMA2' or combined else 1,
        internal_timing=candidate=='G2_TIMING',views=candidate=='G3_VIEWS' or combined,
        scalar_chunk=(1 if candidate=='G4_C1' else 16 if candidate=='G4_C16' else 4) if scalar else 0,
        frozen_wire=candidate in ('C1_T','C1_H'))

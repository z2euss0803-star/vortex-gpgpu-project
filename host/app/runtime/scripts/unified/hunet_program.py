"""R13 HUNet default with explicit historical selection and source gates."""
from scripts.unified.program_identity import selected

CURRENT = "hunet-fp16-v3"
DEFAULT_VARIANT = "span32-tile2-v3"
VARIANTS = ("span32-v2", DEFAULT_VARIANT)


def resolve_variant(variant=None):
    if variant is None:
        return DEFAULT_VARIANT
    if type(variant) is not str or variant not in VARIANTS:
        raise ValueError("UNKNOWN_EXPLICIT_HUNET_VARIANT")
    return variant


def program_source_paths(variant=None):
    if resolve_variant(variant) == DEFAULT_VARIANT:
        return ["scripts/unified/hunet_tile2_program.py", "include/vxnn/hunet_tile2_program.hpp"]
    return ["include/vxnn/hunet_span_program.hpp"]


def selected_directory(root, *, variant=None):
    variant = resolve_variant(variant)
    if variant == DEFAULT_VARIANT:
        from scripts.unified.hunet_tile2_program import selected_directory as tile2
        return tile2(root)
    return selected(root, "hunet-fp16-v2", variant,
                    "79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89",
                    "include/vxnn/hunet_span_program.hpp",
                    lambda family, sources: dict(hunet_span_code_sha256=family["code_sha256"],
                                                  hunet_span_dispatch_sha256=sources["device/vxnn/hunet_scalar_dispatch.h"]))


def selected_name(root, *, variant=None):
    return selected_directory(root, variant=variant).name

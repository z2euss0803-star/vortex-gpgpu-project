"""Explicit R13 HUNet tile2 publication; does not change the default family."""
from scripts.unified.program_identity import selected

CURRENT = "hunet-fp16-v3"
VARIANT = "span32-tile2-v3"
PIN_HEADER = "include/vxnn/hunet_tile2_program.hpp"
SCHEMA_SHA256 = "79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89"


def pins(family, sources):
    return dict(hunet_tile2_code_sha256=family["code_sha256"],
                hunet_tile2_entry_sha256=sources["device/vxnn/hunet_entry.cpp"],
                hunet_tile2_grouped_gemm_sha256=sources["device/vxnn/hunet_grouped_gemm.h"],
                hunet_tile2_dispatch_sha256=sources["device/vxnn/hunet_scalar_dispatch.h"],
                hunet_tile2_schema_sha256=family["schema_sha256"])


def selected_directory(root):
    return selected(root, CURRENT, VARIANT, SCHEMA_SHA256, PIN_HEADER, pins)


def selected_name(root):
    return selected_directory(root).name

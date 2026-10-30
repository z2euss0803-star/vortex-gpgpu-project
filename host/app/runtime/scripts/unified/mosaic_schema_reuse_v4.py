"""Private Mosaic ABI4/schedule2/weight-layout2/WORK-layout1. No CODE publication or HW change.

The generic C schema bounds/relocates the packet; the target validator enforces
geometry, exact expanded extents and ALL writable/readonly interval exclusions.
Tile numbering is spatial-major nb-owned; weights are padded MB_KPAD_ROW8 layout2.
"""
import struct

def make_schema_reuse_v4():
    rules = []
    def scalar(offset, low, high, words=1):
        row = [0] * 16
        row[0], row[1], row[10], row[11], row[12] = 1, offset, words * 4, low, high
        rules.append(row)
    scalar(0, 4, 4)
    scalar(4, 128, 128)
    scalar(8, 1, 8)
    scalar(12, 0, 2)
    access = (1, 2, 3, 1, 1, 3, 3, 3)
    alignment = (4, 4, 2, 2, 4, 4, 4, 64)
    for role in range(8):
        row = [0] * 16
        row[:10] = [2, 16 + 4 * role, role, access[role], alignment[role], 1,
                    48 + 4 * role, 0xffffffff, 0xffffffff, 0xffffffff]
        rules.append(row)
    scalar(48, 1, 0x7f700000, 8)
    scalar(80, 1, 128, 2)
    scalar(88, 2, 960)
    scalar(92, 2, 1280)
    scalar(96, 3, 9)
    scalar(100, 1, 2, 2)
    scalar(108, 32, 2592)
    scalar(112, 0, 0xffffffff)
    scalar(116, 1, 4096)
    scalar(120, 0, 0x7f7fffff)
    scalar(124, 0, 1)
    header = [0x534c5856, 1, 64 + 64 * len(rules), 128, len(rules),
              0x80000000, 0x80110000, 0x10002000, 4096, 0xfff00000,
              0x100000, 1, 0, 0, 0, 0]
    return struct.pack('<16I', *header) + b''.join(struct.pack('<16I', *r) for r in rules)

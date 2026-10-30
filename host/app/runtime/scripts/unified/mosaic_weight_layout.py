"""HOST-only derived layout2. No files or original model bytes are modified."""
import hashlib
ORIGINAL_LAYOUT_ID=1
DERIVED_LAYOUT_ID=2
DERIVED_LAYOUT_NAME='MB_KPAD_ROW8_I16_LE_V1'
def repack_weights(source,co,kpad):
    """Exact [co,kpad] LE bytes -> [ceil(co/8),kpad,8] LE bytes.

    Offsets and counts are bytes relative to the supplied source segment and
    returned derived segment. All source Kpad bytes survive unchanged.
    The caller separately binds model/layer, source offset, epoch and new ABI.
    """
    if type(source) is not bytes:
        raise TypeError('immutable source segment bytes required')
    if type(co) is not int or type(kpad) is not int or not 1<=co<=128 or not 1<=kpad<=2592 or kpad%32 or len(source)!=co*kpad*2:
        raise ValueError('exact bounded CO_KPAD_I16_LE segment required')
    blocks=(co+7)//8
    derived=bytearray(blocks*kpad*16)
    for row in range(co):
        for k in range(kpad):
            src=(row*kpad+k)*2
            dst=(((row//8)*kpad+k)*8+row%8)*2
            derived[dst:dst+2]=source[src:src+2]
    rows=[]
    for mb in range(blocks):
        valid=min(8,co-mb*8)
        rows.append(dict(mb=mb,offset_bytes=mb*kpad*16,count_bytes=kpad*16,source_offset_bytes=mb*8*kpad*2,source_count_bytes=valid*kpad*2,valid_rows=valid))
    result=bytes(derived)
    return dict(original_layout_id=ORIGINAL_LAYOUT_ID,layout_id=DERIVED_LAYOUT_ID,layout=DERIVED_LAYOUT_NAME,co=co,kpad=kpad,derived_shape=[blocks,kpad,8],source_count_bytes=len(source),derived_count_bytes=len(result),source_sha256=hashlib.sha256(source).hexdigest(),derived_sha256=hashlib.sha256(result).hexdigest(),blocks=rows,derived_bytes=result)

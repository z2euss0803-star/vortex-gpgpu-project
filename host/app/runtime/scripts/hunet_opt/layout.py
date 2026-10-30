"""Frozen software-only scratch layout; no hardware validation claim."""
def gn_layout():
    return dict(schema=2,scope='STATIC_LAYOUT_NOT_GPU',gn=dict(start=0x86c4df40,bytes=4096,regions=[
        dict(name=name,offset=offset,bytes=size) for name,offset,size in (
            ('guard_left',0,64),('mean_partial',64,256),('variance_partial',320,256),
            ('counts',576,256),('scalars',832,64),('core_cycles',896,128),
            ('reserved_zero',1024,3008),('guard_right',4032,64))]))
def validate_layout(layout):
    expected=gn_layout()
    if layout.get('schema')!=2 or layout.get('gn')!=expected['gn']:
        raise ValueError('GN_SCRATCH_IDENTITY')
    if 'timing' in layout and layout['timing']!=timing_layout():raise ValueError('TIMING_PAGE_IDENTITY')
def timing_layout():
    return dict(start=0x84141000,bytes=4096,guard_bytes=64,schema=3,
                core_wall_fields=['pack','global_barrier'],
                warp_activity_fields=['wmma_loop','store'],
                scalar_error=dict(offset=416,cores=4,warps=4,lanes=4,words=64),
                aggregation='warps overlap; not exclusive core or job wall time')

"""Independent direct INT64 oracle and explicitly rounded FP32 operations."""
import numpy as np

def compensated_sum_f32(x):
    x=finite(x)
    if x.ndim!=2 or not x.shape[0] or not x.shape[1]:raise ValueError('nonempty C by spatial input required')
    count=1<<(x.shape[1]-1).bit_length()
    hi=np.zeros((x.shape[0],count),np.float32);hi[:,:x.shape[1]]=x
    lo=np.zeros_like(hi)
    def two_sum(a,b):
        s=np.add(a,b,dtype=np.float32)
        bp=np.subtract(s,a,dtype=np.float32)
        e=np.add(np.subtract(a,np.subtract(s,bp,dtype=np.float32),dtype=np.float32),np.subtract(b,bp,dtype=np.float32),dtype=np.float32)
        return s,e
    while hi.shape[1]>1:
        h,e=two_sum(hi[:,0::2],hi[:,1::2])
        l=np.add(np.add(lo[:,0::2],lo[:,1::2],dtype=np.float32),e,dtype=np.float32)
        hi,lo=two_sum(h,l)
    return finite(np.add(hi[:,0],lo[:,0],dtype=np.float32))


def global_norm_f32(x,gamma,beta):
    x=finite(x);gamma=finite(gamma);beta=finite(beta)
    if x.ndim!=2 or gamma.shape!=(x.shape[0],) or beta.shape!=gamma.shape:raise ValueError('norm shape')
    n=np.float32(x.shape[1])
    mean=np.divide(compensated_sum_f32(x),n,dtype=np.float32)
    centered=np.subtract(x,mean[:,None],dtype=np.float32)
    squares=np.multiply(centered,centered,dtype=np.float32)
    variance=np.divide(compensated_sum_f32(squares),n,dtype=np.float32)
    root=np.sqrt(np.add(variance,np.float32(1e-5),dtype=np.float32),dtype=np.float32)
    inv=np.divide(np.float32(1),root,dtype=np.float32)
    scaled=np.multiply(np.multiply(centered,inv[:,None],dtype=np.float32),gamma[:,None],dtype=np.float32)
    return finite(np.add(scaled,beta[:,None],dtype=np.float32))


def run_reference(input_array,model,on_stage=None):
    layers=model.get('layers');parameters=model.get('parameters')
    if not isinstance(layers,list) or len(layers)!=16 or not isinstance(parameters,list) or len(parameters)!=16:raise ValueError('complete16-layer graph required')
    y=finite(input_array);skip=None
    for index,(layer,params) in enumerate(zip(layers,parameters)):
        if y.ndim!=4 or y.shape[0]!=1 or y.shape[1]!=layer['ci']:raise ValueError('layer input shape')
        if layer['save']:skip=y.copy()
        previous=y;k=layer['k'];stride=layer['stride'];up=layer['upsample']
        if k not in (3,9) or stride not in (1,2) or up not in (1,2):raise ValueError('frozen convolution geometry')
        ci,h,w=y.shape[1:];inner=ci*k*k
        if inner>4096 or h*up<=k//2 or w*up<=k//2:raise ValueError('bounded reflection or K')
        sa=finite(params['sA'])[0]
        if sa<=0:raise ValueError('scale')
        inverse=np.divide(np.float32(1),sa,dtype=np.float32)
        q=quantize_rne(y,inverse)[0]
        oh=(h*up+stride-1)//stride;ow=(w*up+stride-1)//stride
        weights=np.asarray(params['q16'])
        if weights.dtype!=np.int16 or weights.shape[0]!=layer['co'] or weights.shape[1]<inner or np.any(weights==-32768):raise ValueError('frozen native weights')
        output=np.empty((layer['co'],oh*ow),np.float32)
        ks=np.arange(inner,dtype=np.int64);channels=ks//(k*k);ky=(ks//k)%k;kx=ks%k
        def reflect(v,length):
            period=2*(length-1);v=v%period
            return np.where(v<length,v,period-v)
        for start in range(0,oh*ow,64):
            positions=np.arange(start,min(start+64,oh*ow),dtype=np.int64)
            rows=(positions//ow)[:,None]*stride+ky[None,:]-k//2
            cols=(positions%ow)[:,None]*stride+kx[None,:]-k//2
            iy=reflect(rows,h*up)//up;ix=reflect(cols,w*up)//up
            patch=q[channels[None,:],iy,ix]
            acc=dot_i64(weights[:,:inner],patch)
            output[:,start:start+len(positions)]=restore_f32(acc,params['coeff'][:,0],sa,params['coeff'][:,1])
        if layer['norm']:output=global_norm_f32(output,params['norm_gamma'],params['norm_beta'])
        if layer['relu']:output=np.maximum(output,np.float32(0))
        y=output.reshape(1,layer['co'],oh,ow)
        if layer['residual']:
            if skip is None or skip.shape!=y.shape:raise ValueError('residual input')
            y=finite(np.add(y,skip,dtype=np.float32));skip=None
        if on_stage:on_stage(index,previous,y)
    return y


def finite(x):
    with np.errstate(over='ignore',invalid='ignore'):value=np.asarray(x,dtype=np.float32)
    if not np.isfinite(value).all():raise ValueError('nonfinite FP32')
    return value


def quantize_rne(x,inv_scale):
    x=finite(x);inverse=finite(inv_scale)
    if np.any(inverse<=0):raise ValueError('positive inverse scale required')
    with np.errstate(over='ignore',invalid='ignore'):
        scaled=np.multiply(x,inverse,dtype=np.float32)
    return np.clip(np.rint(scaled),-32767,32767).astype(np.int16)


def dot_i64(a,b):
    a=np.asarray(a);b=np.asarray(b)
    if (a.dtype!=np.int16 or b.dtype!=np.int16 or a.ndim!=2 or b.ndim!=2 or
        a.shape[1]!=b.shape[1] or not 1<=a.shape[1]<=4096 or not a.shape[0] or not b.shape[0]):
        raise ValueError('bounded I16 row matrices required')
    # Exact integer multiply and sum; independent of S8 packing/page decoding.
    return np.einsum('mk,nk->mn',a.astype(np.int64),b.astype(np.int64),dtype=np.int64,optimize=False)


def restore_f32(q,sw,sa,bias):
    q=np.asarray(q)
    if q.dtype!=np.int64 or q.ndim!=2 or np.any(q<-(1<<47)) or np.any(q>=(1<<47)):raise ValueError('signed48 I64 matrix required')
    sw=finite(sw);sa=finite(sa);bias=finite(bias)
    if sw.shape!=(q.shape[0],) or bias.shape!=sw.shape or sa.ndim!=0 or np.any(sw<=0) or sa<=0:raise ValueError('coefficient shape/scale')
    converted=q.astype(np.float32)
    first=np.multiply(converted,sw[:,None],dtype=np.float32)
    second=np.multiply(first,sa,dtype=np.float32)
    return finite(np.add(second,bias[:,None],dtype=np.float32))

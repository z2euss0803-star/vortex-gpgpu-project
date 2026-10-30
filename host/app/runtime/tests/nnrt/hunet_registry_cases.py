"""Independent metadata expectations from frozen old jobs, not new inference."""
import hashlib,json,pathlib,re,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
def main():
 ds=(ROOT/'generated/hunet_fused/descriptors.bin').read_bytes();jobs=list(struct.iter_unpack('<32I',(ROOT/'generated/hunet_fused/jobs.bin').read_bytes()));cases=[]
 old=[]
 for line in (ROOT/'generated/hunet/catalog.h').read_text().splitlines():
  if line.startswith('{0x'):old.append([int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)u',line)])
 assert len(old)==1023
 for j in jobs+[j for j in old if j[3]==6]:
  op=j[3];inputs=[];flags=0;hexdata='';workspace=0 if op==6 else 16
  def tensor(dtype,layout,shape,constant=False,content=None):
   n={'FP32':4,'FP16':2,'U8':1}[dtype]
   for d in shape:n*=d
   value=dict(id=len(inputs)+1,dtype=dtype,layout=layout,shape=shape,bytes=n,constant=constant,sha256=hashlib.sha256(content if content is not None else b'METADATA_FIXTURE_NOT_REAL_WEIGHTS').hexdigest());inputs.append(value)
  if op==15:
   ci,co,ih,iw,oh,ow,k,stride,pad,oldflags=j[18:28];flags=oldflags|(16 if j[10] else 0);shape=[ci,co,ih,iw,oh,ow,k,stride,pad,0,0,0];base=j[14]-0x86d0df80;raw=ds[base:base+j[7]*64];assert len(raw)==j[7]*64;d=struct.unpack_from('<16I',raw)
   tensor('FP32','CHW_F32',[ci,ih,iw]);tensor('FP16','RM_FP16_PAD8',[d[3],d[5]],True)
   if j[10]:tensor('FP32','ROW_F32',[d[0]],True)
   tensor('U8','DESC64_BYTES',[len(raw)],True,raw);hexdata=raw.hex();workspace=((j[13]+63)//64)*64+j[28]*4;out_shape=[co,oh,ow];layout='CHW_F32'
  else:
   ci,co,ih,iw,oh,ow,k=j[16:23];stride,pad,groups,n_div,eps=j[24:29];shape=[ci,co,ih,iw,oh,ow,k,stride,pad,groups,n_div,eps];tensor('FP32','CHW_F32',[ci,ih,iw]);out_shape=[co,oh,ow];layout='CHW_F32'
   if op==4:out_shape=[2];layout='GN_MEAN_INVSTD_F32'
   if op==5:
    tensor('FP32','ROW_F32',[ci],True);tensor('FP32','ROW_F32',[ci],True);tensor('FP32','GN_MEAN_INVSTD_F32',[2])
   if op==8:tensor('FP32','CHW_F32',[co,oh,ow])
   if op==11:tensor('FP32','ROW_F32',[co])
   if op==13:tensor('FP32','CHW_F32',[co-ci,ih,iw])
  cases.append(dict(op=op,inputs=inputs,parameters=dict(shape=shape,flags=flags,descriptors=hexdata),output_shape=out_shape,output_layout=layout,workspace=workspace))
 assert len(cases)==116 and sum(x['op']==6 for x in cases)==13
 p=pathlib.Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((json.dumps(cases,separators=(',',':'))+'\n').encode())
if __name__=='__main__':main()

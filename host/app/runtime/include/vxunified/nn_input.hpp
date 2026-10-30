#pragma once
#include <vxnn/request_input.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {
using NnInputRecord=vxnn::RequestInput;
// Only this id becomes a file name; RPC clients cannot choose a path/model.
inline bool nn_input_id(const std::string&s){return !s.empty()&&s.size()<=96&&s!="."&&s!=".."&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';});}
inline std::string nn_preprocessing_policy(bool mosaic){return mosaic?"MOSAIC_RGB_STRETCH_BILINEAR_CHW_F32_0_255_V1":"HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1";}
inline Result<std::shared_ptr<const std::vector<uint8_t>>>read_nn_sealed(int directory,const NnInputRecord&r,uint64_t bytes,bool source=false){
 using R=Result<std::shared_ptr<const std::vector<uint8_t>>>;
 if(directory<0||!nn_input_id(r.id)||!bytes||bytes>(source?(16u<<20):(64u<<20)))return R::failure({EINVAL,"NN_INPUT","bounded owned inbox/id/extent required"});
 struct stat ds{};if(fstat(directory,&ds)||!S_ISDIR(ds.st_mode)||ds.st_uid!=geteuid()||(ds.st_mode&07777)!=0700)return R::failure({EPERM,"NN_INPUT","inbox must remain owned0700 directory"});
 int fd=openat(directory,(r.id+(source?".source":".f32")).c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0)return R::failure({errno,"NN_INPUT","sealed tensor open failed"});
 struct Close{int fd;~Close(){close(fd);}}owned{fd};struct stat before{},after{};
 auto sealed=[](const struct stat&s){return S_ISREG(s.st_mode)&&s.st_uid==geteuid()&&(s.st_mode&07777)==0400&&s.st_nlink==1;};
 if(fstat(fd,&before)||!sealed(before)||before.st_size<0||(!source&&uint64_t(before.st_size)!=bytes)||(source&&(!before.st_size||uint64_t(before.st_size)>bytes)))return R::failure({EPERM,"NN_INPUT","tensor must be owned0400 single-link regular exact extent"});
 try{auto data=std::make_shared<std::vector<uint8_t>>(size_t(before.st_size));size_t offset=0;while(offset<data->size()){ssize_t n=read(fd,data->data()+offset,data->size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)return R::failure({EIO,"NN_INPUT","short/interrupted tensor read"});offset+=size_t(n);}uint8_t extra;
 if(read(fd,&extra,1)!=0||fstat(fd,&after)||!sealed(after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)return R::failure({ESTALE,"NN_INPUT","tensor changed during bounded read"});
 Digest actual{};vxl_hunet_hash(data->data(),data->size(),actual.data());if(actual!=(source?r.source_sha:r.tensor_sha))return R::failure({EBADMSG,"NN_INPUT","tensor SHA differs upload record"});if(source){bool png=data->size()>=8&&std::memcmp(data->data(),"\x89PNG\r\n\x1a\n",8)==0;bool jpeg=data->size()>=3&&(*data)[0]==0xff&&(*data)[1]==0xd8&&(*data)[2]==0xff;bool bmp=data->size()>=2&&(*data)[0]=='B'&&(*data)[1]=='M';if(!((r.source_format=="PNG"&&png)||(r.source_format=="JPEG"&&jpeg)||(r.source_format=="BMP"&&bmp)))return R::failure({EINVAL,"NN_INPUT","source signature differs supported declared format"});}return R::success(std::move(data));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NN_INPUT","bounded tensor allocation"});}
}
inline Result<std::shared_ptr<const std::vector<uint8_t>>>read_nn_input(int directory,const NnInputRecord&r,uint64_t bytes){return read_nn_sealed(directory,r,bytes);}
}

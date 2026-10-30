#pragma once
#include <vxunified/service.hpp>
#include <cerrno>
namespace vxunified {
// Additive private result artifacts have fixed roles and share the producer's
// own request directory. Legacy single-output results retain their old path.
inline Result<void>nn_result_artifact_layout(const PublishedResult&p){
 using R=Result<void>;const auto&a=p.lease.artifacts;
 if(a.empty()||(a.size()!=1&&a.size()!=3&&a.size()!=4)||!p.binding)return R::failure({EINVAL,"NN_RESULT","one output with optional exact metadata/input roles required"});
 if(a.size()==1)return R::success();
 if(a[0].layout!="CHW_F32"||a[0].shape.size()!=3||a[0].shape[0]!=3||!a[0].shape[1]||!a[0].shape[2]||a[0].shape[1]>1080||a[0].shape[2]>1920||a[0].bytes!=12*a[0].shape[1]*a[0].shape[2])return R::failure({EINVAL,"NN_RESULT","native Mosaic final tensor geometry differs"});
 auto slash=a[0].path.find('/');if(slash==std::string::npos||a[0].path.find('/',slash+1)!=std::string::npos||a[0].path.substr(slash+1)!="output.f32")return R::failure({EINVAL,"NN_RESULT","original native output path required"});auto prefix=a[0].path.substr(0,slash+1);
 for(size_t i=1;i<3;i++)if(a[i].path!=prefix+(i==1?"input-record.json":"plan-inventory.json")||a[i].layout!="JSON_UTF8"||!a[i].bytes||a[i].bytes>(128u<<10)||a[i].shape!=vxnn::Shape{a[i].bytes}||!nonzero_digest(a[i].sha))return R::failure({EINVAL,"NN_RESULT","exact same-request metadata artifact shape/identity required"});
 if(a.size()==4&&(a[3].path!=prefix+"request-input.f32"||a[3].layout!="CHW_F32"||a[3].bytes!=p.binding->input_bytes||a[3].sha!=p.binding->input||a[3].shape!=a[0].shape))return R::failure({EINVAL,"NN_RESULT","same-request actual processed input tensor differs binding"});
 return R::success();
}
}

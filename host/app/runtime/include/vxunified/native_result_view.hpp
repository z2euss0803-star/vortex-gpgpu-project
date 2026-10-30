#pragma once
#include <vxunified/native_files.hpp>
#include <vxunified/display.hpp>
namespace vxunified {
#define VXUNIFIED_HUNET_PAGE_VIEW_V1 1
// One trusted installed preset catalog, bound to its admitted model/base plan.
struct NativeHunetPagePreset {std::string directory;Digest manifest{},model{},base_plan{};};
// Trusted installer binding, never a peer supplied model/display source.
struct NativeHunetViewSpec {std::string directory;Digest manifest{};std::vector<Digest>models;std::vector<NativeHunetPagePreset>page_presets;};
class NativeResultView {
 struct State;std::shared_ptr<State>state_;explicit NativeResultView(std::shared_ptr<State>);
public:
 static Result<std::shared_ptr<NativeResultView>>create(const StartupContext&,std::shared_ptr<NativeFiles>,std::optional<NativeHunetViewSpec> spec={});
 Result<DisplayLayout>layout(const PublishedResult&,uint32_t width,uint32_t height)const;
 Result<std::shared_ptr<const DisplayCanvas>>compose(const PublishedResult&,const DisplayLayout&)const;
};
}

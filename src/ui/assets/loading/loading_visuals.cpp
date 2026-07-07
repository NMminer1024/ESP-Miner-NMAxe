#include "ui/assets/loading/loading_visuals.h"

#include "ui/assets/loading/background_240x135.h"

namespace nm::ui::assets {

namespace {

LoadingVisualSpec make_loading_visuals(
    const lv_img_dsc_t* background,
    const char* headline,
    const char* fallback_pool) {
    LoadingVisualSpec spec;
    spec.background = background;
    spec.headline = headline;
    spec.fallback_pool = fallback_pool;
    return spec;
}

}  // namespace

const LoadingVisualSpec& loading_visuals_240x135() {
    static const LoadingVisualSpec spec =
        make_loading_visuals(&loading_background_240x135(), "Make it better", "");
    return spec;
}

}  // namespace nm::ui::assets

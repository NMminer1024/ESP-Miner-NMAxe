#pragma once

#include <lvgl.h>

namespace nm::ui::assets {

struct LoadingVisualSpec {
    const lv_img_dsc_t* background = nullptr;
    const char* headline = "Make it better";
    const char* fallback_pool = "";
};

const LoadingVisualSpec& loading_visuals_240x135();

}  // namespace nm::ui::assets

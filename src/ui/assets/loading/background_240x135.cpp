#include "ui/assets/loading/background_240x135.h"

#include "ui/assets/loading/background_240x135_data.h"

namespace nm::ui::assets {

const lv_img_dsc_t& loading_background_240x135() {
    static const lv_img_dsc_t descriptor = [] {
        lv_img_dsc_t value{};
        value.header.cf = LV_IMG_CF_TRUE_COLOR;
        value.header.w = 240;
        value.header.h = 135;
        value.data_size = static_cast<uint32_t>(240u * 135u * LV_COLOR_SIZE / 8u);
        value.data = reinterpret_cast<const uint8_t*>(kLoadingBackground240x135Rgb565);
        return value;
    }();

    return descriptor;
}

}  // namespace nm::ui::assets

#include "ui/assets/loading/background_320x240.h"

#include "ui/assets/loading/background_320x240_data.h"

namespace nm::ui::assets {

const lv_img_dsc_t& loading_background_320x240() {
    static const lv_img_dsc_t descriptor = [] {
        lv_img_dsc_t value{};
        value.header.cf = LV_IMG_CF_TRUE_COLOR;
        value.header.w = 320;
        value.header.h = 240;
        value.data_size = static_cast<uint32_t>(320u * 240u * LV_COLOR_SIZE / 8u);
        value.data = reinterpret_cast<const uint8_t*>(kLoadingBackground320x240Rgb565);
        return value;
    }();

    return descriptor;
}

}  // namespace nm::ui::assets

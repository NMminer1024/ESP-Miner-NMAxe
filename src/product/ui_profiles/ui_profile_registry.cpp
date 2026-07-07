#include "product/ui_profiles/ui_profile_registry.h"

namespace nm::product {

const UiProfile& active_ui_profile(const bsp::Board& board) {
    (void)board;

    static UiProfile gamma_placeholder;
    static bool initialized = false;
    if (!initialized) {
        gamma_placeholder.layout_id = UiLayoutId::Layout320x240;
        gamma_placeholder.variant_id = UiVariantId::Default;
        gamma_placeholder.input_mode = bsp::UiInputMode::ButtonOnly;
        gamma_placeholder.profile_name = "nmaxegamma-placeholder";
        initialized = true;
    }
    return gamma_placeholder;
}

}  // namespace nm::product

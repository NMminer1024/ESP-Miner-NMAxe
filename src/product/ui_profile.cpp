// What: Compile-time oriented UI profile resolver for the active product build.
// Why: The framework needs one place to translate a selected BSP into a concrete
// UI layout and variant before pages are constructed.
// Role: Returns the active `UiProfile` consumed by `ui_root` during boot.
// Benefit: UI selection logic stays centralized, so adding new boards or UI
// variants does not require sprinkling conditionals throughout page code.
#include "product/ui_profile.h"

namespace nm::product {

const UiProfile& active_ui_profile(const bsp::Board& board) {
    (void)board;

    // Temporary single-board profile binding.
    // TODO(agent): replace this with compile-time board/layout registration once
    // multiple BSPs and real page variants are introduced.
    static UiProfile gamma_profile;
    static bool initialized = false;
    if (!initialized) {
        gamma_profile.layout_id = UiLayoutId::Layout240x135;
        gamma_profile.variant_id = UiVariantId::Default;
        gamma_profile.input_mode = bsp::UiInputMode::ButtonOnly;
        gamma_profile.profile_name = "nmaxe_gamma";
        initialized = true;
    }
    return gamma_profile;
}

}  // namespace nm::product

// What: Compile-time UI profile resolver for the active firmware target.
// Why: The build already selects exactly one BOARD_* BSP, so UI product binding
// should stay compile-time instead of branching on runtime strings.
// Role: Returns the single `UiProfile` for the current firmware image.
// Benefit: Product-specific UI routing stays type-safe, local, and easy to
// extend without scattering board-name comparisons through the UI layer.
#include "product/ui_profile.h"

namespace nm::product {

namespace {

UiProfile make_profile(
    UiLayoutId layout_id,
    UiVariantId variant_id,
    UiProductId product_id,
    bsp::UiInputMode input_mode,
    const char* profile_name) {
    UiProfile profile;
    profile.layout_id = layout_id;
    profile.variant_id = variant_id;
    profile.product_id = product_id;
    profile.input_mode = input_mode;
    profile.profile_name = profile_name;
    return profile;
}

}  // namespace

const UiProfile& active_ui_profile() {
#if defined(BOARD_NMAXE)
    static const UiProfile profile = make_profile(
        UiLayoutId::Layout240x135,
        UiVariantId::Default,
        UiProductId::NMAxe,
        bsp::UiInputMode::ButtonOnly,
        "nmaxe");
    return profile;
#elif defined(BOARD_NMAXE_GAMMA)
    static const UiProfile profile = make_profile(
        UiLayoutId::Layout240x135,
        UiVariantId::Default,
        UiProductId::NMAxeGamma,
        bsp::UiInputMode::ButtonOnly,
        "nmaxe_gamma");
    return profile;
#elif defined(BOARD_NMQAXE_PP)
    static const UiProfile profile = make_profile(
        UiLayoutId::Layout320x240,
        UiVariantId::Default,
        UiProductId::NMQAxePP,
        bsp::UiInputMode::ButtonOnly,
        "nmqaxe_pp");
    return profile;
#elif defined(BOARD_NMQAXE_PP_REV61)
    static const UiProfile profile = make_profile(
        UiLayoutId::Layout320x240,
        UiVariantId::Default,
        UiProductId::NMQAxePPRev61,
        bsp::UiInputMode::ButtonOnly,
        "nmqaxe_pp_rev61");
    return profile;
#elif defined(BOARD_NMQAXE_PP_REV81)
    static const UiProfile profile = make_profile(
        UiLayoutId::Layout320x240,
        UiVariantId::Default,
        UiProductId::NMQAxePPRev81,
        bsp::UiInputMode::ButtonOnly,
        "nmqaxe_pp_rev81");
    return profile;
#else
    #error "No UI profile selected. Define one BOARD_* macro in platformio.ini."
#endif
}

}  // namespace nm::product

// What: UI boot coordinator and temporary root-page builder.
// Why: After BSP init, the framework needs one place to bind hardware-backed UI
// ports, resolve the active product profile, and build the first screen.
// Role: Bridges board context into LVGL-facing UI composition.
// Benefit: Concentrates UI bring-up logic in one layer instead of coupling page
// creation directly to application startup or BSP implementations.
#include "ui/ui_root.h"

#include <Arduino.h>
#include <lvgl.h>

#include "product/ui_profile.h"
#include "ui/port/ports.h"

namespace nm::ui {

// Temporary BSP-first bring-up page.
// TODO(agent): replace this with real layout/variant dispatch once the gamma UI tree is introduced.
static void build_placeholder_page(const bsp::Board& board) {
    lv_obj_t* screen = lv_scr_act();
    if (screen == nullptr) {
        return;
    }

    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(screen);
    if (title == nullptr) {
        return;
    }
    lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(title, board.traits().board_name);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t* subtitle = lv_label_create(screen);
    if (subtitle == nullptr) {
        return;
    }
    lv_obj_set_style_text_color(subtitle, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, LV_PART_MAIN);

    char subtitle_text[48] = {};
    snprintf(
        subtitle_text,
        sizeof(subtitle_text),
        "UI placeholder %ux%u",
        board.display_profile().width,
        board.display_profile().height);
    lv_label_set_text(subtitle, subtitle_text);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 14);
}


void boot(const bsp::Board& board) {
    const auto& profile = product::active_ui_profile(board);

    bool display_ready = false;
    if (board.drivers().display != nullptr) {
        display_ready = port::bind_display(*board.drivers().display);
    }
    port::bind_input(board.drivers().touch);

    Serial.printf("[ui] profile=%s layout=%u variant=%u input=%u\n",
                  profile.profile_name,
                  static_cast<unsigned>(profile.layout_id),
                  static_cast<unsigned>(profile.variant_id),
                  static_cast<unsigned>(profile.input_mode));

    if (display_ready) {
        build_placeholder_page(board);
        if (lv_disp_get_default() != nullptr) {
            lv_refr_now(lv_disp_get_default());
        }
    }
}

void poll() {
    port::poll();
}

}  // namespace nm::ui

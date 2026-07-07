// What: UI boot coordinator and temporary root-page builder.
// Why: After BSP init, the framework needs one place to bind hardware-backed UI
// ports, resolve the active product profile, and build the first screen.
// Role: Bridges board context into LVGL-facing UI composition.
// Benefit: Concentrates UI bring-up logic in one layer instead of coupling page
// creation directly to application startup or BSP implementations.
#include "ui/ui_root.h"

#include <Arduino.h>
#include <stdio.h>
#include <lvgl.h>

#include "product/ui_profile.h"
#include "ui/port/ports.h"

namespace nm::ui {

namespace {

struct RootView {
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* line1 = nullptr;
    lv_obj_t* line2 = nullptr;
    lv_obj_t* line3 = nullptr;
    lv_obj_t* line4 = nullptr;
    lv_obj_t* line5 = nullptr;
    lv_obj_t* footer = nullptr;
    bool ready = false;
};

RootView g_root;

lv_obj_t* create_text_line(lv_obj_t* parent, const lv_font_t* font, lv_align_t align, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* label = lv_label_create(parent);
    if (label == nullptr) {
        return nullptr;
    }
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 236);
    lv_obj_align(label, align, x, y);
    return label;
}

void build_root_page() {
    lv_obj_t* screen = lv_scr_act();
    if (screen == nullptr) {
        return;
    }

    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    g_root.title = create_text_line(screen, &lv_font_montserrat_20, LV_ALIGN_TOP_MID, 0, 8);
    g_root.subtitle = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 34);
    g_root.line1 = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 4, 56);
    g_root.line2 = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 4, 74);
    g_root.line3 = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 4, 92);
    g_root.line4 = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 4, 110);
    g_root.line5 = create_text_line(screen, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 4, 128);
    g_root.footer = create_text_line(screen, &lv_font_montserrat_12, LV_ALIGN_BOTTOM_MID, 0, -4);
    g_root.ready =
        g_root.title != nullptr &&
        g_root.subtitle != nullptr &&
        g_root.line1 != nullptr &&
        g_root.line2 != nullptr &&
        g_root.line3 != nullptr &&
        g_root.line4 != nullptr &&
        g_root.line5 != nullptr &&
        g_root.footer != nullptr;
}

void render_summary_page(const bsp::Board& board, const state::RuntimeState& runtime) {
    char line[96] = {};

    lv_label_set_text(g_root.subtitle, "Summary");

    snprintf(line, sizeof(line), "BOOT %s", runtime.boot.message);
    lv_label_set_text(g_root.line1, line);

    snprintf(
        line,
        sizeof(line),
        "VBUS %.2fV  IBUS %.2fA",
        runtime.power.vbus_mv / 1000.0f,
        runtime.power.ibus_ma / 1000.0f);
    lv_label_set_text(g_root.line2, line);

    snprintf(
        line,
        sizeof(line),
        "VCORE %lumV  %s",
        static_cast<unsigned long>(runtime.power.vcore_mv),
        runtime.power.vcore_ready ? "READY" : "WAIT");
    lv_label_set_text(g_root.line3, line);

    snprintf(
        line,
        sizeof(line),
        "TEMP VRM %.1fC ASIC %.1fC",
        runtime.thermal.vcore_c,
        runtime.thermal.asic_c);
    lv_label_set_text(g_root.line4, line);

    if (runtime.fan_count > 0) {
        snprintf(
            line,
            sizeof(line),
            "FAN %urpm @ %u%%",
            static_cast<unsigned>(runtime.fans[0].rpm),
            static_cast<unsigned>(runtime.fans[0].speed_percent));
    } else {
        snprintf(line, sizeof(line), "FAN none");
    }
    lv_label_set_text(g_root.line5, line);

    snprintf(
        line,
        sizeof(line),
        "%s  %ux%u",
        board.display_profile().display_name,
        static_cast<unsigned>(board.display_profile().width),
        static_cast<unsigned>(board.display_profile().height));
    lv_label_set_text(g_root.footer, line);
}

void render_detail_page(const bsp::Board& board, const state::RuntimeState& runtime) {
    char line[96] = {};

    lv_label_set_text(g_root.subtitle, "Detail");

    snprintf(
        line,
        sizeof(line),
        "ASIC %u x %u @ %uMHz",
        static_cast<unsigned>(board.mining_profile().asic_family),
        static_cast<unsigned>(board.mining_profile().asic_count),
        static_cast<unsigned>(board.mining_profile().default_freq_mhz));
    lv_label_set_text(g_root.line1, line);

    snprintf(
        line,
        sizeof(line),
        "PWR adc:%s dc:%s vcore:%s",
        runtime.power.adc_ready ? "yes" : "no",
        runtime.power.dc_plugged ? "yes" : "no",
        runtime.power.vcore_ready ? "yes" : "no");
    lv_label_set_text(g_root.line2, line);

    snprintf(
        line,
        sizeof(line),
        "BTN0 %lu/%lu/%lu",
        static_cast<unsigned long>(runtime.buttons[0].click_count),
        static_cast<unsigned long>(runtime.buttons[0].double_click_count),
        static_cast<unsigned long>(runtime.buttons[0].long_press_count));
    lv_label_set_text(g_root.line3, line);

    snprintf(
        line,
        sizeof(line),
        "BTN1 %lu/%lu/%lu",
        static_cast<unsigned long>(runtime.buttons[1].click_count),
        static_cast<unsigned long>(runtime.buttons[1].double_click_count),
        static_cast<unsigned long>(runtime.buttons[1].long_press_count));
    lv_label_set_text(g_root.line4, line);

    snprintf(
        line,
        sizeof(line),
        "REV %s  BTN %u FAN %u",
        board.traits().board_revision,
        static_cast<unsigned>(board.traits().button_count),
        static_cast<unsigned>(board.traits().fan_count));
    lv_label_set_text(g_root.line5, line);

    lv_label_set_text(g_root.footer, "boot click next, double prev");
}

}  // namespace


bool boot(const bsp::Board& board, const state::RuntimeState&, const state::UiState& ui_state) {
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

    if (!display_ready) {
        return false;
    }

    build_root_page();
    if (!g_root.ready) {
        return false;
    }

    render(board, state::RuntimeState{}, ui_state);
    if (lv_disp_get_default() != nullptr) {
        lv_refr_now(lv_disp_get_default());
    }
    return true;
}

void render(const bsp::Board& board, const state::RuntimeState& runtime, const state::UiState& ui_state) {
    if (!g_root.ready) {
        return;
    }

    lv_label_set_text(g_root.title, board.traits().board_name);

    if (ui_state.current_page == state::UiPageId::Summary) {
        render_summary_page(board, runtime);
    } else {
        render_detail_page(board, runtime);
    }
}

void poll() {
    port::poll();
}

}  // namespace nm::ui

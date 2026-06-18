#include "page_config.h"

#include "../../../version.h"
#include "app/application.h"
#include "ui/assets/images.h"

extern "C" {
    int qrcodegen_getMinFitVersion(int ecl, size_t dataLen);
}

void PageConfig240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    _parent = parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageConfig240x135::_create_dynamic(lv_obj_t* parent) {
    static constexpr const char* kLegacyApPassword = "12345678";

    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &config_page_img_135_240);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_move_background(bg);

    auto& app = MinerApp::instance();
    _lb_logo = lv_img_create(parent);
    lv_img_set_src(_lb_logo,
        app.spec().name == BOARD_NMAXE_GAMMA_NAME ? &logo_worker_nmaxegamma : &logo_worker_nmaxe);
    lv_obj_align(_lb_logo, LV_ALIGN_TOP_LEFT, 35, 0);
    lv_obj_add_flag(_lb_logo, LV_OBJ_FLAG_EVENT_BUBBLE);

    _lb_version = lv_label_create(parent);
    lv_obj_set_width(_lb_version, 120);
    lv_label_set_text(_lb_version, BOARD_CURRENT_FW_VERSION);
    lv_obj_set_style_text_font(_lb_version, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_version, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_version, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_version, LV_ALIGN_TOP_MID, 70, 0);

    _lb_timeout = lv_label_create(parent);
    lv_label_set_text(_lb_timeout, "");
    lv_obj_set_style_text_font(_lb_timeout, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_timeout, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_timeout, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lb_timeout, _W);
    lv_obj_align(_lb_timeout, LV_ALIGN_BOTTOM_MID, 175, 0);

    String qr_str = "WIFI:T:WPA;S:" + app.wifi_cfg().ap_ssid + ";P:" + String(kLegacyApPassword) + ";H:false;";
    lv_coord_t qr_size = _H - 32;
    const int min_margin_px = 4;
    int32_t version = qrcodegen_getMinFitVersion(1, qr_str.length());
    if (version > 0) {
        int32_t modules = 4 * version + 17;
        int32_t scale = qr_size / modules;
        if (scale > 0) {
            int32_t margin = (qr_size - modules * scale) / 2;
            if (margin < min_margin_px) {
                qr_size = modules * scale + 2 * min_margin_px;
            }
        }
    }

    _qr = lv_qrcode_create(parent, qr_size, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    lv_qrcode_update(_qr, (const uint8_t*)qr_str.c_str(), qr_str.length());
    lv_obj_align(_qr, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(_qr, LV_OBJ_FLAG_EVENT_BUBBLE);

    _lb_config = lv_label_create(parent);
    lv_obj_set_width(_lb_config, _W / 2);
    lv_label_set_text(_lb_config, (app.wifi_cfg().ap_ssid + "\r\n" + app.wifi_cfg().ap_ip.toString()).c_str());
    lv_obj_set_style_text_font(_lb_config, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_config, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_config, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(_lb_config, 0, LV_PART_MAIN);
    lv_obj_align(_lb_config, LV_ALIGN_LEFT_MID, 10, 40);
}

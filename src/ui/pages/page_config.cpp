#include "ui/pages/page_config.h"

#include "../../version.h"
#include "app/application.h"
#include "app/system_events.h"
#include "nvs/nvs_config.h"
#include "net/wifi_ctx.h"
#include "ui/assets/fonts.h"
#include "utils/logger/logger.h"
#include "utils/reboot_log/reboot_log.h"

extern "C" {
    int qrcodegen_getMinFitVersion(int ecl, size_t dataLen);
}

static void s_set_label_text(lv_obj_t* obj, const String& text) {
    if (obj) {
        lv_label_set_text(obj, text.c_str());
    }
}

void PageConfigBase::destroy() {
    _parent = nullptr;
    _lb_version = nullptr;
    _lb_timeout = nullptr;
    _lb_logo = nullptr;
}

void PageConfigQrBase::destroy() {
    PageConfigBase::destroy();
    _lb_config = nullptr;
    _qr = nullptr;
    _last_timeout_ms = 0;
    _timeout_anim = 0;
}

void PageConfigQrBase::_on_update() {
    auto& app = MinerApp::instance();
    WifiState* wifi = app.wifi();
    if (!wifi) return;

    if (_lb_timeout) {
        uint32_t now = millis();
        if (now - _last_timeout_ms >= 1000) {
            _last_timeout_ms = now;
            static const char* anim[4] = {"config   ", "config.  ", "config.. ", "config..."};
            if (wifi->client_connected) {
                lv_obj_align(_lb_timeout, LV_ALIGN_BOTTOM_MID, 165, 0);
                lv_label_set_text(_lb_timeout, anim[_timeout_anim++ % 4]);
            } else {
                lv_obj_align(_lb_timeout, LV_ALIGN_BOTTOM_MID, 175, 0);
                lv_label_set_text_fmt(_lb_timeout, "%us", (unsigned)wifi->config_timeout);
            }
        }
    }
}

void PageConfigWifiListBase::destroy() {
    _destroy_phase_widgets();
    PageConfigBase::destroy();
    _phase = Phase::LIST;
    _last_phase = (Phase)0xFF;
    _scan_status = -2;
    _pending_ssid = "";
    _pending_pwd = "";
    _last_timeout_ms = 0;
}

void PageConfigWifiListBase::_destroy_phase_widgets() {
    _clear_key_popups();
    if (_ws_kbd) {
        lv_obj_del(_ws_kbd);
        _ws_kbd = nullptr;
    }
    if (_ws_cont) {
        lv_obj_del(_ws_cont);
        _ws_cont = nullptr;
    }
    _ws_ssid_list = nullptr;
    _ws_scan_lbl = nullptr;
    _ws_pwd_ta = nullptr;
}

void PageConfigWifiListBase::_clear_key_popups() {
    for (auto& popup : _ws_key_popup) {
        if (popup) {
            lv_obj_del(popup);
            popup = nullptr;
        }
    }
}

void PageConfigWifiListBase::_show_key_popups(const char* txt, lv_coord_t touch_x, lv_coord_t touch_y) {
    _clear_key_popups();
    const lv_coord_t pw = 28;
    const lv_coord_t ph = 28;
    const lv_coord_t gap = 40;
    struct Pos { lv_coord_t x; lv_coord_t y; };
    Pos pos[4] = {
        {(lv_coord_t)(touch_x - pw / 2),     (lv_coord_t)(touch_y - ph - gap)},
        {(lv_coord_t)(touch_x - pw / 2),     (lv_coord_t)(touch_y + gap)},
        {(lv_coord_t)(touch_x - pw - gap),   (lv_coord_t)(touch_y - ph / 2)},
        {(lv_coord_t)(touch_x + gap),        (lv_coord_t)(touch_y - ph / 2)},
    };

    for (int i = 0; i < 4; ++i) {
        lv_coord_t x = pos[i].x;
        lv_coord_t y = pos[i].y;
        if (x < 0) x = 0;
        if (x + pw > _W) x = _W - pw;
        if (y < 0) y = 0;
        if (y + ph > _H) y = _H - ph;

        lv_obj_t* bubble = lv_obj_create(lv_layer_top());
        lv_obj_set_size(bubble, pw, ph);
        lv_obj_set_pos(bubble, x, y);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x00BFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bubble, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(bubble, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(bubble, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(bubble, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(bubble, lv_color_hex(0x0050A0), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(bubble, LV_OPA_70, LV_PART_MAIN);
        lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(bubble);
        lv_label_set_text(lbl, txt);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_size(lbl, pw, ph);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        _ws_key_popup[i] = bubble;
    }
}

void PageConfigWifiListBase::_build_list_phase() {
    _ws_cont = lv_obj_create(_parent);
    lv_obj_set_size(_ws_cont, _W, _H - 22);
    lv_obj_align(_ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_ws_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_ws_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_ws_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* title_bar = lv_obj_create(_ws_cont);
    lv_obj_set_size(title_bar, _W, 26);
    lv_obj_align(title_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(title_bar, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* lb_title = lv_label_create(title_bar);
    lv_label_set_text(lb_title, "WiFi Setup");
    lv_obj_set_style_text_font(lb_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(lb_title, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(lb_title, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* btn_refresh = lv_btn_create(title_bar);
    lv_obj_set_size(btn_refresh, 32, 24);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_refresh, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_refresh, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_refresh, 2, LV_PART_MAIN);
    lv_obj_t* lb_refresh_icon = lv_label_create(btn_refresh);
    lv_label_set_text(lb_refresh_icon, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(lb_refresh_icon, lv_color_hex(0x00BFFF), LV_PART_MAIN);
    lv_obj_center(lb_refresh_icon);
    lv_obj_add_event_cb(btn_refresh, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        WiFi.scanDelete();
        WiFi.mode(WIFI_AP_STA);
        WiFi.scanNetworks(true, false);
        self->_scan_status = -1;
        if (self->_ws_ssid_list) lv_obj_add_flag(self->_ws_ssid_list, LV_OBJ_FLAG_HIDDEN);
        if (self->_ws_scan_lbl) {
            lv_obj_clear_flag(self->_ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(self->_ws_scan_lbl, "Scanning...");
        }
    }, LV_EVENT_CLICKED, this);

    _ws_scan_lbl = lv_label_create(_ws_cont);
    lv_label_set_text(_ws_scan_lbl, "Scanning...");
    lv_obj_set_style_text_font(_ws_scan_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_ws_scan_lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_align(_ws_scan_lbl, LV_ALIGN_CENTER, 0, 0);

    _ws_ssid_list = lv_obj_create(_ws_cont);
    lv_obj_set_size(_ws_ssid_list, _W, _H - 22 - 26);
    lv_obj_align(_ws_ssid_list, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_set_style_bg_color(_ws_ssid_list, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_ssid_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_ssid_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_ws_ssid_list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_ws_ssid_list, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(_ws_ssid_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(_ws_ssid_list, LV_OBJ_FLAG_HIDDEN);

    WiFi.scanDelete();
    WiFi.mode(WIFI_AP_STA);
    WiFi.scanNetworks(true, false);
    _scan_status = -1;
}

void PageConfigWifiListBase::_build_pwd_input_phase() {
    const lv_coord_t usable_h = _H - 22;
    const lv_coord_t pad = 6;
    const lv_coord_t inner_w = _W - pad * 2;
    const lv_coord_t btn_h = 36;
    const lv_coord_t btn_w = inner_w / 2 - 2;
    const lv_coord_t ssid_h = 18;
    const lv_coord_t ta_h = 32;
    const lv_coord_t top_h = pad + ssid_h + 4 + ta_h + 4 + btn_h + pad;
    const lv_coord_t kbd_h = usable_h - top_h;

    _ws_cont = lv_obj_create(_parent);
    lv_obj_set_size(_ws_cont, _W, top_h);
    lv_obj_align(_ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_ws_cont, pad, LV_PART_MAIN);
    lv_obj_clear_flag(_ws_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lb_ssid = lv_label_create(_ws_cont);
    lv_label_set_text_fmt(lb_ssid, LV_SYMBOL_WIFI "  %s", _pending_ssid.c_str());
    lv_obj_set_style_text_font(lb_ssid, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ssid, lv_color_hex(0x00BFFF), LV_PART_MAIN);
    lv_obj_set_width(lb_ssid, inner_w);
    lv_label_set_long_mode(lb_ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(lb_ssid, LV_ALIGN_TOP_MID, 0, 0);

    _ws_pwd_ta = lv_textarea_create(_ws_cont);
    lv_obj_set_size(_ws_pwd_ta, inner_w, ta_h);
    lv_obj_align(_ws_pwd_ta, LV_ALIGN_TOP_LEFT, 0, ssid_h + 4);
    lv_textarea_set_one_line(_ws_pwd_ta, true);
    lv_textarea_set_password_mode(_ws_pwd_ta, true);
    lv_textarea_set_max_length(_ws_pwd_ta, 64);
    lv_textarea_set_placeholder_text(_ws_pwd_ta, "Enter password...");
    lv_textarea_set_text(_ws_pwd_ta, "");
    lv_obj_set_style_text_font(_ws_pwd_ta, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_ws_pwd_ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_ws_pwd_ta, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_pwd_ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(_ws_pwd_ta, lv_color_hex(0x00BFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_pwd_ta, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_ws_pwd_ta, 30, LV_PART_MAIN);
    lv_obj_clear_flag(_ws_pwd_ta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btn_eye = lv_btn_create(_ws_cont);
    lv_obj_set_size(btn_eye, 28, ta_h - 4);
    lv_obj_align(btn_eye, LV_ALIGN_TOP_RIGHT, -2, ssid_h + 6);
    lv_obj_set_style_bg_opa(btn_eye, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_eye, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_eye, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_eye, 0, LV_PART_MAIN);
    lv_obj_t* lb_eye = lv_label_create(btn_eye);
    lv_label_set_text(lb_eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(lb_eye, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_eye, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_center(lb_eye);
    lv_obj_add_event_cb(btn_eye, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self || !self->_ws_pwd_ta) return;
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* icon = lv_obj_get_child(btn, 0);
        bool pw_mode = lv_textarea_get_password_mode(self->_ws_pwd_ta);
        lv_textarea_set_password_mode(self->_ws_pwd_ta, !pw_mode);
        if (icon) {
            lv_label_set_text(icon, pw_mode ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
            lv_obj_set_style_text_color(icon, pw_mode ? lv_color_hex(0x00BFFF) : lv_color_hex(0x888888), LV_PART_MAIN);
        }
    }, LV_EVENT_CLICKED, this);

    const lv_coord_t btn_y = ssid_h + 4 + ta_h + 4;

    lv_obj_t* btn_cancel = lv_btn_create(_ws_cont);
    lv_obj_set_size(btn_cancel, btn_w, btn_h);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 0, btn_y);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_t* lb_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lb_cancel, LV_SYMBOL_LEFT "  Cancel");
    lv_obj_set_style_text_font(lb_cancel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lb_cancel);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        self->_pending_ssid = "";
        self->_pending_pwd = "";
        self->_phase = Phase::LIST;
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* btn_save = lv_btn_create(_ws_cont);
    lv_obj_set_size(btn_save, btn_w, btn_h);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, 0, btn_y);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x007ACC), LV_PART_MAIN);
    lv_obj_t* lb_save = lv_label_create(btn_save);
    lv_label_set_text(lb_save, LV_SYMBOL_OK "  Save");
    lv_obj_set_style_text_font(lb_save, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lb_save);
    lv_obj_add_event_cb(btn_save, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        if (self->_ws_pwd_ta) self->_pending_pwd = String(lv_textarea_get_text(self->_ws_pwd_ta));
        self->_phase = Phase::CONFIRM;
    }, LV_EVENT_CLICKED, this);

    _ws_kbd = lv_keyboard_create(_parent);
    lv_obj_set_size(_ws_kbd, _W, kbd_h);
    lv_obj_align(_ws_kbd, LV_ALIGN_TOP_LEFT, 0, top_h);
    lv_keyboard_set_textarea(_ws_kbd, _ws_pwd_ta);
    lv_obj_set_style_bg_color(_ws_kbd, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_text_font(_ws_kbd, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_event_cb(_ws_kbd, [](lv_event_t* e) {
        lv_obj_t* kbd = lv_event_get_target(e);
        lv_btnmatrix_t* bm = (lv_btnmatrix_t*)kbd;
        uint16_t sel = bm->btn_id_sel;
        if (sel != LV_BTNMATRIX_BTN_NONE) {
            const char* txt = lv_btnmatrix_get_btn_text(kbd, sel);
            if (txt && strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) return;
        }
        lv_event_stop_processing(e);
    }, (lv_event_code_t)(LV_EVENT_LONG_PRESSED_REPEAT | LV_EVENT_PREPROCESS), nullptr);

    lv_obj_add_event_cb(_ws_pwd_ta, [](lv_event_t* e) {
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        if (self->_ws_pwd_ta) self->_pending_pwd = String(lv_textarea_get_text(self->_ws_pwd_ta));
        self->_phase = Phase::CONFIRM;
    }, LV_EVENT_READY, this);

    lv_obj_add_event_cb(_ws_kbd, [](lv_event_t* e) {
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* kbd = lv_event_get_target(e);
        if (code == LV_EVENT_PRESSED) {
            uint16_t btn_id = lv_btnmatrix_get_selected_btn(kbd);
            if (btn_id == LV_BTNMATRIX_BTN_NONE) return;
            const char* txt = lv_btnmatrix_get_btn_text(kbd, btn_id);
            if (!txt || strlen(txt) != 1 || (uint8_t)txt[0] < 0x21) return;
            lv_indev_t* indev = lv_event_get_indev(e);
            lv_point_t tp = {0, 0};
            if (indev) lv_indev_get_point(indev, &tp);
            self->_show_key_popups(txt, tp.x, tp.y);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            self->_clear_key_popups();
        }
    }, LV_EVENT_ALL, this);
}

void PageConfigWifiListBase::_build_confirm_phase() {
    _ws_cont = lv_obj_create(_parent);
    lv_obj_set_size(_ws_cont, _W, _H - 22);
    lv_obj_align(_ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_ws_cont, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_cont, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_ws_cont, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t dlg_w = _W - 24;
    const lv_coord_t dlg_h = 150;
    lv_obj_t* dlg = lv_obj_create(_ws_cont);
    lv_obj_set_size(dlg, dlg_w, dlg_h);
    lv_obj_align(dlg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dlg, lv_color_hex(0x007ACC), LV_PART_MAIN);
    lv_obj_set_style_border_width(dlg, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dlg, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dlg, 10, LV_PART_MAIN);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lb_msg = lv_label_create(dlg);
    lv_label_set_text_fmt(lb_msg, "Connect to \"%s\"?\nDevice will reboot.", _pending_ssid.c_str());
    lv_obj_set_style_text_font(lb_msg, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_msg, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(lb_msg, dlg_w - 20);
    lv_label_set_long_mode(lb_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(lb_msg, LV_ALIGN_TOP_MID, 0, 0);

    const lv_coord_t btn_w = (dlg_w - 20 - 8) / 2;
    const lv_coord_t btn_h = 36;

    lv_obj_t* btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, btn_w, btn_h);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_t* lb_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lb_cancel, "Cancel");
    lv_obj_set_style_text_font(lb_cancel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lb_cancel);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        self->_phase = Phase::PWD_INPUT;
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* btn_confirm = lv_btn_create(dlg);
    lv_obj_set_size(btn_confirm, btn_w, btn_h);
    lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x00AA55), LV_PART_MAIN);
    lv_obj_t* lb_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(lb_confirm, LV_SYMBOL_OK "  Confirm");
    lv_obj_set_style_text_font(lb_confirm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lb_confirm);
    lv_obj_add_event_cb(btn_confirm, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
        if (!self) return;
        self->_phase = Phase::SAVING;
    }, LV_EVENT_CLICKED, this);
}

void PageConfigWifiListBase::_build_saving_phase() {
    nvs_config_set_string(NVS_CONFIG_WIFI_SSID, _pending_ssid.c_str());
    nvs_config_set_string(NVS_CONFIG_WIFI_PASS, _pending_pwd.c_str());
    LOG_I("WiFi credentials saved: SSID=%s", _pending_ssid.c_str());

    _ws_cont = lv_obj_create(_parent);
    lv_obj_set_size(_ws_cont, _W, _H - 22);
    lv_obj_align(_ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_ws_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_ws_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_ws_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lb_ok = lv_label_create(_ws_cont);
    lv_label_set_text(lb_ok, "Saved!\nRebooting...");
    lv_obj_set_style_text_font(lb_ok, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ok, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_ok, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(lb_ok);

    auto& app = MinerApp::instance();
    reboot_intent_set(REBOOT_INTENT_LCD_WIFI_SAVED, "saved SSID=%s via on-screen wizard", _pending_ssid.c_str());
    if (app.reboot_xsem()) {
        xSemaphoreGive(app.reboot_xsem());
    }
}

void PageConfigWifiListBase::_build_phase() {
    _destroy_phase_widgets();
    _last_phase = _phase;

    switch (_phase) {
        case Phase::LIST: _build_list_phase(); break;
        case Phase::PWD_INPUT: _build_pwd_input_phase(); break;
        case Phase::CONFIRM: _build_confirm_phase(); break;
        case Phase::SAVING: _build_saving_phase(); break;
    }
}

void PageConfigWifiListBase::_poll_scan_results() {
    if (_phase != Phase::LIST || _scan_status != -1) return;

    int16_t n = (int16_t)WiFi.scanComplete();
    if (n < 0) return;

    _scan_status = n;
    if (_ws_scan_lbl) lv_obj_add_flag(_ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
    if (_ws_ssid_list) {
        lv_obj_clear_flag(_ws_ssid_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(_ws_ssid_list);
    }

    for (int16_t i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || !_ws_ssid_list) continue;
        int32_t rssi = WiFi.RSSI(i);

        lv_obj_t* btn = lv_btn_create(_ws_ssid_list);
        lv_obj_set_size(btn, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 4, LV_PART_MAIN);

        lv_obj_t* icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0x00BFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* ssid_lbl = lv_label_create(btn);
        lv_label_set_text(ssid_lbl, ssid.c_str());
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid_lbl, _W - 22 - 60);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 22, 0);

        lv_obj_t* rssi_lbl = lv_label_create(btn);
        lv_label_set_text_fmt(rssi_lbl, "%ddBm", (int)rssi);
        lv_obj_set_style_text_color(rssi_lbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_align(rssi_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<PageConfigWifiListBase*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_obj_t* b = lv_event_get_target(e);
            lv_obj_t* lbl = lv_obj_get_child(b, 1);
            if (lbl) self->_pending_ssid = String(lv_label_get_text(lbl));
            self->_phase = Phase::PWD_INPUT;
        }, LV_EVENT_CLICKED, this);
    }

    if (n == 0 && _ws_scan_lbl) {
        lv_obj_clear_flag(_ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_ws_scan_lbl, "No networks found");
    }
    WiFi.scanDelete();
}

void PageConfigWifiListBase::_on_update() {
    auto& app = MinerApp::instance();
    WifiState* wifi = app.wifi();
    if (!wifi) return;

    uint32_t now = millis();
    if (now - _last_timeout_ms >= 1000) {
        _last_timeout_ms = now;
        uint32_t inactive_ms = lv_disp_get_inactive_time(nullptr);
        if (inactive_ms < 1500) {
            wifi->config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
        } else if (wifi->config_timeout > 0) {
            wifi->config_timeout--;
        }
        if (_lb_timeout) {
            lv_label_set_text_fmt(_lb_timeout, "%ds", wifi->config_timeout);
        }
    }

    if (_phase != _last_phase) {
        _build_phase();
    }
    _poll_scan_results();
}

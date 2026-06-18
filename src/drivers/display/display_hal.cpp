#include "display_hal.h"
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <FS.h>
#include "../../utils/logger/logger.h"
#include "../touch/ft6206.h"

// ── Panel driver + logical resolution (post-rotation) ───────────────────────
static TFT_eSPI* tftDriver    = nullptr;
static uint16_t  SCREEN_WIDTH  = 0;
static uint16_t  SCREEN_HEIGHT = 0;

// Backlight needs the spec for the board-specific PWM mapping; cached at init.
static BoardSpecConfig* s_spec = nullptr;

// ── Touch state ─────────────────────────────────────────────────────────────
static FT6206Class*     s_touch      = nullptr;
static PreferenceState* s_touch_pref = nullptr;

uint16_t tft_screen_width()  { return SCREEN_WIDTH; }
uint16_t tft_screen_height() { return SCREEN_HEIGHT; }

// LVGL flush: push the rendered rectangle to the panel over SPI.
static void tft_flush_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tftDriver->startWrite();
    tftDriver->setAddrWindow(area->x1, area->y1, w, h);
    tftDriver->pushColors((uint16_t*)&color_p->full, w * h, true);
    tftDriver->endWrite();

    lv_disp_flush_ready(disp_drv);
}

void tft_bl_ctrl(int8_t percent, BoardSpecConfig* spec) {
    if (!spec) spec = s_spec;
    if (!spec) return;
    uint8_t pwm = 0;
    if ((spec->name == BOARD_NMAXE_GAMMA_NAME) || (spec->name == BOARD_NMAXE_NAME)) {
        pwm = 255 * (1 - percent * 0.01f);
    } else if (spec->name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
        pwm = percent * 2.55;
    } else {
        pwm = 128; // default mid brightness
    }
    LOG_D("Set brightness %d%%, PWM=%d", percent, pwm);
    ledcWrite(spec->tft.bl.pwm_ch, pwm);
}

void tft_init(BoardSpecConfig* spec, PreferenceState* pref) {
    s_spec = spec;
    SCREEN_WIDTH  = spec->tft.height;
    SCREEN_HEIGHT = spec->tft.width;

    // Power on TFT
    if (spec->tft.pwr_pin >= 0) {
        pinMode(spec->tft.pwr_pin, OUTPUT);
        digitalWrite(spec->tft.pwr_pin, LOW);
    }
    // Initialize backlight PWM
    if (spec->tft.bl.pin >= 0) {
        pinMode(spec->tft.bl.pin, OUTPUT);
        ledcSetup(spec->tft.bl.pwm_ch, spec->tft.bl.pwm_freq, spec->tft.bl.pwm_resolution);
        ledcAttachPin(spec->tft.bl.pin, spec->tft.bl.pwm_ch);
        tft_bl_ctrl(0, spec); // sleep when boot up
    }

    tftDriver = new TFT_eSPI(SCREEN_HEIGHT, SCREEN_WIDTH);
    if (!tftDriver) {
        LOG_E("Failed to create TFT_eSPI instance!");
        return;
    }
    LOG_D("Screen size: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);

    tftDriver->begin(spec->spi.cs_pin,
                     spec->tft.dc_pin,
                     spec->tft.rst_pin,
                     spec->spi.sclk_pin,
                     spec->spi.miso_pin,
                     spec->spi.mosi_pin,
                     spec->tft.color_invert);

    if (pref && pref->screen.flip) tftDriver->setRotation(1);
    else tftDriver->setRotation(spec->name == BOARD_NMQAXE_PLUS_PLUS_NAME ? 4 : 3);
}

void ui_drv_register(uint16_t hor_res, uint16_t ver_res) {
    LOG_I("lvgl version: %s",
          (String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch()).c_str());

    static lv_disp_draw_buf_t lvgl_draw_buf;
    static lv_disp_drv_t      disp_drv;
    static lv_color_t*        color_buf = nullptr;
    const size_t color_buf_size = (size_t)hor_res * ver_res;

    if (color_buf == nullptr) {
        size_t buffer_bytes = color_buf_size * sizeof(lv_color_t);
        size_t psram_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (psram_free > buffer_bytes * 2) { // leave safety margin
            color_buf = (lv_color_t*)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM);
            if (color_buf) LOG_I("LVGL display buffer in PSRAM: %d bytes at 0x%p", buffer_bytes, color_buf);
        }
        if (color_buf == nullptr) {
            color_buf = (lv_color_t*)heap_caps_malloc(buffer_bytes, MALLOC_CAP_INTERNAL);
            if (color_buf) LOG_W("LVGL display buffer in internal RAM: %d bytes", buffer_bytes);
            else { LOG_E("Failed to allocate LVGL display buffer!"); return; }
        }
    }

    lv_disp_draw_buf_init(&lvgl_draw_buf, color_buf, nullptr, color_buf_size);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = hor_res;
    disp_drv.ver_res  = ver_res;
    disp_drv.flush_cb = tft_flush_cb;
    disp_drv.draw_buf = &lvgl_draw_buf;
    lv_disp_drv_register(&disp_drv);
}

// LVGL touch read: debounced FT6206 sampling + flip-aware coord mapping.
// (Ported from legacy touchpad_read_cb.)
static void touchpad_read_cb(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
    (void)indev_drv;
    static const uint32_t TOUCH_DEBOUNCE_MS = 50;
    static uint32_t touch_start_ms = 0;

    if (s_touch && s_touch->touched()) {
        if (touch_start_ms == 0) touch_start_ms = millis();
        if ((millis() - touch_start_ms) >= TOUCH_DEBOUNCE_MS) {
            TS_Point raw_p = s_touch->getPoint();
            bool flip = s_touch_pref && s_touch_pref->screen.flip;
            data->point.x = flip ? raw_p.y                 : SCREEN_WIDTH  - raw_p.y;
            data->point.y = flip ? SCREEN_HEIGHT - raw_p.x : raw_p.x;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED; // too short, ignore
        }
    } else {
        touch_start_ms = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool touch_drv_register(PreferenceState* pref, uint8_t threshold) {
    s_touch_pref = pref;
    if (!s_touch) s_touch = new FT6206Class();
    if (!s_touch || !s_touch->begin(threshold)) {
        LOG_W("No touch controller detected, disabling touch support.");
        if (s_touch) { delete s_touch; s_touch = nullptr; }
        return false;
    }
    LOG_I("FT6206 touch controller initialized.");

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);
    return true;
}

// SPIFFS-backed LVGL filesystem driver (letter 'S'); used by lv_gif to stream
// the screensaver GIF from "/screen_saver_*.gif". Ported from legacy display.cpp.
void lvgl_fs_spiffs_register() {
    static lv_fs_drv_t spiffs_drv;
    lv_fs_drv_init(&spiffs_drv);
    spiffs_drv.letter = 'S';
    spiffs_drv.open_cb = +[](lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) -> void* {
        String fpath = String("/") + path;
        const char* flag = (mode & LV_FS_MODE_WR) ? "w" : "r";
        fs::File* f = new fs::File(SPIFFS.open(fpath.c_str(), flag));
        if (!f || !(*f)) { delete f; return nullptr; }
        return (void*)f;
    };
    spiffs_drv.close_cb = +[](lv_fs_drv_t*, void* fp) -> lv_fs_res_t {
        fs::File* f = (fs::File*)fp; f->close(); delete f; return LV_FS_RES_OK;
    };
    spiffs_drv.read_cb = +[](lv_fs_drv_t*, void* fp, void* buf, uint32_t btr, uint32_t* br) -> lv_fs_res_t {
        fs::File* f = (fs::File*)fp; *br = f->read((uint8_t*)buf, btr); return LV_FS_RES_OK;
    };
    spiffs_drv.seek_cb = +[](lv_fs_drv_t*, void* fp, uint32_t pos, lv_fs_whence_t whence) -> lv_fs_res_t {
        fs::File* f = (fs::File*)fp;
        fs::SeekMode sm = (whence == LV_FS_SEEK_CUR) ? fs::SeekCur
                        : (whence == LV_FS_SEEK_END) ? fs::SeekEnd : fs::SeekSet;
        f->seek(pos, sm); return LV_FS_RES_OK;
    };
    spiffs_drv.tell_cb = +[](lv_fs_drv_t*, void* fp, uint32_t* pos_p) -> lv_fs_res_t {
        fs::File* f = (fs::File*)fp; *pos_p = f->position(); return LV_FS_RES_OK;
    };
    lv_fs_drv_register(&spiffs_drv);
    LOG_I("LVGL SPIFFS FS driver registered (letter='S')");
}

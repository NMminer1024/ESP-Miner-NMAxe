#include "display.h"
#include "utils/logger/logger.h"
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include "global.h"
#include "utils/helper.h" 
#include "image/image.h"
#include "nvs/nvs_config.h"
#include <nvs.h>
#include "board/board.h"
#include "utils/reboot_log/reboot_log.h"

// Forward-declare qrcodegen C API (compiled as part of lvgl, no header modify needed)
extern "C" {
    int qrcodegen_getMinFitVersion(int ecl, size_t dataLen);
}

/******************************************************************* global state variables ******************************************************************/
static uint16_t SCREEN_WIDTH  = 0;
static uint16_t SCREEN_HEIGHT = 0;
static TFT_eSPI *tftDriver = nullptr;
static lv_obj_t *parent_wall = nullptr;

LV_FONT_DECLARE(ds_digib_font_16)
LV_FONT_DECLARE(ds_digib_font_18)
LV_FONT_DECLARE(ds_digib_font_20)
LV_FONT_DECLARE(ds_digib_font_24)
LV_FONT_DECLARE(ds_digib_font_28)
LV_FONT_DECLARE(ds_digib_font_32)
LV_FONT_DECLARE(ds_digib_font_36)
LV_FONT_DECLARE(ds_digib_font_38)
LV_FONT_DECLARE(ds_digib_font_52)
LV_FONT_DECLARE(ds_digib_font_56)
LV_FONT_DECLARE(ds_digib_font_120)
LV_FONT_DECLARE(Inconsolata_14)
LV_FONT_DECLARE(Inconsolata_16)
LV_FONT_DECLARE(Inconsolata_18)
LV_FONT_DECLARE(Inconsolata_22)
LV_FONT_DECLARE(Inconsolata_26)
LV_FONT_DECLARE(symbol_14)
LV_FONT_DECLARE(symbol_20)
/********************************************************************* global UI elements ********************************************************************/
// loading page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  ui_element_t   lb_progress;
  ui_element_t   bar_progress;
  ui_element_t   lb_version;
  ui_element_t   lb_details;
  ui_element_t   lb_ip_and_slogan;
  ui_element_t   lb_pool_url;
}loading_page;

// config page elements
struct{
  lv_obj_t      *container;

  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;

  ui_element_t  img_logo;
  lv_img_dsc_t  *logo_img_dsc;

  ui_element_t  lb_config_txt;
  ui_element_t  lb_version;
  ui_element_t  lb_cfg_timeout;

  ui_element_t  qr_code;
}config_page;

// miner page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  lv_img_dsc_t  *logo_img_dsc;
  ui_element_t  img_logo;
  ui_element_t  lb_share, lb_fan, lb_hr_unit, lb_uptime_day_unit;
  ui_element_t  lb_hashrate, lb_blk_hit, lb_temp ,lb_power,lb_ip ,lb_uptime_day, lb_uptime_hms, lb_diff;
  ui_element_t  lb_uptime_symbol ,lb_wifi_symbol ,lb_diff_symbol ,lb_share_symb ,lb_temp_symb ,lb_fan_symb;
  ui_element_t  lb_price, lb_ver;
  ui_element_t  lb_swarm_best_diff, lb_swarm_workers, lb_swarm_total_hashrate;
  ui_element_t  lb_utc_time;
}miner_page;

// dashboard page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  ui_element_t  lb_hr;
  ui_element_t  lb_hr_unit;
  lv_img_dsc_t  *miner_img_dsc;
  ui_element_t  img_miner;
  ui_element_t  lb_diff;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_oc;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_pwr;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_vc_real;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_vc_req;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_asic_temp;
  struct{
    ui_ring_obj_t    obj;
    ui_ring_config_t cfg;
  }ring_vcore_temp;
}dashboard_page;

// hashrate health page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  ui_element_t  lb_hr;
  ui_element_t  lb_hr_unit;
  ui_element_t  lb_scale;
  struct{
    lv_coord_t      center_x;                             
    lv_coord_t      center_y;  
    lv_coord_t      radius;                              
    ui_pie_chart_t  obj;
    pie_sector_t    cfg[PIE_CHART_MAX_SECTORS];
  }asic_hr_chart;
  struct{
    lv_chart_series_t *series;
    lv_obj_t          *obj;
    const lv_font_t   *font;
    struct {
        lv_coord_t x, y;
    } coord;
  }total_hr_chart;
}hr_health_page;

// clock page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  ui_element_t  lb_hr;
  ui_element_t  lb_hr_unit;
  ui_element_t  lb_hits;
  ui_element_t  lb_hits_unit;
  ui_element_t  lb_date;
  ui_element_t  lb_time;
  ui_element_t  lb_ampm;
  ui_element_t  lb_price;
}clock_page;

// Market page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
  // Coin row label vectors (created once in ui_market_page_update !inited block)
  std::vector<lv_obj_t*> lb_names;
  std::vector<lv_obj_t*> lb_dollar_signs;  // fixed "$" per row, left-aligned
  std::vector<lv_obj_t*> lb_prices;        // price number, right-aligned
  std::vector<lv_obj_t*> lb_changes;
  // Bottom hashrate display
  ui_element_t  lb_hr;       // .font set in ui_page_element_init
  ui_element_t  lb_hr_unit;  // .font set in ui_page_element_init
  // Bottom-left firmware version label (.font / .coord set in ui_page_element_init)
  ui_element_t  lb_version;
  // Per-board column layout (set in ui_page_element_init)
  const lv_font_t *coin_font;    // font for coin row text
  lv_coord_t      col_dollar_x;  // X start of "$" column
  lv_coord_t      col_price_x;   // X start of price number column (right-aligned)
  lv_coord_t      col_change_x;  // X start of change% column
}market_page;

// setting page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;

  ui_element_t bar_brightness;
  ui_element_t lb_brightness;
  ui_element_t kb_input;
  ui_element_t btn_save;
  ui_element_t btn_restart;
  ui_element_t list_asic_vcore;
  ui_element_t list_asic_freq;
  ui_element_t list_saver;
  // ui_element_t checkbox_led_on;
  ui_element_t checkbox_auto_rolling;
  ui_element_t checkbox_screen_flip;
  // swarm page labels (NMAXE / NMAxeGamma only)
  ui_element_t lb_swarm_workers;
  ui_element_t lb_swarm_total_hr;
  ui_element_t lb_swarm_best_sess_bd;
  ui_element_t lb_swarm_best_ever_bd;
}setting_page;

// Timestamp set by tileview_changed_cb when the setting page is entered;
// ui_setting_or_swarm_page_update compares against it to reload NVS values on each entry.
static uint32_t s_setting_page_entered_ms = 0;


/*********************************** some helper func *******************************************/
static ui_ring_obj_t ui_draw_ring(lv_obj_t* parent, const ui_ring_config_t* config) {
    ui_ring_obj_t ring_obj = {NULL, NULL, NULL};
    
    if(!parent || !config) {
        LOG_E("Invalid parameters for ui_draw_ring");
        return ring_obj;
    }

    // Create arc
    ring_obj.arc = lv_arc_create(parent);
    lv_obj_set_size(ring_obj.arc, 2 * config->radius, 2 * config->radius);
    lv_arc_set_bg_angles(ring_obj.arc, 0, config->angle_full);
    lv_arc_set_angles(ring_obj.arc, config->angle_start, config->angle_end);
    lv_obj_remove_style(ring_obj.arc, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(ring_obj.arc, 90 + (360 - config->angle_full) / 2);
    lv_obj_set_style_arc_width(ring_obj.arc, config->line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_obj.arc, config->line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring_obj.arc, config->bg_color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring_obj.arc, config->indicator_color, LV_PART_INDICATOR);
    lv_obj_set_pos(ring_obj.arc, config->x, config->y);
    lv_obj_clear_flag(ring_obj.arc, LV_OBJ_FLAG_CLICKABLE); // make arc non-interactive, let events pass through to parent container
    // Create center text label
    if(config->center_text && config->center_font) {
        ring_obj.label_center = lv_label_create(ring_obj.arc);
        lv_label_set_text(ring_obj.label_center, config->center_text);
        lv_obj_set_style_text_font(ring_obj.label_center, config->center_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(ring_obj.label_center, config->center_text_color, LV_PART_MAIN);
        lv_obj_set_style_text_align(ring_obj.label_center, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(ring_obj.label_center, LV_LABEL_LONG_CLIP);
        lv_obj_center(ring_obj.label_center);
    } else {
        ring_obj.label_center = NULL;
    }

    // Create title label (position adapts based on arc radius, line width, and font height)
    if(config->title_text && config->title_font) {
        ring_obj.label_title = lv_label_create(parent);
        lv_obj_set_width(ring_obj.label_title, 2 * config->radius);
        lv_label_set_text(ring_obj.label_title, config->title_text);
        lv_obj_set_style_text_font(ring_obj.label_title, config->title_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(ring_obj.label_title, config->title_text_color, LV_PART_MAIN);
        lv_obj_set_style_text_align(ring_obj.label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(ring_obj.label_title, LV_LABEL_LONG_DOT);
        // Get font height
        lv_coord_t font_height = lv_font_get_line_height(config->title_font);
        // Title Y = center_Y + radius + line_width + font_height/2 (aligns text baseline to bottom of arc)
        lv_obj_align(ring_obj.label_title, LV_ALIGN_TOP_LEFT, 
                    config->x,  
                    config->y + config->radius + config->line_width + font_height / 2);
    } else {
        ring_obj.label_title = NULL;
    }
    
    return ring_obj;
}

static void ui_update_ring(ui_ring_obj_t* ring_obj, uint16_t angle, String center_text) {
    if(!ring_obj || !ring_obj->arc) return;
    
    lv_arc_set_angles(ring_obj->arc, 0, angle);
    
    if(ring_obj->label_center && center_text.length() > 0) {
        lv_label_set_text(ring_obj->label_center, center_text.c_str());
    }
}

static ui_pie_chart_t ui_draw_pie_chart(lv_obj_t* parent, lv_coord_t center_x, lv_coord_t center_y, lv_coord_t radius, const pie_sector_t* sectors, uint8_t sector_count) {
    ui_pie_chart_t pie_chart = {0};
    
    if (!parent || !sectors || sector_count == 0 || sector_count > PIE_CHART_MAX_SECTORS) {
        LOG_E("Invalid parameters for ui_draw_pie_chart");
        return pie_chart;
    }

    // Store configuration
    pie_chart.sector_count = sector_count;
    pie_chart.center_x = center_x;
    pie_chart.center_y = center_y;
    pie_chart.radius = radius;
    // Calculate starting angle (from top, clockwise)
    uint16_t current_angle = 0;
    
    // Draw each sector as a thick arc (like the ring implementation)
    for (uint8_t i = 0; i < sector_count; i++) {
        uint16_t end_angle = current_angle + sectors[i].angle;
        
        // Create arc object for this sector
        pie_chart.arcs[i] = lv_arc_create(parent);
        lv_obj_t* arc = pie_chart.arcs[i];
        lv_obj_set_size(arc, radius * 2, radius * 2);
        lv_obj_set_pos(arc, center_x - radius, center_y - radius);
        
        // Set angles for this sector
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, current_angle, end_angle);
        
        // Remove knob (make it display only)
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        
        // Rotate to start from top (12 o'clock position)
        lv_arc_set_rotation(arc, 270);
        
        // Hide background arc (make it transparent)
        lv_obj_set_style_arc_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        
        // Set indicator (sector) to fill from center to edge with 70% opacity
        lv_obj_set_style_arc_width(arc, radius, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, sectors[i].color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_50, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR); // Flat end caps for perfect pie chart
        
        // Draw sector label if provided
        if (sectors[i].label != NULL && strlen(sectors[i].label) > 0) {
            // Calculate label position (midpoint of sector angle)
            float mid_angle = current_angle + sectors[i].angle / 2.0f;
            float label_radius = radius * 0.6f;
            
            // Convert to radians (accounting for rotation)
            float angle_rad = (mid_angle - 90.0f) * M_PI / 180.0f;
            
            lv_coord_t label_x = center_x + (lv_coord_t)(label_radius * cos(angle_rad));
            lv_coord_t label_y = center_y + (lv_coord_t)(label_radius * sin(angle_rad));
            
            pie_chart.labels[i] = lv_label_create(parent);
            lv_label_set_text(pie_chart.labels[i], sectors[i].label);
            lv_obj_set_style_text_color(pie_chart.labels[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(pie_chart.labels[i], &lv_font_montserrat_12, 0);
            lv_obj_align(pie_chart.labels[i], LV_ALIGN_TOP_LEFT, label_x - 10, label_y - 10);
        } else {
            pie_chart.labels[i] = NULL;
        }
        current_angle = end_angle;
    }
    return pie_chart;
}

static void ui_update_pie_chart(ui_pie_chart_t* pie_chart, const uint16_t* angles) {
    if (!pie_chart || !angles || pie_chart->sector_count == 0) {
        LOG_E("Invalid parameters for ui_update_pie_chart");
        return;
    }
    
    uint16_t current_angle = 0;
    
    // Update each sector's angles
    for (uint8_t i = 0; i < pie_chart->sector_count; i++) {
        if (pie_chart->arcs[i]) {
            uint16_t end_angle = current_angle + angles[i];
            lv_arc_set_angles(pie_chart->arcs[i], current_angle, end_angle);
            
            // Update label position if label exists
            if (pie_chart->labels[i]) {
                float mid_angle = current_angle + angles[i] / 2.0f;
                float label_radius = pie_chart->radius * 0.6f;
                float angle_rad = (mid_angle - 90.0f) * M_PI / 180.0f;

                lv_coord_t label_x = pie_chart->center_x + (lv_coord_t)(label_radius * cos(angle_rad));
                lv_coord_t label_y = pie_chart->center_y + (lv_coord_t)(label_radius * sin(angle_rad));

                // Update label text with percentage (without decimal)
                uint8_t percentage = (uint8_t)((angles[i] * 100) / 360);
                String label_text = "A" + String(i + 1) + "\r" + String(percentage) + "%";
                lv_label_set_text(pie_chart->labels[i], label_text.c_str());
                lv_obj_align(pie_chart->labels[i], LV_ALIGN_TOP_LEFT, label_x - 10, label_y - 10);
            }
            
            current_angle = end_angle;
        }
    }
}

/*********************************** some callback func *******************************************/
static void keyboard_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *keyboard = lv_event_get_target(e);
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {  
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);  
    }
}

static void slider_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED &&
        lv_event_get_target(e) == setting_page.bar_brightness.obj) {
        g_board.status.preference.screen.brightness = lv_slider_get_value(lv_event_get_target(e));
    }
}

static void checkbox_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *checkbox = lv_event_get_target(e);
        if(checkbox == setting_page.checkbox_auto_rolling.obj) {
            g_board.status.preference.led.enable = lv_obj_has_state(checkbox, LV_STATE_CHECKED) ? true : false;
        }
        else if(checkbox == setting_page.checkbox_screen_flip.obj) {
            // take effect after reboot, save to NVS when user click "Save" button
        }
    }
}

static void msgbox_restart_cb(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t  btn  = lv_msgbox_get_active_btn(mbox);
    if(btn == 0) { // "Yes"
        LOG_W("Restart confirmed.");
        reboot_intent_set(REBOOT_INTENT_LCD_USER_RESTART,
                          "user confirmed restart on LCD setting page");
        xSemaphoreGive(g_board.status.reboot_xsem);
    }
    lv_msgbox_close(mbox);
}

static void show_toast(const char *msg, uint32_t duration_ms); // forward declaration

static void do_save_settings() {
    // brightness
    uint8_t brightness = (uint8_t)lv_slider_get_value(setting_page.bar_brightness.obj);
    nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, brightness);
    // auto rolling
    bool auto_roll = lv_obj_has_state(setting_page.checkbox_auto_rolling.obj, LV_STATE_CHECKED);
    g_board.status.preference.screen.auto_rolling = auto_roll;
    nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, auto_roll ? 1 : 0);
    // screen flip
    bool flip = lv_obj_has_state(setting_page.checkbox_screen_flip.obj, LV_STATE_CHECKED);
    nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, flip ? 1 : 0);
    // overclock
    if (setting_page.list_asic_freq.obj) {
        uint16_t idx = lv_dropdown_get_selected(setting_page.list_asic_freq.obj);
        uint16_t frq = g_board.info.spec.ui.setting_page.oc[idx].value;
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, frq);
    } else {
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, g_board.info.spec.asic.default_frq);
    }
    // vcore
    if (setting_page.list_asic_vcore.obj) {
        uint16_t idx   = lv_dropdown_get_selected(setting_page.list_asic_vcore.obj);
        uint16_t vcore = g_board.info.spec.ui.setting_page.vc[idx].value;
        g_board.info.spec.asic.req_vcore = vcore;
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, vcore);
    } else {
        g_board.info.spec.asic.req_vcore = g_board.info.spec.asic.default_vcore;
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, g_board.info.spec.asic.default_vcore);
    }
    // screen saver
    if (setting_page.list_saver.obj) {
        static const uint32_t SAVER_VALS[] = {0, 30, 60, 300, 900, 1800, 3600, 7200, 21600};
        uint16_t idx = lv_dropdown_get_selected(setting_page.list_saver.obj);
        uint32_t tmo = (idx < 9) ? SAVER_VALS[idx] : 0;
        uint8_t  en  = (tmo > 0) ? 1 : 0;
        nvs_config_set_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, en);
        nvs_config_set_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, tmo);
        g_board.status.preference.screen.saver_enable  = en;
        g_board.status.preference.screen.saver_timeout = tmo;
        g_board.status.ui.last_active_ms = millis();
    }
    show_toast("Settings Saved!", 2000);
}

static void msgbox_save_cb(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t  btn  = lv_msgbox_get_active_btn(mbox);
    if(btn == 0) { // "OK"
        do_save_settings();
    }
    lv_msgbox_close(mbox);
}

static void enable_label_recolor_recursive(lv_obj_t *obj) {
    if (!obj) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_label_set_recolor(obj, true);
    }
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        enable_label_recolor_recursive(lv_obj_get_child(obj, i));
    }
}

static void textarea_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *textarea   = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {  
      lv_obj_clear_flag(setting_page.kb_input.obj, LV_OBJ_FLAG_HIDDEN);  
      lv_keyboard_set_textarea(setting_page.kb_input.obj, textarea);    
    }
}

static void toast_timer_cb(lv_timer_t *t) {
    lv_obj_t *toast = (lv_obj_t *)t->user_data;
    if(toast) lv_obj_del(toast);
    lv_timer_del(t);
}

static void show_toast(const char *msg, uint32_t duration_ms = 2000) {
    lv_obj_t *toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x009900), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_pad_all(toast, 6, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(toast);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(toast, LV_ALIGN_CENTER, 0, -10);
    lv_timer_create(toast_timer_cb, duration_ms, toast);
}

static void button_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn        = lv_event_get_target(e);
    
    if(setting_page.btn_save.obj == btn){
      if (code == LV_EVENT_CLICKED) {
          // Collect current values for confirmation display
          uint8_t brightness = (uint8_t)lv_slider_get_value(setting_page.bar_brightness.obj);
          bool auto_roll = lv_obj_has_state(setting_page.checkbox_auto_rolling.obj, LV_STATE_CHECKED);
          bool flip      = lv_obj_has_state(setting_page.checkbox_screen_flip.obj,  LV_STATE_CHECKED);

          char freq_buf[32]  = "N/A";
          char vcore_buf[32] = "N/A";
          char saver_buf[16] = "never";

          if (setting_page.list_asic_freq.obj) {
              lv_dropdown_get_selected_str(setting_page.list_asic_freq.obj, freq_buf, sizeof(freq_buf));
          }
          if (setting_page.list_asic_vcore.obj) {
              lv_dropdown_get_selected_str(setting_page.list_asic_vcore.obj, vcore_buf, sizeof(vcore_buf));
          }
          if (setting_page.list_saver.obj) {
              lv_dropdown_get_selected_str(setting_page.list_saver.obj, saver_buf, sizeof(saver_buf));
          }

          static char confirm_msg[512];
          snprintf(confirm_msg, sizeof(confirm_msg),
              "#7F8FA6 Brightness:# #00E5FF %d%%#\n"
              "#7F8FA6 Frequency:# #00E5FF %s#\n"
              "#7F8FA6 Vcore:# #00E5FF %s#\n"
              "#7F8FA6 Screen Roll:# #00E5FF %s#\n"
              "#7F8FA6 Screen Flip:# #00E5FF %s#\n"
              "#7F8FA6 Screen Saver:# #00E5FF %s#",
              brightness,
              freq_buf,
              vcore_buf,
              auto_roll ? "ON" : "OFF",
              flip       ? "ON" : "OFF",
              saver_buf);

          static const char *btns[] = {"OK", "Cancel", ""};
          lv_obj_t *mbox = lv_msgbox_create(NULL, "Save Settings?", confirm_msg, btns, false);
          enable_label_recolor_recursive(mbox);
          lv_obj_add_event_cb(mbox, msgbox_save_cb, LV_EVENT_VALUE_CHANGED, NULL);
          lv_obj_center(mbox);
      }
    }
    else if(setting_page.btn_restart.obj == btn){
      if (code == LV_EVENT_CLICKED) { 
          static const char * btns[] = {"Yes", "No", ""};
          lv_obj_t *mbox = lv_msgbox_create(NULL, "",
                                            "Restart Miner?",
                                            btns, false);
          lv_obj_add_event_cb(mbox, msgbox_restart_cb, LV_EVENT_VALUE_CHANGED, NULL);
          lv_obj_center(mbox);
      }
    }
}

static void scroll_begin_cb(lv_event_t *e) {
    // lv_obj_scroll_by (the snap path) computes time via lv_anim_speed_to_time clamped to [200,400]ms.
    // SCROLL_BEGIN passes the in-flight lv_anim_t* as param, so we can override time before it starts.
    lv_anim_t *a = (lv_anim_t *)lv_event_get_param(e);
    if (a) {
        lv_anim_set_time(a, 500); // override to 80ms for smoother transition (reduced from 120ms)
        // Set path to ease-out for natural deceleration feel
        lv_anim_set_path_cb(a, lv_anim_path_ease_out);
    }
}

// Scroll state captured at finger RELEASE (before LVGL snap animation).
// LV_EVENT_SCROLL_END fires AFTER snap completes — by then scroll offset is already
// at the snapped tile center so drift = 0 and we can't detect the user swipe distance.
// Capturing at RELEASED preserves the actual finger-lift position for use in scroll_end_cb.
static lv_coord_t s_release_scroll_x = 0;
static lv_coord_t s_release_scroll_y = 0;
static lv_obj_t  *s_release_tile     = nullptr;

static void scroll_end_cb(lv_event_t *e) {
    // Override LVGL snap using the scroll position captured at finger RELEASE.
    // s_release_scroll_x/y reflects where the user let go; we use that to decide
    // the intended destination page regardless of LVGL's own snap decision.
    if (!parent_wall || !s_release_tile) return;

    lv_obj_t *tv = (lv_obj_t *)lv_event_get_target(e);

    // Consume the captured release state immediately. This prevents stale touch data
    // from interfering with subsequent programmatic scrolls (e.g. auto_rolling timer),
    // whose LV_EVENT_SCROLL_END would otherwise re-enter here with outdated s_release_tile
    // and incorrectly override the destination — causing the A→B→A bounce loop.
    lv_obj_t   *release_tile     = s_release_tile;
    lv_coord_t  release_scroll_x = s_release_scroll_x;
    lv_coord_t  release_scroll_y = s_release_scroll_y;
    s_release_tile = nullptr;  // consumed — re-armed on next finger RELEASED event

    // Resolve the departure page (page user was on when they started the swipe)
    int departure_page = -1;
    for (int i = 0; i <= UI_PAGE_SETTING_SWARM; i++) {
        if (g_board.status.ui.page.list[i] == release_tile) { departure_page = i; break; }
    }
    if (departure_page < 0) return;

    // Drift = distance from departure tile center to where the finger released
    lv_coord_t ref_x   = lv_obj_get_x(release_tile);
    lv_coord_t ref_y   = lv_obj_get_y(release_tile);
    lv_coord_t drift_x = release_scroll_x - ref_x;
    lv_coord_t drift_y = release_scroll_y - ref_y;

    // Threshold: 20% of screen size
    const lv_coord_t THRESHOLD_X = SCREEN_WIDTH * 0.20;
    const lv_coord_t THRESHOLD_Y = SCREEN_HEIGHT * 0.20;

    lv_obj_t *target_tile = nullptr;

    if (LV_ABS(drift_x) > LV_ABS(drift_y)) {
        if (LV_ABS(drift_x) > THRESHOLD_X) {
            if (drift_x > 0) {
                // Finger moved left → show right neighbour
                switch (departure_page) {
                    case UI_PAGE_MINER:     target_tile = g_board.status.ui.page.list[UI_PAGE_SETTING_SWARM];   break;
                    case UI_PAGE_DASHBOARD: target_tile = g_board.status.ui.page.list[UI_PAGE_MARKET];    break;
                    case UI_PAGE_HR_HEALTH: target_tile = g_board.status.ui.page.list[UI_PAGE_CLOCK];     break;
                    default: break;
                }
            } else {
                // Finger moved right → show left neighbour
                switch (departure_page) {
                    case UI_PAGE_SETTING_SWARM: target_tile = g_board.status.ui.page.list[UI_PAGE_MINER];     break;
                    case UI_PAGE_MARKET:  target_tile = g_board.status.ui.page.list[UI_PAGE_DASHBOARD]; break;
                    case UI_PAGE_CLOCK:   target_tile = g_board.status.ui.page.list[UI_PAGE_HR_HEALTH]; break;
                    default: break;
                }
            }
        }
    } else {
        if (LV_ABS(drift_y) > THRESHOLD_Y) {
            if (drift_y > 0) {
                // Finger moved up → show lower neighbour
                switch (departure_page) {
                    case UI_PAGE_MINER:     target_tile = g_board.status.ui.page.list[UI_PAGE_DASHBOARD]; break;
                    case UI_PAGE_DASHBOARD: target_tile = g_board.status.ui.page.list[UI_PAGE_HR_HEALTH]; break;
                    case UI_PAGE_MARKET:    target_tile = g_board.status.ui.page.list[UI_PAGE_CLOCK];     break;
                    case UI_PAGE_SETTING_SWARM:   target_tile = g_board.status.ui.page.list[UI_PAGE_MARKET];    break;
                    default: break;
                }
            } else {
                // Finger moved down → show upper neighbour
                switch (departure_page) {
                    case UI_PAGE_DASHBOARD: target_tile = g_board.status.ui.page.list[UI_PAGE_MINER];     break;
                    case UI_PAGE_HR_HEALTH: target_tile = g_board.status.ui.page.list[UI_PAGE_DASHBOARD]; break;
                    case UI_PAGE_CLOCK:     target_tile = g_board.status.ui.page.list[UI_PAGE_MARKET];    break;
                    case UI_PAGE_MARKET:    target_tile = g_board.status.ui.page.list[UI_PAGE_SETTING_SWARM];   break;
                    default: break;
                }
            }
        }
    }

    if (!target_tile) return;  // drift < threshold, let LVGL's snap stand

    // Where did LVGL actually snap to after release?
    lv_obj_t *snapped_tile = lv_tileview_get_tile_act(tv);
    // Only override if LVGL snapped to the wrong tile
    if (target_tile != snapped_tile) {
        lv_obj_set_tile(parent_wall, target_tile, LV_ANIM_ON);
    }
}

static void ui_bounce_effect(lv_obj_t *current_page, uint8_t tp_evt) {
    if(parent_wall == nullptr || current_page == nullptr) return;
    const int16_t BOUNCE_PX = 25;
    int16_t dx = 0, dy = 0;
    switch(tp_evt) {
        case TOUCH_SWIPE_LEFT_EVT:  dx = -BOUNCE_PX; break;
        case TOUCH_SWIPE_RIGHT_EVT: dx = +BOUNCE_PX; break;
        case TOUCH_SWIPE_UP_EVT:    dy = -BOUNCE_PX; break;
        case TOUCH_SWIPE_DOWN_EVT:  dy = +BOUNCE_PX; break;
        default: return;
    }
    lv_obj_scroll_by(parent_wall, dx, dy, LV_ANIM_OFF); // instant offset
    lv_obj_set_tile(parent_wall, current_page, LV_ANIM_ON); // animate back to current tile
}

static void tileview_changed_cb(lv_event_t *e) {
    // Block tileview transitions until miner is ready to prevent UI glitch.
    // Must mask with INIT_EVENT_MINER_READY: return value is the full event group bits, not a bool.
    if (!(xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, 0) & INIT_EVENT_MINER_READY)) return;

    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *tv = (lv_obj_t *)lv_event_get_target(e);
    // If tileview is still animating/scrolling, this is the start-of-scroll event; ignore it.
    if (lv_obj_is_scrolling(tv)) return;
    lv_obj_t *active_tile = lv_tileview_get_tile_act(tv);
    for (int i = 0; i <= UI_PAGE_SETTING_SWARM; i++) {
        if (g_board.status.ui.page.list[i] == active_tile) {
            g_board.status.ui.page.current = i;
            g_board.status.ui.page.last    = g_board.status.ui.page.current;
            if (i == UI_PAGE_SETTING_SWARM) s_setting_page_entered_ms = millis();
            xSemaphoreGive(g_board.status.ui.page.save_xsem);
            LOG_D("Page changed to %d", g_board.status.ui.page.current);
            break;
        }
    }
}

static void pressed_event_cb(lv_event_t *e) {
    static lv_point_t press_pt = {0, 0};
    const int16_t     SWIPE_THRESHOLD = 20;

    lv_event_code_t code  = lv_event_get_code(e);
    lv_indev_t     *indev = lv_event_get_indev(e);
    if (indev == NULL) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    // Tracks whether screensaver or find-neighbor was active at the moment of touch-down.
    // Set in PRESSED, consumed in RELEASED to suppress page switching on the dismissal tap/swipe.
    static bool s_was_special_state = false;

    if (code == LV_EVENT_PRESSED) {
        press_pt = pt;
        // Capture special state BEFORE clearing — dismissal touch must not also navigate
        s_was_special_state = (xEventGroupGetBits(g_board.status.sys_evt) & (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
        g_board.status.ui.last_active_ms = millis(); // track activity for screensaver inactivity timer
        xEventGroupClearBits(g_board.status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
    }else if (code == LV_EVENT_RELEASED) {
        // Capture scroll offset right now (before LVGL's snap animation starts).
        // scroll_end_cb uses these to measure the user's actual swipe distance.
        if (parent_wall) {
            s_release_scroll_x = lv_obj_get_scroll_x(parent_wall);
            s_release_scroll_y = lv_obj_get_scroll_y(parent_wall);
            s_release_tile     = lv_tileview_get_tile_act(parent_wall);
        }
        // If the finger-down dismissed screensaver/find-neighbor, consume the release state
        // so scroll_end_cb does not navigate to another page on this same interaction.
        if (s_was_special_state) {
            s_release_tile      = nullptr;
            s_was_special_state = false;
            return;
        }

        lv_coord_t dx = pt.x - press_pt.x;
        lv_coord_t dy = pt.y - press_pt.y;

        // track gesture for long-press countdown logic
        g_board.status.touch.evt = guess_touch_gesture(dx, dy, SWIPE_THRESHOLD);

        // bounce feedback when swiping toward a non-existent neighbour
        if (g_board.status.touch.evt != TOUCH_TAP_EVT) {
            // allowed physical scroll directions for each user page
            lv_dir_t allowed = LV_DIR_NONE;
            switch (g_board.status.ui.page.current) {
                case UI_PAGE_MINER:     allowed = (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_BOTTOM);               break;
                case UI_PAGE_DASHBOARD: allowed = (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP | LV_DIR_BOTTOM);  break;
                case UI_PAGE_HR_HEALTH: allowed = (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP);                  break;
                case UI_PAGE_CLOCK:     allowed = (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP);                  break;
                case UI_PAGE_MARKET:    allowed = (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP | LV_DIR_BOTTOM);  break;
                case UI_PAGE_SETTING_SWARM:   allowed = (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_BOTTOM);               break;
                default: break;
            }
            // map gesture to tileview dir (finger direction is OPPOSITE to content scroll direction)
            // e.g. finger LEFT → content scrolls left → reveals RIGHT column → LV_DIR_RIGHT
            lv_dir_t gesture_dir = LV_DIR_NONE;
            switch (g_board.status.touch.evt) {
                case TOUCH_SWIPE_LEFT_EVT:  gesture_dir = LV_DIR_RIGHT;  break; // finger left  → right tile
                case TOUCH_SWIPE_RIGHT_EVT: gesture_dir = LV_DIR_LEFT;   break; // finger right → left tile
                case TOUCH_SWIPE_UP_EVT:    gesture_dir = LV_DIR_BOTTOM; break; // finger up    → bottom tile
                case TOUCH_SWIPE_DOWN_EVT:  gesture_dir = LV_DIR_TOP;    break; // finger down  → top tile
                default: break;
            }
            if (gesture_dir != LV_DIR_NONE && !(allowed & gesture_dir)) {
                int8_t index = g_board.status.ui.page.current;
                ui_bounce_effect(g_board.status.ui.page.list[index], g_board.status.touch.evt);
            }
        }
    }
}

static void long_press_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    static uint32_t s_lp_last_tick = 0; // track last tick for long-press repeat logic
    if (code == LV_EVENT_LONG_PRESSED) {
        g_board.status.touch.evt = TOUCH_LONGPRESS_EVT;
        g_board.status.ui.page.countdown.timeout = BOARD_TOUCH_LONG_PRESS_TO_RECOVER;
        s_lp_last_tick = millis();
        // disable tileview scrolling to prevent user from swiping to other pages during long-press countdown, which can cause confusion and potential bugs if user switch page and trigger other events before countdown ends
        lv_obj_clear_flag(parent_wall, LV_OBJ_FLAG_SCROLLABLE);
        xEventGroupClearBits(g_board.status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED);
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if (millis() - s_lp_last_tick >= 1000) {
            s_lp_last_tick = millis();
            g_board.status.ui.page.countdown.timeout =
                (g_board.status.ui.page.countdown.timeout > 0)
                ? (g_board.status.ui.page.countdown.timeout - 1) : 0;
            if (g_board.status.ui.page.countdown.timeout <= 0) {
                xSemaphoreGive(g_board.status.recover_factory_xsem);
            }
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_add_flag(parent_wall, LV_OBJ_FLAG_SCROLLABLE); // re-enable scrolling when user releases long-press (either by lifting finger or system interrupt like incoming call that causes press lost), to restore normal UI behavior
        g_board.status.ui.page.countdown.timeout = BOARD_TOUCH_LONG_PRESS_TO_RECOVER;
    }
}

static void touchpad_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    // Software debounce: require touch to be held for at least TOUCH_DEBOUNCE_MS
    // before reporting PRESSED to LVGL.  This eliminates one-shot ghost touches
    // caused by EMI / power-supply noise on the FT6206 INT line.
    static const uint32_t TOUCH_DEBOUNCE_MS = 50; // empirically determined sweet spot for responsiveness without false triggers
    static uint32_t touch_start_ms = 0;

    if (g_board.touch->touched()) {
        if (touch_start_ms == 0) touch_start_ms = millis();
        if ((millis() - touch_start_ms) >= TOUCH_DEBOUNCE_MS) {
            TS_Point raw_p = g_board.touch->getPoint();
            bool flip = g_board.status.preference.screen.flip;
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

static void tft_flush_cb( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ){
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tftDriver->startWrite();
    tftDriver->setAddrWindow( area->x1, area->y1, w, h );
    tftDriver->pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tftDriver->endWrite();

    lv_disp_flush_ready(disp_drv);
}

/*********************************** display  func *******************************************/
void tft_bl_ctrl(int8_t percent){
  uint8_t pwm = 0;
  if((g_board.info.spec.name == BOARD_NMAXE_GAMMA_NAME) || (g_board.info.spec.name == BOARD_NMAXE_NAME)){
    pwm = 255 * (1 - percent * 0.01f);
  } 
  else if(g_board.info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    pwm = percent * 2.55;
  }
  else pwm = 128; //default mid brightness
  
  LOG_D("Set brightness %d%%, PWM=%d", percent, pwm);
  ledcWrite(g_board.info.spec.tft.bl.pwm_ch, pwm);
}

void tft_init(void* args){
  board_sal_t *board = (board_sal_t*)args;
  SCREEN_WIDTH  = board->info.spec.tft.height;
  SCREEN_HEIGHT = board->info.spec.tft.width;
  // Power on TFT
  if(board->info.spec.tft.pwr_pin >= 0){
    pinMode(board->info.spec.tft.pwr_pin, OUTPUT);
    digitalWrite(board->info.spec.tft.pwr_pin, LOW);
  }
  // Initialize backlight PWM
  if(board->info.spec.tft.bl.pin >= 0){
    pinMode(board->info.spec.tft.bl.pin, OUTPUT);
    ledcSetup(board->info.spec.tft.bl.pwm_ch, board->info.spec.tft.bl.pwm_freq, board->info.spec.tft.bl.pwm_resolution);
    ledcAttachPin(board->info.spec.tft.bl.pin, board->info.spec.tft.bl.pwm_ch);
    tft_bl_ctrl(0);//sleep when boot up 
  }

  tftDriver = new TFT_eSPI(SCREEN_HEIGHT, SCREEN_WIDTH);
  if(!tftDriver){
    LOG_E("Failed to create TFT_eSPI instance!");
    return;
  }
  else{
    LOG_D("Screen size: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
  }

  tftDriver->begin(board->info.spec.spi.cs_pin, 
                  board->info.spec.tft.dc_pin,
                  board->info.spec.tft.rst_pin, 
                  board->info.spec.spi.sclk_pin,
                  board->info.spec.spi.miso_pin,
                  board->info.spec.spi.mosi_pin,
                  board->info.spec.tft.color_invert
                );
  if(board->status.preference.screen.flip)tftDriver->setRotation(1); 
  else tftDriver->setRotation(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME ? 4 : 3); // NMQAxe++ use rotation 4, NMaxE use rotation 3
}

void touch_init(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board->touch->begin(100)){  // threshold=100: high enough to reject noise/ghost touches, low enough for normal finger presses
    LOG_W("No touch controller detected, disabling touch support.");
    delay(10);
    if(board->touch != nullptr) {
        delete board->touch;
        board->touch = nullptr;
    }
  }
  else {
    LOG_I("FT6206 touch controller initialized.");
  }
}

void ui_drv_register(void){
  LOG_I("lvgl version: %s", (String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch()).c_str());

  static lv_disp_draw_buf_t lvgl_draw_buf;
  static lv_disp_drv_t      disp_drv;
  static lv_indev_drv_t     indev_drv; 
  // Allocate LVGL color buffer in PSRAM for memory optimization
  static lv_color_t *color_buf = NULL;
  static const size_t color_buf_size = SCREEN_WIDTH * SCREEN_HEIGHT;
  
  if (color_buf == NULL) {
    // Try to allocate in PSRAM first
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t buffer_bytes = color_buf_size * sizeof(lv_color_t);
    
    if (psram_free > buffer_bytes * 2) { // Leave safety margin
      color_buf = (lv_color_t*)heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM);
      if (color_buf != NULL) {
        LOG_I("LVGL display buffer allocated in PSRAM: %d bytes at 0x%p", buffer_bytes, color_buf);
      }
    }
    
    // Fallback to internal RAM if PSRAM allocation failed
    if (color_buf == NULL) {
      color_buf = (lv_color_t*)heap_caps_malloc(buffer_bytes, MALLOC_CAP_INTERNAL);
      if (color_buf != NULL) {
        LOG_W("LVGL display buffer allocated in internal RAM: %d bytes", buffer_bytes);
      } else {
        LOG_E("Failed to allocate LVGL display buffer!");
        return; // Cannot proceed without display buffer
      }
    }
  }

  lv_disp_draw_buf_init( &lvgl_draw_buf, color_buf, NULL, color_buf_size );
  /*Initialize the display*/
  lv_disp_drv_init( &disp_drv );
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = tft_flush_cb;
  disp_drv.draw_buf = &lvgl_draw_buf;
  lv_disp_drv_register( &disp_drv );

  /* Register input device (touch) */
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER; 
  indev_drv.read_cb = touchpad_read_cb;   
  indev_drv.scroll_limit    = 6;      /* reduced from 8; smaller = scroll recognized sooner */
  indev_drv.scroll_throw    = 10;     /* reduced from 80; lower = more inertia, smoother feel after finger lifts */
  indev_drv.long_press_time = 500;        // long press time in milliseconds
  indev_drv.long_press_repeat_time = 100; // long press repeat time in milliseconds
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

  // ── SPIFFS file-system driver for LVGL (letter 'S', used by lv_gif) ─────
  // Registered here once so any LVGL widget can open "S:filename" from SPIFFS.
  {
    static lv_fs_drv_t spiffs_drv;
    lv_fs_drv_init(&spiffs_drv);
    spiffs_drv.letter = 'S';

    // open: prepend '/' so SPIFFS path looks like "/screensaver.gif"
    spiffs_drv.open_cb = +[](lv_fs_drv_t*, const char *path, lv_fs_mode_t mode) -> void* {
        String fpath = String("/") + path;
        const char *flag = (mode & LV_FS_MODE_WR) ? "w" : "r";
        File *f = new File(SPIFFS.open(fpath.c_str(), flag));
        if (!f || !(*f)) { delete f; return nullptr; }
        return (void*)f;
    };
    spiffs_drv.close_cb = +[](lv_fs_drv_t*, void *fp) -> lv_fs_res_t {
        File *f = (File*)fp; f->close(); delete f; return LV_FS_RES_OK;
    };
    spiffs_drv.read_cb  = +[](lv_fs_drv_t*, void *fp, void *buf, uint32_t btr, uint32_t *br) -> lv_fs_res_t {
        File *f = (File*)fp; *br = f->read((uint8_t*)buf, btr); return LV_FS_RES_OK;
    };
    spiffs_drv.seek_cb  = +[](lv_fs_drv_t*, void *fp, uint32_t pos, lv_fs_whence_t whence) -> lv_fs_res_t {
        File *f = (File*)fp;
        SeekMode sm = (whence == LV_FS_SEEK_CUR) ? SeekCur
                    : (whence == LV_FS_SEEK_END) ? SeekEnd : SeekSet;
        f->seek(pos, sm); return LV_FS_RES_OK;
    };
    spiffs_drv.tell_cb  = +[](lv_fs_drv_t*, void *fp, uint32_t *pos_p) -> lv_fs_res_t {
        File *f = (File*)fp; *pos_p = f->position(); return LV_FS_RES_OK;
    };

    lv_fs_drv_register(&spiffs_drv);
    LOG_I("LVGL SPIFFS FS driver registered (letter='S')");
  }

}

void ui_page_element_init(void* args){
  board_sal_t *board = (board_sal_t*)args;

  // logo worker image buffer init
  logo_worker_nmaxe.header.w = 60;
  logo_worker_nmaxe.header.h = 68;
  logo_worker_nmaxe.data_size = logo_worker_nmaxe.header.w * logo_worker_nmaxe.header.h * LV_IMG_PX_SIZE_ALPHA_BYTE;

  logo_worker_nmaxegamma.header.w = 60;
  logo_worker_nmaxegamma.header.h = 68;
  logo_worker_nmaxegamma.data_size = logo_worker_nmaxegamma.header.w * logo_worker_nmaxegamma.header.h * LV_IMG_PX_SIZE_ALPHA_BYTE;

  logo_worker_nmqaxepp.header.w = 70;
  logo_worker_nmqaxepp.header.h = 79;
  logo_worker_nmqaxepp.data_size = logo_worker_nmqaxepp.header.w * logo_worker_nmqaxepp.header.h * LV_IMG_PX_SIZE_ALPHA_BYTE;

  logo_miner_nmqaxepp_70_70.header.w = 70;
  logo_miner_nmqaxepp_70_70.header.h = 70;
  logo_miner_nmqaxepp_70_70.data_size = logo_miner_nmqaxepp_70_70.header.w * logo_miner_nmqaxepp_70_70.header.h * LV_IMG_PX_SIZE_ALPHA_BYTE;

  /*********************************** 135x240 image **************************************/
  loading_page_img_135_240.header.w = SCREEN_WIDTH;
  loading_page_img_135_240.header.h = SCREEN_HEIGHT;
  loading_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  config_page_img_135_240.header.w = SCREEN_WIDTH;
  config_page_img_135_240.header.h = SCREEN_HEIGHT;
  config_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  mining_page_img_135_240.header.w = SCREEN_WIDTH;
  mining_page_img_135_240.header.h = SCREEN_HEIGHT;
  mining_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  black_page_img_135_240.header.w = SCREEN_WIDTH;
  black_page_img_135_240.header.h = SCREEN_HEIGHT;
  black_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  block_hits_page_img_135_240.header.w = SCREEN_WIDTH;
  block_hits_page_img_135_240.header.h = SCREEN_HEIGHT;
  block_hits_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  new_achievement_page_img_135_240.header.w = SCREEN_WIDTH;
  new_achievement_page_img_135_240.header.h = SCREEN_HEIGHT;
  new_achievement_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  status_page_img_135_240.header.w = SCREEN_WIDTH;
  status_page_img_135_240.header.h = SCREEN_HEIGHT;
  status_page_img_135_240.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  /*********************************** 240x320 image **************************************/
  loading_page_img_240_320.header.w = SCREEN_WIDTH;
  loading_page_img_240_320.header.h = SCREEN_HEIGHT;
  loading_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  config_page_img_240_320.header.w = SCREEN_WIDTH;
  config_page_img_240_320.header.h = SCREEN_HEIGHT;
  config_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  mining_page_img_240_320.header.w = SCREEN_WIDTH;
  mining_page_img_240_320.header.h = SCREEN_HEIGHT;
  mining_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  black_page_img_240_320.header.w = SCREEN_WIDTH;
  black_page_img_240_320.header.h = SCREEN_HEIGHT;
  black_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  block_hits_page_img_240_320.header.w = SCREEN_WIDTH;
  block_hits_page_img_240_320.header.h = SCREEN_HEIGHT;
  block_hits_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  new_achievement_page_img_240_320.header.w = SCREEN_WIDTH;
  new_achievement_page_img_240_320.header.h = SCREEN_HEIGHT;
  new_achievement_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  status_page_img_240_320.header.w = SCREEN_WIDTH;
  status_page_img_240_320.header.h = SCREEN_HEIGHT;
  status_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;

  if((board->info.spec.name == BOARD_NMAXE_NAME) || (board->info.spec.name  == BOARD_NMAXE_GAMMA_NAME)){
    loading_page.back_img_dsc           = &loading_page_img_135_240;
    config_page.back_img_dsc            = &config_page_img_135_240;
    miner_page.back_img_dsc             = &mining_page_img_135_240;
    dashboard_page.back_img_dsc         = &status_page_img_135_240;
    hr_health_page.back_img_dsc         = &status_page_img_135_240;
    clock_page.back_img_dsc             = &black_page_img_135_240;
    miner_page.logo_img_dsc             = (board->info.spec.name == BOARD_NMAXE_NAME) ? &logo_worker_nmaxe : &logo_worker_nmaxegamma;
    config_page.logo_img_dsc            = (board->info.spec.name == BOARD_NMAXE_NAME) ? &logo_worker_nmaxe : &logo_worker_nmaxegamma;
    setting_page.back_img_dsc           = &black_page_img_135_240;
    market_page.back_img_dsc            = &black_page_img_135_240;
    /*********************************** Loading page *********************************/
    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {0, 0};
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 13};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_16;
    loading_page.lb_pool_url.coord      = {0, 35};
    /*********************************** Config page *********************************/
    config_page.img_logo.coord          = {35, 0};
    config_page.lb_config_txt.font      = &lv_font_montserrat_14;
    config_page.lb_config_txt.coord     = {10, 40 };
    config_page.lb_version.font         = &lv_font_montserrat_16;
    config_page.lb_version.coord        = {70, 0};
    config_page.lb_cfg_timeout.font     = &lv_font_montserrat_14;
    config_page.lb_cfg_timeout.coord    = {175, 0 }; 
    config_page.qr_code.coord           = {0, 0};
    config_page.qr_code.font            = &lv_font_montserrat_14;
    /*********************************** mining page *********************************/
    miner_page.img_logo.coord           = {45, 20};
    miner_page.lb_share.font            = &ds_digib_font_18;
    miner_page.lb_share.coord           = {132, 41};
    miner_page.lb_hr_unit.font          = &ds_digib_font_28;
    miner_page.lb_hr_unit.coord         = {182, 110};
    miner_page.lb_blk_hit.font          = &ds_digib_font_56;
    miner_page.lb_blk_hit.coord         = {6, 36};
    miner_page.lb_fan_symb.font         = &symbol_14;
    miner_page.lb_fan_symb.coord        = {110, 76};
    miner_page.lb_hashrate.font         = &ds_digib_font_38;
    miner_page.lb_hashrate.coord        = {50, 0};
    miner_page.lb_price.font            = &ds_digib_font_20;
    miner_page.lb_price.coord           = {33, 29};
    miner_page.lb_diff.font             = &ds_digib_font_18;
    miner_page.lb_diff.coord            = {132, 25};
    miner_page.lb_ver.font              = &ds_digib_font_16;
    miner_page.lb_ver.coord             = {2, 22};
    miner_page.lb_power.font            = &Inconsolata_18;
    miner_page.lb_power.coord           = {16, 114};
    miner_page.lb_ip.font               = &ds_digib_font_16;
    miner_page.lb_ip.coord              = {140, 2};
    miner_page.lb_uptime_hms.font       = &ds_digib_font_16;
    miner_page.lb_uptime_hms.coord      = {65, 2};
    miner_page.lb_uptime_day.font       = &ds_digib_font_16;
    miner_page.lb_uptime_day.coord      = {32, 2};
    miner_page.lb_uptime_day_unit.font  = &lv_font_montserrat_14;
    miner_page.lb_uptime_day_unit.coord = {56, 2};
    miner_page.lb_temp.font             = &ds_digib_font_18;
    miner_page.lb_temp.coord            = {132, 58};
    miner_page.lb_uptime_symbol.font    = &lv_font_montserrat_14;
    miner_page.lb_uptime_symbol.coord   = {18, 1};
    miner_page.lb_wifi_symbol.font      = &lv_font_montserrat_14;
    miner_page.lb_wifi_symbol.coord     = {123, 1};
    miner_page.lb_fan.font              = &ds_digib_font_18;
    miner_page.lb_fan.coord             = {132, 75};
    miner_page.lb_temp_symb.font        = &symbol_14;
    miner_page.lb_temp_symb.coord       = {113, 59};
    miner_page.lb_diff_symbol.font      = &symbol_14;
    miner_page.lb_diff_symbol.coord     = {108, 26};
    miner_page.lb_share_symb.font       = &symbol_14;
    miner_page.lb_share_symb.coord      = {108, 42};
    /*********************************** dashboard page *********************************/
    dashboard_page.lb_hr.font           = &ds_digib_font_36;
    dashboard_page.lb_hr.coord          = {75, -4};
    dashboard_page.lb_hr_unit.font      = &ds_digib_font_20;
    dashboard_page.lb_hr_unit.coord     = {138, 8};
    dashboard_page.img_miner.coord      = {0, 60};
    dashboard_page.miner_img_dsc        = &logo_worker_nmaxe;

    uint8_t arc_r = 30, arc_line_width = 8, arc_angle_full = 230;

    dashboard_page.ring_oc.cfg = {
        .x = 0,
        .y = 4,
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "OC",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_pwr.cfg = {
        .x = (lv_coord_t)(2 * arc_r + 10),
        .y = 4,
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Power",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_vc_req.cfg = {
        .x = 0,
        .y = (lv_coord_t)(4 + 2 * arc_r + 8),
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Vc req",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_vc_real.cfg = {
        .x = (lv_coord_t)(2 * arc_r + 10),
        .y = (lv_coord_t)(4 + 2 * arc_r + 8),
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Vc real",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_asic_temp.cfg = {
        .x = 140,
        .y = 40,
        .radius = (lv_coord_t)(3 * arc_r / 2),
        .line_width = (lv_coord_t)(arc_line_width * 2),
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_20,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "ASIC Temp",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    /******************************** hashrate healthy page *****************************/
    hr_health_page.lb_hr.font           = &ds_digib_font_36;
    hr_health_page.lb_hr.coord          = {75, -4};

    hr_health_page.lb_hr_unit.font      = &ds_digib_font_20;
    hr_health_page.lb_hr_unit.coord     = {138, 8};

    hr_health_page.lb_scale.font        = &lv_font_montserrat_14;
    hr_health_page.lb_scale.coord       = {-125, 5};

    hr_health_page.total_hr_chart.coord = {15, 8};
    hr_health_page.total_hr_chart.font  = &lv_font_montserrat_10;
    /******************************** big digit healthy page *****************************/
    clock_page.lb_hr.font           = &ds_digib_font_56;
    clock_page.lb_hr.coord          = {0, 0};

    clock_page.lb_hr_unit.font      = &ds_digib_font_20;
    clock_page.lb_hr_unit.coord     = {95, 26};

    clock_page.lb_hits.font         = &ds_digib_font_56;
    clock_page.lb_hits.coord        = {-30, 0};

    clock_page.lb_hits_unit.font    = &ds_digib_font_20;
    clock_page.lb_hits_unit.coord   = {190, 26};

    clock_page.lb_date.font         = &ds_digib_font_24;
    clock_page.lb_date.coord        = {0, 0};

    clock_page.lb_time.font         = &ds_digib_font_56;
    clock_page.lb_time.coord        = {0, 15};

    clock_page.lb_ampm.font         = &lv_font_montserrat_14;
    clock_page.lb_ampm.coord        = {0, 15};

    clock_page.lb_price.font        = &ds_digib_font_24;
    clock_page.lb_price.coord       = {0, 0};
    /*********************************** Market page (NMAxe / NMAxe Gamma, ~240x135) *********************************/
    market_page.coin_font           = &Inconsolata_18;
    market_page.col_dollar_x        = 73;
    market_page.col_price_x         = 86;
    market_page.col_change_x        = 185;
    market_page.lb_hr.font          = &ds_digib_font_32;
    market_page.lb_hr_unit.font     = &Inconsolata_16;
    market_page.lb_version.font     = &lv_font_montserrat_14;
    market_page.lb_version.coord    = {2, -3};  // offset from BOTTOM_LEFT
  }
  else if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    loading_page.back_img_dsc           = &loading_page_img_240_320;
    config_page.back_img_dsc            = &black_page_img_240_320;
    miner_page.back_img_dsc             = &mining_page_img_240_320;
    dashboard_page.back_img_dsc         = &status_page_img_240_320;
    hr_health_page.back_img_dsc         = &status_page_img_240_320;
    clock_page.back_img_dsc             = &black_page_img_240_320;
    miner_page.logo_img_dsc             = &logo_worker_nmqaxepp;
    config_page.logo_img_dsc            = &logo_worker_nmqaxepp;
    setting_page.back_img_dsc           = &black_page_img_240_320;// only for NMQAxe++ since it has more settings items and need bigger screen to display
    market_page.back_img_dsc            = &black_page_img_240_320;// only for NMQAxe++ since it has more market info and need bigger screen to display
    /*********************************** Loading page *********************************/
    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {0, 0};
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 25};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_20;
    loading_page.lb_pool_url.coord      = {0, 65};
    /*********************************** Config page *********************************/
    config_page.img_logo.coord          = {50, 50};
    config_page.lb_config_txt.font      = &lv_font_montserrat_16;
    config_page.lb_config_txt.coord     = {15, 87 };
    config_page.lb_version.font         = &lv_font_montserrat_20;
    config_page.lb_version.coord        = {88, 10};
    config_page.lb_cfg_timeout.font     = &lv_font_montserrat_20;
    config_page.lb_cfg_timeout.coord    = {225, -15 }; 
    config_page.qr_code.coord           = {0, 0};
    config_page.qr_code.font            = &lv_font_montserrat_14;
    /*********************************** mining page *********************************/
    miner_page.img_logo.coord           = {70, 44};
    miner_page.lb_hr_unit.font          = &ds_digib_font_24;
    miner_page.lb_hr_unit.coord         = {268 , 172};
    miner_page.lb_blk_hit.font          = &ds_digib_font_56;
    miner_page.lb_blk_hit.coord         = {20, 65};
    miner_page.lb_hashrate.font         = &ds_digib_font_52;
    miner_page.lb_hashrate.coord        = {62, -44};
    miner_page.lb_price.font            = &ds_digib_font_24;
    miner_page.lb_price.coord           = {60, 25};
    miner_page.lb_ver.font              = &ds_digib_font_18;
    miner_page.lb_ver.coord             = {16, 38};
    miner_page.lb_power.font            = &Inconsolata_26;
    miner_page.lb_power.coord           = {20, 169};
    miner_page.lb_ip.font               = &ds_digib_font_18;
    miner_page.lb_ip.coord              = {148+ 60, 2};
    miner_page.lb_uptime_hms.font       = &ds_digib_font_18;
    miner_page.lb_uptime_hms.coord      = {65+ 58, 2};
    miner_page.lb_uptime_day.font       = &ds_digib_font_18;
    miner_page.lb_uptime_day.coord      = {32+ 55, 2};
    miner_page.lb_uptime_day_unit.font  = &lv_font_montserrat_16;
    miner_page.lb_uptime_day_unit.coord = {56+ 57, 2};
    miner_page.lb_uptime_symbol.font    = &lv_font_montserrat_16;
    miner_page.lb_uptime_symbol.coord   = {18 + 55, 1};
    miner_page.lb_wifi_symbol.font      = &lv_font_montserrat_16;
    miner_page.lb_wifi_symbol.coord     = {128+ 59, 1};

    miner_page.lb_diff.font             = &ds_digib_font_24;
    miner_page.lb_diff.coord            = {132+ 55, 30};
    miner_page.lb_share.font            = &ds_digib_font_24;
    miner_page.lb_share.coord           = {132+ 55, 55};
    miner_page.lb_temp.font             = &ds_digib_font_24;
    miner_page.lb_temp.coord            = {132+ 55, 83};
    miner_page.lb_fan.font              = &ds_digib_font_24;
    miner_page.lb_fan.coord             = {132+ 55, 110};

    miner_page.lb_diff_symbol.font      = &symbol_20;
    miner_page.lb_diff_symbol.coord     = {108 + 45, 30};
    miner_page.lb_share_symb.font       = &symbol_20;
    miner_page.lb_share_symb.coord      = {108 + 43, 55};
    miner_page.lb_temp_symb.font        = &symbol_20;
    miner_page.lb_temp_symb.coord       = {113 + 47, 83};
    miner_page.lb_fan_symb.font         = &symbol_20;
    miner_page.lb_fan_symb.coord        = {110 + 45, 110};

    miner_page.lb_utc_time.font          = &ds_digib_font_18;
    miner_page.lb_utc_time.coord         = {1, 3};

    miner_page.lb_swarm_best_diff.font      = &ds_digib_font_24;
    miner_page.lb_swarm_best_diff.coord     = {3, 210};
    miner_page.lb_swarm_workers.font        = &ds_digib_font_24;
    miner_page.lb_swarm_workers.coord       = {145, 210};
    miner_page.lb_swarm_total_hashrate.font = &ds_digib_font_24;
    miner_page.lb_swarm_total_hashrate.coord= {237, 210}; 
    /*********************************** dashboard page *********************************/
    dashboard_page.lb_hr.font          = &ds_digib_font_56;
    dashboard_page.lb_hr.coord         = {55 + 40, -4};
    dashboard_page.lb_hr_unit.font     = &ds_digib_font_20;
    dashboard_page.lb_hr_unit.coord    = {100 + 95, 23};
    dashboard_page.img_miner.coord     = {0, 55};
    dashboard_page.lb_diff.font        = &Inconsolata_16;
    dashboard_page.lb_diff.coord       = {0, 0};
    dashboard_page.miner_img_dsc       = &logo_miner_nmqaxepp_70_70;

    uint8_t arc_r = 30, arc_line_width = 8, arc_angle_full = 230;
    
    dashboard_page.ring_oc.cfg = {
        .x = 0,
        .y = 8,
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "OC",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_pwr.cfg = {
        .x = (lv_coord_t)(2 * arc_r + 10),
        .y = 8,
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Power",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_vc_req.cfg = {
        .x = 0,
        .y = (lv_coord_t)(4 + 2 * arc_r + 8),
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Vc req",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_vc_real.cfg = {
        .x = (lv_coord_t)(2 * arc_r + 10),
        .y = (lv_coord_t)(4 + 2 * arc_r + 8),
        .radius = arc_r,
        .line_width = arc_line_width,
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_14,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Vc real",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };
    
    dashboard_page.ring_asic_temp.cfg = {
        .x = 227,
        .y = 50,
        .radius = (lv_coord_t)(3 * arc_r / 2),
        .line_width = (lv_coord_t)(arc_line_width * 2),
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_20,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "ASIC Temp",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };

    dashboard_page.ring_vcore_temp.cfg = {
        .x = 132,
        .y = 50,
        .radius = (lv_coord_t)(3 * arc_r / 2),
        .line_width = (lv_coord_t)(arc_line_width * 2),
        .angle_start = 0,
        .angle_end = 0,
        .angle_full = arc_angle_full,
        .bg_color = lv_color_hex(0xC0C0C0),
        .indicator_color = lv_color_hex(0x0080FF),
        .center_text = " ",
        .center_font = &lv_font_montserrat_20,
        .center_text_color = lv_color_hex(0xFFFFFF),
        .title_text = "Vc Temp",
        .title_font = &lv_font_montserrat_14,
        .title_text_color = lv_color_hex(0xD3D3D3)
    };

    /******************************** hashrate healthy page *****************************/
    hr_health_page.lb_hr.font          = &ds_digib_font_56;
    hr_health_page.lb_hr.coord         = {55 + 40, -4};

    hr_health_page.lb_hr_unit.font      = &ds_digib_font_20;
    hr_health_page.lb_hr_unit.coord     = {100 + 95, 23};

    hr_health_page.lb_scale.font        = &lv_font_montserrat_16;
    hr_health_page.lb_scale.coord       = {0, 45};

    hr_health_page.total_hr_chart.font  = &lv_font_montserrat_12;
    hr_health_page.total_hr_chart.coord = {15, 8};

    hr_health_page.asic_hr_chart.center_x = 100;
    hr_health_page.asic_hr_chart.center_y = 65;
    hr_health_page.asic_hr_chart.radius   = 53;

    hr_health_page.asic_hr_chart.cfg[0].angle = 90;
    hr_health_page.asic_hr_chart.cfg[0].color = lv_color_hex(0xA7A9AC);
    hr_health_page.asic_hr_chart.cfg[0].label = "A1\r25%";
    
    hr_health_page.asic_hr_chart.cfg[1].angle = 90;
    hr_health_page.asic_hr_chart.cfg[1].color = lv_color_hex(0xF05A28);
    hr_health_page.asic_hr_chart.cfg[1].label = "A2\r25%";
    
    hr_health_page.asic_hr_chart.cfg[2].angle = 90;
    hr_health_page.asic_hr_chart.cfg[2].color = lv_color_hex(0x00ADEF);
    hr_health_page.asic_hr_chart.cfg[2].label = "A3\r25%";
    
    hr_health_page.asic_hr_chart.cfg[3].angle = 90;
    hr_health_page.asic_hr_chart.cfg[3].color = lv_color_hex(0x2E3192);
    hr_health_page.asic_hr_chart.cfg[3].label = "A4\r25%";


    /******************************** big digit healthy page *****************************/
    clock_page.lb_hr.font           = &ds_digib_font_56;
    clock_page.lb_hr.coord          = {0, 0};

    clock_page.lb_hr_unit.font      = &ds_digib_font_20;
    clock_page.lb_hr_unit.coord     = {100, 26};

    clock_page.lb_hits.font         = &ds_digib_font_56;
    clock_page.lb_hits.coord        = {-30, 0};

    clock_page.lb_hits_unit.font    = &ds_digib_font_20;
    clock_page.lb_hits_unit.coord   = {280, 26};

    clock_page.lb_date.font         = &ds_digib_font_24;
    clock_page.lb_date.coord        = {0, 0};

    clock_page.lb_time.font         = &ds_digib_font_120;
    clock_page.lb_time.coord        = {0, 0};

    clock_page.lb_ampm.font         = &lv_font_montserrat_20;
    clock_page.lb_ampm.coord        = {0, 15};

    clock_page.lb_price.font        = &ds_digib_font_24;
    clock_page.lb_price.coord       = {0, 0};
    /*********************************** Market page (NMQAxe++, ~320x240) *********************************/
    market_page.coin_font           = &Inconsolata_26;
    market_page.col_dollar_x        = 87;
    market_page.col_price_x         = 103;
    market_page.col_change_x        = 242;
    market_page.lb_hr.font          = &ds_digib_font_38;
    market_page.lb_hr_unit.font     = &Inconsolata_16;
    market_page.lb_version.font     = &lv_font_montserrat_16;
    market_page.lb_version.coord    = {2, -3};  // offset from BOTTOM_LEFT
  }
  else{
      LOG_E("Unknown board type for UI layout init: %s", board->info.spec.name);
  }
}

void ui_layout_init(void* args){

  board_sal_t *board = (board_sal_t*)args;

  // =====================================================================
  // PAGE GRID LAYOUT TABLE (lv_tileview based)
  // col=0: loading/config tiles (LV_DIR_NONE, not user-accessible)
  // col=1,2: user-navigable tiles; dir = physical scroll directions allowed.
  // Direction mapping (lv_tileview v8) — physical scroll direction:
  //   LV_DIR_RIGHT  = scroll right → col+1
  //   LV_DIR_LEFT   = scroll left  → col-1
  //   LV_DIR_BOTTOM = scroll down  → row+1
  //   LV_DIR_TOP    = scroll up    → row-1
  // Only set directions where a real tile exists at the target position.
  // =====================================================================
  struct page_grid_t {
    lv_obj_t     **container;
    lv_obj_t     **back_img_obj;
    lv_img_dsc_t **back_img_dsc;
    uint8_t        col;
    uint8_t        row;
    int            page_idx;
    lv_dir_t       dir;
  };
  const page_grid_t page_grid[] = {
    // container                       back_img_obj                       back_img_dsc         col  row      page_idx           dir
    { &loading_page.container,    &loading_page.back_img_obj,    &loading_page.back_img_dsc,    0,   0,   UI_PAGE_LOADING,   LV_DIR_NONE},
    { &config_page.container,     &config_page.back_img_obj,     &config_page.back_img_dsc,     0,   1,   UI_PAGE_CONFIG,    LV_DIR_NONE },
    { &miner_page.container,      &miner_page.back_img_obj,      &miner_page.back_img_dsc,      1,   0,   UI_PAGE_MINER,     (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_BOTTOM)              }, // →setting(2,0), ↓dashboard(1,1)
    { &dashboard_page.container,  &dashboard_page.back_img_obj,  &dashboard_page.back_img_dsc,  1,   1,   UI_PAGE_DASHBOARD, (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP | LV_DIR_BOTTOM) }, // right→market(2,1), up→miner(1,0), down→hr_health(1,2)
    { &hr_health_page.container,  &hr_health_page.back_img_obj,  &hr_health_page.back_img_dsc,  1,   2,   UI_PAGE_HR_HEALTH, (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP)                 }, // right→clock(2,2), up→dashboard(1,1)
    { &clock_page.container,      &clock_page.back_img_obj,      &clock_page.back_img_dsc,      2,   2,   UI_PAGE_CLOCK,     (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP)                 }, // left→hr_health(1,2), up→market(2,1)
    { &market_page.container,     &market_page.back_img_obj,     &market_page.back_img_dsc,     2,   1,   UI_PAGE_MARKET,    (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP | LV_DIR_BOTTOM) }, // left→dashboard(1,1), up→setting(2,0), down→clock(2,2)
    { &setting_page.container,    &setting_page.back_img_obj,    &setting_page.back_img_dsc,    2,   0,   UI_PAGE_SETTING_SWARM,   (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_BOTTOM)              }, // left→miner(1,0), down→market(2,1)
  };
  // =====================================================================
  parent_wall = lv_tileview_create(lv_scr_act());
  lv_obj_set_pos(parent_wall, 0, 0);
  lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scrollbar_mode(parent_wall, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(parent_wall, 0, 0);
  lv_obj_set_style_border_width(parent_wall, 0, 0);
  lv_obj_set_style_bg_opa(parent_wall, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_event_cb(parent_wall, pressed_event_cb,   LV_EVENT_PRESSED,  NULL); // record press origin for bounce detection
  lv_obj_add_event_cb(parent_wall, pressed_event_cb,   LV_EVENT_RELEASED, NULL); // bounce on blocked-direction swipes
  lv_obj_add_event_cb(parent_wall, long_press_event_cb, LV_EVENT_LONG_PRESSED,        NULL);
  lv_obj_add_event_cb(parent_wall, long_press_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
  lv_obj_add_event_cb(parent_wall, long_press_event_cb, LV_EVENT_RELEASED,            NULL);
  lv_obj_add_event_cb(parent_wall, long_press_event_cb, LV_EVENT_PRESS_LOST,          NULL);
  lv_obj_add_event_cb(parent_wall, tileview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL); // sync page.current on native tileview scroll
  lv_obj_add_event_cb(parent_wall, scroll_begin_cb,     LV_EVENT_SCROLL_BEGIN,   NULL); // override snap animation time to 80ms with ease-out
  lv_obj_add_event_cb(parent_wall, scroll_end_cb,       LV_EVENT_SCROLL_END,     NULL); // lower threshold: force switch at 20% scroll
  
  // Create all page tiles via lv_tileview_add_tile. Each tile is auto-sized to
  // SCREEN_WIDTH × SCREEN_HEIGHT and positioned at (col*W, row*H) by the tileview.
  // loading/config tiles (col=0) have LV_DIR_NONE so the tileview's native scroll
  // cannot transition into them; user pages control their own allowed directions.
  for(const auto& p : page_grid) {
    *p.container = lv_tileview_add_tile(parent_wall, p.col, p.row, p.dir);
    lv_obj_set_style_pad_all(*p.container, 0, 0);
    lv_obj_set_style_border_width(*p.container, 0, 0);
    lv_obj_set_scrollbar_mode(*p.container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(*p.container, LV_OBJ_FLAG_SCROLLABLE); // prevent tile from rubber-band scrolling internally; all gestures must reach tileview
    lv_obj_add_flag(*p.container, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble long-press events up to parent_wall
    *p.back_img_obj = lv_img_create(*p.container);
    lv_img_set_src(*p.back_img_obj, *p.back_img_dsc);
    lv_obj_set_size(*p.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(*p.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(*p.back_img_obj, LV_OBJ_FLAG_CLICKABLE); // click-transparent, let events fall through to container
    board->status.ui.page.list[p.page_idx] = *p.container;
  }

  // Some pages have an extra logo image; set them individually after all containers are created
  config_page.img_logo.obj = lv_img_create(config_page.container); //worker logo
  lv_img_set_src(config_page.img_logo.obj, config_page.logo_img_dsc); 
  lv_obj_align(config_page.img_logo.obj, LV_ALIGN_TOP_LEFT, config_page.img_logo.coord.x, config_page.img_logo.coord.y);
  lv_obj_add_flag(config_page.img_logo.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall

  miner_page.img_logo.obj = lv_img_create(miner_page.container); //worker logo
  lv_img_set_src(miner_page.img_logo.obj, miner_page.logo_img_dsc); 
  lv_obj_align(miner_page.img_logo.obj, LV_ALIGN_TOP_LEFT, miner_page.img_logo.coord.x, miner_page.img_logo.coord.y);
  lv_obj_add_flag(miner_page.img_logo.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall
  //////////////////////////////////////loading page layout///////////////////////////////////////////////
  //Version label
  lv_color_t font_color = lv_color_hex(0xFFFFFF);
  lv_coord_t width = lv_txt_get_width(board->info.base.fw_version.c_str(), strlen(board->info.base.fw_version.c_str()), loading_page.lb_version.font, 0, LV_TEXT_FLAG_NONE);
  loading_page.lb_version.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_LOADING] );
  lv_obj_set_width(loading_page.lb_version.obj, width);
  lv_label_set_text( loading_page.lb_version.obj, board->info.base.fw_version.c_str());
  lv_obj_set_style_text_font(loading_page.lb_version.obj, loading_page.lb_version.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_version.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_version.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( loading_page.lb_version.obj, LV_ALIGN_BOTTOM_RIGHT, loading_page.lb_version.coord.x, loading_page.lb_version.coord.y);

  //details label
  loading_page.lb_details.obj = lv_label_create(board->status.ui.page.list[UI_PAGE_LOADING]);
  lv_obj_set_width(loading_page.lb_details.obj, SCREEN_WIDTH - width); // make sure it won't overflow, leave space for version label
  lv_obj_set_style_text_color(loading_page.lb_details.obj, font_color, LV_PART_MAIN);
  lv_label_set_text(loading_page.lb_details.obj, "Initializing...");
  lv_obj_set_style_text_font(loading_page.lb_details.obj, loading_page.lb_details.font, LV_PART_MAIN);
  lv_label_set_long_mode(loading_page.lb_details.obj, LV_LABEL_LONG_DOT);
  lv_obj_align(loading_page.lb_details.obj, LV_ALIGN_BOTTOM_LEFT, loading_page.lb_details.coord.x, loading_page.lb_details.coord.y);

  //bar progress
  loading_page.bar_progress.obj = lv_bar_create(board->status.ui.page.list[UI_PAGE_LOADING]);
  lv_obj_add_flag(loading_page.bar_progress.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall
  lv_bar_set_range(loading_page.bar_progress.obj, 0, 16);
  lv_bar_set_value(loading_page.bar_progress.obj, 0, LV_ANIM_ON);
  lv_obj_set_style_bg_opa(loading_page.bar_progress.obj, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_size(loading_page.bar_progress.obj, SCREEN_WIDTH * 0.9, 5);
  lv_obj_align(loading_page.bar_progress.obj, LV_ALIGN_CENTER, loading_page.bar_progress.coord.x, loading_page.bar_progress.coord.y);
  lv_obj_set_style_bg_color(loading_page.bar_progress.obj, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(loading_page.bar_progress.obj, LV_OPA_COVER, LV_PART_INDICATOR);

  //progress label
  loading_page.lb_progress.obj = lv_label_create(board->status.ui.page.list[UI_PAGE_LOADING]);
  lv_label_set_text(loading_page.lb_progress.obj, "");
  lv_obj_set_style_text_color(loading_page.lb_progress.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_align(loading_page.lb_progress.obj, LV_ALIGN_LEFT_MID, loading_page.lb_progress.coord.x, loading_page.lb_progress.coord.y);

  //slogan label
  loading_page.lb_ip_and_slogan.obj   = lv_label_create(board->status.ui.page.list[UI_PAGE_LOADING]);
  String slogan_str = "Make it better";
  width = lv_txt_get_width(slogan_str.c_str(), strlen(slogan_str.c_str()), loading_page.lb_ip_and_slogan.font, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(loading_page.lb_ip_and_slogan.obj, width);
  lv_label_set_text( loading_page.lb_ip_and_slogan.obj, slogan_str.c_str());
  lv_obj_set_style_text_font(loading_page.lb_ip_and_slogan.obj, loading_page.lb_ip_and_slogan.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_ip_and_slogan.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_ip_and_slogan.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( loading_page.lb_ip_and_slogan.obj, LV_ALIGN_CENTER, loading_page.lb_ip_and_slogan.coord.x, loading_page.lb_ip_and_slogan.coord.y);

  //pool url label
  loading_page.lb_pool_url.obj   = lv_label_create(board->status.ui.page.list[UI_PAGE_LOADING]);
  lv_label_set_text( loading_page.lb_pool_url.obj, "");
  lv_obj_set_style_text_font(loading_page.lb_pool_url.obj, loading_page.lb_pool_url.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_pool_url.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_pool_url.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( loading_page.lb_pool_url.obj, LV_ALIGN_CENTER, loading_page.lb_pool_url.coord.x, loading_page.lb_pool_url.coord.y);
  //////////////////////////////////////config page layout///////////////////////////////////////////////
  // version + timeout labels — created for all boards, positioned per board below
  font_color = lv_color_hex(0xFFFFFF);
  width = lv_txt_get_width(board->info.base.fw_version.c_str(), strlen(board->info.base.fw_version.c_str()), config_page.lb_version.font, 0, LV_TEXT_FLAG_NONE);
  config_page.lb_version.obj   = lv_label_create(board->status.ui.page.list[UI_PAGE_CONFIG]);
  lv_obj_set_width(config_page.lb_version.obj, width);
  lv_label_set_text( config_page.lb_version.obj, board->info.base.fw_version.c_str());
  lv_obj_set_style_text_font(config_page.lb_version.obj, config_page.lb_version.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(config_page.lb_version.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(config_page.lb_version.obj, LV_LABEL_LONG_DOT);

  config_page.lb_cfg_timeout.obj   = lv_label_create(board->status.ui.page.list[UI_PAGE_CONFIG]);
  lv_label_set_text( config_page.lb_cfg_timeout.obj, "");
  lv_obj_set_style_text_font(config_page.lb_cfg_timeout.obj, config_page.lb_cfg_timeout.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(config_page.lb_cfg_timeout.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(config_page.lb_cfg_timeout.obj, LV_LABEL_LONG_DOT);

  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    // NMQAxe++: touch WiFi Setup page
    // Version bottom-left, countdown bottom-right; no QR code, no config text, hide logo.
    // AP mode is kept by wifi_connect_thread as a web-config fallback (WIFI_AP → WIFI_AP_STA
    // when the user triggers a scan so scanning works while AP stays up).
    lv_obj_set_style_text_font(config_page.lb_version.obj,     &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(config_page.lb_cfg_timeout.obj, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_width(config_page.lb_version.obj,     80);
    lv_obj_set_width(config_page.lb_cfg_timeout.obj, 60);
    lv_obj_set_style_text_align(config_page.lb_cfg_timeout.obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(config_page.lb_version.obj,     LV_ALIGN_BOTTOM_LEFT,  4, -4);
    lv_obj_align(config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    // hide logo image — dynamic WiFi list will cover the page
    lv_obj_add_flag(config_page.img_logo.obj, LV_OBJ_FLAG_HIDDEN);
    // qr_code and lb_config_txt are not created for NMQAxe++; obj stays nullptr (zero-init)
  } else {
    // NMAxe / NMAxeGamma: existing QR code config page
    lv_obj_set_width(config_page.lb_cfg_timeout.obj, SCREEN_WIDTH);
    lv_obj_align(config_page.lb_version.obj,     LV_ALIGN_TOP_MID,    config_page.lb_version.coord.x,     config_page.lb_version.coord.y);
    lv_obj_align(config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, config_page.lb_cfg_timeout.coord.x, config_page.lb_cfg_timeout.coord.y);

    //QR code
    lv_coord_t qr_size = SCREEN_HEIGHT - 32;
    // Build QR string first so we can compute the exact QR version and
    // adjust canvas size to guarantee a visible quiet-zone border (>= 4 px).
    // qrcodegen_Ecc_MEDIUM == 1; qrcodegen_version2size(v) == 4*v+17 (QR spec).
    String qr_str = "WIFI:T:WPA;S:" + board->info.connection.wifi.ap.info.ssid + ";P:" + board->info.connection.wifi.ap.info.pwd + ";H:false;";
    {
      const int MIN_MARGIN_PX = 4;
      int32_t version = qrcodegen_getMinFitVersion(1 /*Ecc_MEDIUM*/, qr_str.length());
      if (version > 0) {
        int32_t modules = 4 * version + 17;  // qrcodegen_version2size formula
        int32_t scale   = qr_size / modules;
        if (scale > 0) {
          int32_t margin  = (qr_size - modules * scale) / 2;
          if (margin < MIN_MARGIN_PX) {
            qr_size = modules * scale + 2 * MIN_MARGIN_PX;
          }
        }
      }
    }
    config_page.qr_code.obj = lv_qrcode_create(board->status.ui.page.list[UI_PAGE_CONFIG], qr_size, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    lv_qrcode_update(config_page.qr_code.obj, (uint8_t*)qr_str.c_str(), qr_str.length());
    lv_obj_align(config_page.qr_code.obj, LV_ALIGN_RIGHT_MID, config_page.qr_code.coord.x, config_page.qr_code.coord.y);
    lv_obj_add_flag(config_page.qr_code.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall

    // config text label
    String config = board->info.connection.wifi.ap.info.ssid + "\r\n"+ board->info.connection.wifi.ap.ip.toString();
    config_page.lb_config_txt.obj  = lv_label_create(board->status.ui.page.list[UI_PAGE_CONFIG]);
    lv_obj_set_width(config_page.lb_config_txt.obj, SCREEN_WIDTH / 2);
    lv_label_set_text(config_page.lb_config_txt.obj, config.c_str());
    lv_obj_set_style_text_font(config_page.lb_config_txt.obj, config_page.lb_config_txt.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(config_page.lb_config_txt.obj, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(config_page.lb_config_txt.obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(config_page.lb_config_txt.obj, 0, LV_PART_MAIN); 
    lv_obj_align(config_page.lb_config_txt.obj, LV_ALIGN_LEFT_MID, config_page.lb_config_txt.coord.x, config_page.lb_config_txt.coord.y);
  }

  //////////////////////////////////////miner page layout///////////////////////////////////////////////
  //Hashrate value
  font_color = lv_color_hex(0xEE7D30);
  miner_page.lb_hashrate.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_hashrate.obj, 100);
  lv_label_set_text( miner_page.lb_hashrate.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_hashrate.obj, miner_page.lb_hashrate.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_hashrate.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_hashrate.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( miner_page.lb_hashrate.obj, LV_ALIGN_BOTTOM_MID, miner_page.lb_hashrate.coord.x, miner_page.lb_hashrate.coord.y);
  //Hit value
  font_color = lv_color_hex(0xEE7D30);
  miner_page.lb_blk_hit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_blk_hit.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_blk_hit.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, miner_page.lb_blk_hit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_blk_hit.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_blk_hit.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x, miner_page.lb_blk_hit.coord.y); 
  //price value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_price.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_price.obj , SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_price.obj , "");
  lv_obj_set_style_text_font(miner_page.lb_price.obj , miner_page.lb_price.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_price.obj , LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_price.obj , font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_price.obj , LV_ALIGN_LEFT_MID, miner_page.lb_price.coord.x, miner_page.lb_price.coord.y ); 
  //version value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_ver.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_ver.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_ver.obj, board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
  lv_obj_set_style_text_font(miner_page.lb_ver.obj, miner_page.lb_ver.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_ver.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_ver.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_ver.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_ver.coord.x, miner_page.lb_ver.coord.y );
  //power value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_power.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) width = SCREEN_WIDTH/2.2;
  else   width = SCREEN_WIDTH/2.5;
  lv_obj_set_width(miner_page.lb_power.obj, width);
  lv_label_set_text( miner_page.lb_power.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_power.obj, miner_page.lb_power.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_power.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_power.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_power.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_power.coord.x, miner_page.lb_power.coord.y ); 
  //wifi value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_ip.obj    = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_ip.obj, SCREEN_WIDTH/2.5);
  lv_label_set_text( miner_page.lb_ip.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_ip.obj, miner_page.lb_ip.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_ip.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(miner_page.lb_ip.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_ip.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_ip.coord.x, miner_page.lb_ip.coord.y );

  //uptime value, hour , minute, second
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_uptime_hms.obj    = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_hms.obj, 88);
  lv_label_set_text( miner_page.lb_uptime_hms.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_uptime_hms.obj, miner_page.lb_uptime_hms.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_hms.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_hms.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_hms.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_hms.coord.x, miner_page.lb_uptime_hms.coord.y);
  //uptime value, day
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_uptime_day.obj    = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_day.obj, 88);
  lv_label_set_text( miner_page.lb_uptime_day.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_uptime_day.obj, miner_page.lb_uptime_day.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_day.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_day.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_day.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_day.coord.x, miner_page.lb_uptime_day.coord.y );
  //uptime day unit  
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_uptime_day_unit.obj    = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_day_unit.obj, 20);
  lv_label_set_text( miner_page.lb_uptime_day_unit.obj, "d");
  lv_obj_set_style_text_font(miner_page.lb_uptime_day_unit.obj, miner_page.lb_uptime_day_unit.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_day_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_day_unit.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_day_unit.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_day_unit.coord.x, miner_page.lb_uptime_day_unit.coord.y );

  //Diff value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_diff.obj    = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) width = SCREEN_WIDTH/2.6;
  else   width = SCREEN_WIDTH/2.4;
  lv_obj_set_width(miner_page.lb_diff.obj, width);
  lv_label_set_text( miner_page.lb_diff.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_diff.obj, miner_page.lb_diff.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_diff.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(miner_page.lb_diff.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_diff.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_diff.coord.x, miner_page.lb_diff.coord.y );
  //share value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_share.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_share.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_share.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_share.obj, miner_page.lb_share.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_share.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_share.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_share.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_share.coord.x, miner_page.lb_share.coord.y);
  //temp value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_temp.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_temp.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_temp.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_temp.obj, miner_page.lb_temp.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_temp.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_temp.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_temp.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_temp.coord.x, miner_page.lb_temp.coord.y );
  //Fan value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_fan.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_fan.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_fan.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_fan.obj, miner_page.lb_fan.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_fan.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_fan.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_fan.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_fan.coord.x, miner_page.lb_fan.coord.y); 
  //Hashrate uint
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_hr_unit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_hr_unit.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_hr_unit.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_hr_unit.obj, miner_page.lb_hr_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_hr_unit.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_hr_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_hr_unit.obj, LV_ALIGN_TOP_MID, miner_page.lb_hr_unit.coord.x, miner_page.lb_hr_unit.coord.y); 
  // symbol uptime
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_uptime_symbol.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_uptime_symbol.obj, LV_SYMBOL_BELL); 
  lv_obj_set_style_text_font(miner_page.lb_uptime_symbol.obj, miner_page.lb_uptime_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_uptime_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_uptime_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_uptime_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_uptime_symbol.coord.x, miner_page.lb_uptime_symbol.coord.y); 
  // symbol wifi
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_wifi_symbol.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_wifi_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_wifi_symbol.obj, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(miner_page.lb_wifi_symbol.obj, miner_page.lb_wifi_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_wifi_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_wifi_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_wifi_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_wifi_symbol.coord.x, miner_page.lb_wifi_symbol.coord.y); 

  //diff symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_diff_symbol.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_diff_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_diff_symbol.obj, "\xEF\x82\x80");
  lv_obj_set_style_text_font(miner_page.lb_diff_symbol.obj, miner_page.lb_diff_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_diff_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_diff_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_diff_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_diff_symbol.coord.x, miner_page.lb_diff_symbol.coord.y); 
  // share symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_share_symb.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_share_symb.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_share_symb.obj, "\xEF\x8E\x82");
  lv_obj_set_style_text_font(miner_page.lb_share_symb.obj, miner_page.lb_share_symb.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_share_symb.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_share_symb.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_share_symb.obj, LV_ALIGN_TOP_MID, miner_page.lb_share_symb.coord.x, miner_page.lb_share_symb.coord.y); 
  //temp symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_temp_symb.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_temp_symb.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_temp_symb.obj, "\xEF\x8B\x88");
  lv_obj_set_style_text_font(miner_page.lb_temp_symb.obj, miner_page.lb_temp_symb.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_temp_symb.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_temp_symb.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_temp_symb.obj, LV_ALIGN_TOP_MID, miner_page.lb_temp_symb.coord.x , miner_page.lb_temp_symb.coord.y); 
  //fan symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_fan_symb.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_fan_symb.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_fan_symb.obj, "\xEF\xA1\xA3");
  lv_obj_set_style_text_font(miner_page.lb_fan_symb.obj, miner_page.lb_fan_symb.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_fan_symb.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_fan_symb.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_fan_symb.obj, LV_ALIGN_TOP_MID, miner_page.lb_fan_symb.coord.x, miner_page.lb_fan_symb.coord.y);

  // only for NMQAxe++ board
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    // swarm best diff
    font_color = lv_color_hex(0xEE7D30);
    miner_page.lb_swarm_best_diff.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_best_diff.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_best_diff.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_best_diff.obj, miner_page.lb_swarm_best_diff.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_best_diff.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_best_diff.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_best_diff.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_best_diff.coord.x, miner_page.lb_swarm_best_diff.coord.y);
    // swarm workers
    font_color = lv_color_hex(0xEE7D30);
    miner_page.lb_swarm_workers.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_workers.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_workers.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_workers.obj, miner_page.lb_swarm_workers.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_workers.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_workers.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_workers.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_workers.coord.x, miner_page.lb_swarm_workers.coord.y);
    // swarm total hashrate
    font_color = lv_color_hex(0xEE7D30);
    miner_page.lb_swarm_total_hashrate.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_total_hashrate.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_total_hashrate.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_total_hashrate.obj, miner_page.lb_swarm_total_hashrate.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_total_hashrate.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_total_hashrate.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_total_hashrate.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_total_hashrate.coord.x, miner_page.lb_swarm_total_hashrate.coord.y);

    // utc time label
    font_color = lv_color_hex(0xFFFFFF);
    miner_page.lb_utc_time.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_utc_time.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_utc_time.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_utc_time.obj, miner_page.lb_utc_time.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_utc_time.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_utc_time.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_utc_time.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_utc_time.coord.x, miner_page.lb_utc_time.coord.y);
  }

  //////////////////////////////////////dashboard page layout///////////////////////////////////////////////
  // Hashrate label
  font_color = lv_color_hex(0x000000);
  dashboard_page.lb_hr.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_DASHBOARD] );
  lv_obj_set_width(dashboard_page.lb_hr.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( dashboard_page.lb_hr.obj, " ");
  lv_obj_set_style_text_font(dashboard_page.lb_hr.obj, dashboard_page.lb_hr.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(dashboard_page.lb_hr.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(dashboard_page.lb_hr.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( dashboard_page.lb_hr.obj, LV_ALIGN_TOP_MID, dashboard_page.lb_hr.coord.x, dashboard_page.lb_hr.coord.y);
  // hashrate unit label
  font_color = lv_color_hex(0x808080);
  dashboard_page.lb_hr_unit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_DASHBOARD] );
  lv_obj_set_width(dashboard_page.lb_hr_unit.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( dashboard_page.lb_hr_unit.obj, " ");
  lv_obj_set_style_text_font(dashboard_page.lb_hr_unit.obj, dashboard_page.lb_hr_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(dashboard_page.lb_hr_unit.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(dashboard_page.lb_hr_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( dashboard_page.lb_hr_unit.obj, LV_ALIGN_TOP_MID, dashboard_page.lb_hr_unit.coord.x, dashboard_page.lb_hr_unit.coord.y);
  // miner img
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    // miner image
    dashboard_page.img_miner.obj = lv_img_create(board->status.ui.page.list[UI_PAGE_DASHBOARD]);
    lv_img_set_src(dashboard_page.img_miner.obj, dashboard_page.miner_img_dsc);
    lv_obj_align(dashboard_page.img_miner.obj, LV_ALIGN_CENTER, dashboard_page.img_miner.coord.x, dashboard_page.img_miner.coord.y);
    lv_obj_add_flag(dashboard_page.img_miner.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall

    // diff label
    dashboard_page.lb_diff.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_DASHBOARD] );
    font_color = lv_color_hex(0xFFFFFF);
    lv_obj_set_width(dashboard_page.lb_diff.obj, dashboard_page.miner_img_dsc->header.w);
    lv_label_set_text( dashboard_page.lb_diff.obj, "0.000");
    lv_obj_set_style_text_font(dashboard_page.lb_diff.obj, dashboard_page.lb_diff.font, LV_PART_MAIN);
    lv_obj_set_style_text_align(dashboard_page.lb_diff.obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(dashboard_page.lb_diff.obj, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(dashboard_page.lb_diff.obj, LV_LABEL_LONG_WRAP);
    lv_obj_align( dashboard_page.lb_diff.obj, LV_ALIGN_CENTER,  dashboard_page.img_miner.coord.x + dashboard_page.miner_img_dsc->header.w, 
                                                                dashboard_page.img_miner.coord.y +  dashboard_page.miner_img_dsc->header.h / 2 + 10);
  }
  //////////////////////////////////////hashrate healthy page layout///////////////////////////////////////////////
  // Hashrate label
  font_color = lv_color_hex(0x000000);
  hr_health_page.lb_hr.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_HR_HEALTH] );
  lv_obj_set_width(hr_health_page.lb_hr.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( hr_health_page.lb_hr.obj, " ");
  lv_obj_set_style_text_font(hr_health_page.lb_hr.obj, hr_health_page.lb_hr.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(hr_health_page.lb_hr.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(hr_health_page.lb_hr.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( hr_health_page.lb_hr.obj, LV_ALIGN_TOP_MID, hr_health_page.lb_hr.coord.x, hr_health_page.lb_hr.coord.y);
  // hashrate unit label
  font_color = lv_color_hex(0x808080);
  hr_health_page.lb_hr_unit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_HR_HEALTH] );
  lv_obj_set_width(hr_health_page.lb_hr_unit.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( hr_health_page.lb_hr_unit.obj, " ");
  lv_obj_set_style_text_font(hr_health_page.lb_hr_unit.obj, hr_health_page.lb_hr_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(hr_health_page.lb_hr_unit.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(hr_health_page.lb_hr_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( hr_health_page.lb_hr_unit.obj, LV_ALIGN_TOP_MID, hr_health_page.lb_hr_unit.coord.x, hr_health_page.lb_hr_unit.coord.y);
  // scale label
  font_color = lv_color_hex(0xFFA500);
  uint16_t SCALE = (board->info.spec.ui.hashrate_dist_page.max_x_hr / board->info.spec.ui.hashrate_dist_page.max_x_bars);
  String scale_str = "Scale : " + String(SCALE) + " GH/s";
  width = lv_txt_get_width(scale_str.c_str(), strlen(scale_str.c_str()), hr_health_page.lb_scale.font, 0, LV_TEXT_FLAG_NONE);
  hr_health_page.lb_scale.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_HR_HEALTH] );
  lv_obj_set_width(hr_health_page.lb_scale.obj, width);
  lv_label_set_text(hr_health_page.lb_scale.obj, scale_str.c_str());
  lv_obj_set_style_text_font(hr_health_page.lb_scale.obj, hr_health_page.lb_scale.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(hr_health_page.lb_scale.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(hr_health_page.lb_scale.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( hr_health_page.lb_scale.obj, LV_ALIGN_TOP_RIGHT, hr_health_page.lb_scale.coord.x, hr_health_page.lb_scale.coord.y);
  // Create a chart
  hr_health_page.total_hr_chart.obj = lv_chart_create(board->status.ui.page.list[UI_PAGE_HR_HEALTH]);
  lv_obj_add_flag(hr_health_page.total_hr_chart.obj, LV_OBJ_FLAG_EVENT_BUBBLE); // bubble swipe events up to parent_wall
  lv_obj_set_size(hr_health_page.total_hr_chart.obj, SCREEN_WIDTH - 14, SCREEN_HEIGHT - 48); 
  lv_obj_align(hr_health_page.total_hr_chart.obj, LV_ALIGN_CENTER, hr_health_page.total_hr_chart.coord.x, hr_health_page.total_hr_chart.coord.y);
  lv_chart_set_type(hr_health_page.total_hr_chart.obj, LV_CHART_TYPE_BAR);
  lv_chart_set_range(hr_health_page.total_hr_chart.obj, LV_CHART_AXIS_PRIMARY_X, 0, board->info.spec.ui.hashrate_dist_page.max_x_bars - 1); 
  lv_chart_set_range(hr_health_page.total_hr_chart.obj, LV_CHART_AXIS_PRIMARY_Y, 0, 100); 
  lv_chart_set_div_line_count(hr_health_page.total_hr_chart.obj, 5, 4);
  // Add a series to the chart
  hr_health_page.total_hr_chart.series = lv_chart_add_series(hr_health_page.total_hr_chart.obj, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(hr_health_page.total_hr_chart.obj, board->info.spec.ui.hashrate_dist_page.max_x_bars);
  lv_chart_set_all_value(hr_health_page.total_hr_chart.obj, hr_health_page.total_hr_chart.series, 0);
  lv_chart_set_axis_tick(hr_health_page.total_hr_chart.obj, LV_CHART_AXIS_PRIMARY_X, 1, 1, board->info.spec.ui.hashrate_dist_page.max_x_bars, 1, true, 25);
  lv_chart_set_axis_tick(hr_health_page.total_hr_chart.obj, LV_CHART_AXIS_PRIMARY_Y, 1, 2, 5, 1, true, 25);
  lv_obj_set_style_bg_opa(hr_health_page.total_hr_chart.obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_opa(hr_health_page.total_hr_chart.obj, LV_OPA_TRANSP, LV_PART_MAIN);
  // set grid style
  static lv_style_t style_grid;
  lv_style_init(&style_grid);
  lv_style_set_line_dash_width(&style_grid, 2); 
  lv_style_set_line_dash_gap(&style_grid, 4); 
  lv_style_set_line_opa(&style_grid, LV_OPA_50); 
  lv_obj_add_style(hr_health_page.total_hr_chart.obj, &style_grid, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(hr_health_page.total_hr_chart.obj, hr_health_page.total_hr_chart.font, LV_PART_TICKS); 
  ////////////////////////////////////////////big digit  page layout///////////////////////////////////////////////
  // Hashrate label
  font_color = lv_color_hex(0xEE7D30);
  clock_page.lb_hr.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_hr.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( clock_page.lb_hr.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_hr.obj, clock_page.lb_hr.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_hr.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(clock_page.lb_hr.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_hr.obj, LV_ALIGN_TOP_LEFT, clock_page.lb_hr.coord.x, clock_page.lb_hr.coord.y);
  // hashrate unit label
  font_color = lv_color_hex(0x808080);
  clock_page.lb_hr_unit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_hr_unit.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( clock_page.lb_hr_unit.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_hr_unit.obj, clock_page.lb_hr_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_hr_unit.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_hr_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_hr_unit.obj, LV_ALIGN_TOP_LEFT, clock_page.lb_hr_unit.coord.x, clock_page.lb_hr_unit.coord.y);
  // block hit label
  font_color = lv_color_hex(0xEE7D30);
  clock_page.lb_hits.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_hits.obj, 75);
  lv_label_set_text( clock_page.lb_hits.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_hits.obj, clock_page.lb_hits.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_hits.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_hits.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_hits.obj, LV_ALIGN_TOP_LEFT, clock_page.lb_hits.coord.x, clock_page.lb_hits.coord.y);
  // block hit unit label
  font_color = lv_color_hex(0x808080);
  clock_page.lb_hits_unit.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_hits_unit.obj, 50);
  lv_label_set_text( clock_page.lb_hits_unit.obj, "hits");
  lv_obj_set_style_text_font(clock_page.lb_hits_unit.obj, clock_page.lb_hits_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_hits_unit.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_hits_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_hits_unit.obj, LV_ALIGN_TOP_LEFT, clock_page.lb_hits_unit.coord.x, clock_page.lb_hits_unit.coord.y);
  // time label
  font_color = lv_color_hex(0xFFFFFF);
  clock_page.lb_time.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_time.obj, SCREEN_WIDTH);
  lv_label_set_text( clock_page.lb_time.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_time.obj, clock_page.lb_time.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_time.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_time.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_time.obj, LV_ALIGN_CENTER, clock_page.lb_time.coord.x, clock_page.lb_time.coord.y);
  // date label
  font_color = lv_color_hex(0x808080);
  clock_page.lb_date.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_date.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( clock_page.lb_date.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_date.obj, clock_page.lb_date.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_date.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_date.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_date.obj, LV_ALIGN_BOTTOM_RIGHT, clock_page.lb_date.coord.x, clock_page.lb_date.coord.y);
  // AM PM 
  font_color = lv_color_hex(0x808080);
  clock_page.lb_ampm.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_ampm.obj, 50);
  lv_label_set_text( clock_page.lb_ampm.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_ampm.obj, clock_page.lb_ampm.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_ampm.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_ampm.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_ampm.obj, LV_ALIGN_CENTER, clock_page.lb_ampm.coord.x, clock_page.lb_ampm.coord.y);
  // price label
  font_color = lv_color_hex(0x808080);
  clock_page.lb_price.obj   = lv_label_create( board->status.ui.page.list[UI_PAGE_CLOCK] );
  lv_obj_set_width(clock_page.lb_price.obj, SCREEN_WIDTH / 2);
  lv_label_set_text( clock_page.lb_price.obj, " ");
  lv_obj_set_style_text_font(clock_page.lb_price.obj, clock_page.lb_price.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(clock_page.lb_price.obj, font_color, LV_PART_MAIN);
  lv_label_set_long_mode(clock_page.lb_price.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( clock_page.lb_price.obj, LV_ALIGN_BOTTOM_LEFT, clock_page.lb_price.coord.x, clock_page.lb_price.coord.y);
}

void ui_loading_page_update(void* args) {
  board_sal_t *board = (board_sal_t*)args;

  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static lv_coord_t width = 0;
  // ip address display instead of slogan after wifi connected
  if((WL_CONNECTED == board->status.wifi.status) && (loading_page.lb_ip_and_slogan.obj != NULL)){
    String ip_str = board->status.wifi.ip.toString();
    width = lv_txt_get_width(ip_str.c_str(), strlen(ip_str.c_str()), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(loading_page.lb_ip_and_slogan.obj, width);
    lv_obj_set_style_text_color(loading_page.lb_ip_and_slogan.obj, lv_color_hex(0x00FF00), LV_PART_MAIN); 
    lv_label_set_text(loading_page.lb_ip_and_slogan.obj, board->status.wifi.ip.toString().c_str());
  }

  // pool url update
  EventBits_t bits = xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_VCORE_READY, pdFALSE, pdTRUE, 0);
  if((loading_page.lb_pool_url.obj != NULL) && (bits & INIT_EVENT_VCORE_READY) == INIT_EVENT_VCORE_READY){
    String pool_str = (board->info.connection.pool.use.url + ":" + board->info.connection.pool.use.port);
    width = lv_txt_get_width(pool_str.c_str(), strlen(pool_str.c_str()), loading_page.lb_pool_url.font, 0, LV_TEXT_FLAG_NONE);
    width = (width > SCREEN_WIDTH) ? SCREEN_WIDTH : width;
    lv_obj_set_width(loading_page.lb_pool_url.obj, width);
    lv_label_set_text(loading_page.lb_pool_url.obj, (board->info.connection.pool.use.url + ":" + board->info.connection.pool.use.port).c_str());
  }

  // loading progress update
  if((loading_page.bar_progress.obj != NULL) && (loading_page.lb_progress.obj != NULL)){
    // Smooth lerp toward target percent (UI thread runs at ~50ms, lerp factor 0.4 ≈ 0.1~0.3s transition)
    static float display_percent = 0.0f;
    float target = board->status.ui.page.loading.percent;
    float delta  = target - display_percent;
    if (fabsf(delta) > 0.001f) {
      display_percent += delta * 0.4f;
    } else {
      display_percent = target;
    }

    int32_t max_value = lv_bar_get_max_value(loading_page.bar_progress.obj); // max value of the bar
    lv_coord_t bar_x = lv_obj_get_x(loading_page.bar_progress.obj);
    lv_coord_t bar_w = lv_obj_get_width(loading_page.bar_progress.obj);
    lv_coord_t label_x = bar_x + (bar_w * display_percent) - lv_obj_get_width(loading_page.lb_progress.obj) / 2;
    lv_obj_set_pos(loading_page.lb_progress.obj, label_x, -10);
    lv_label_set_text(loading_page.lb_progress.obj, (String(100 * display_percent, 0) + "%").c_str());
    lv_bar_set_value(loading_page.bar_progress.obj, (int32_t)(max_value * display_percent), LV_ANIM_OFF);

    if(display_percent >= 1.0f){
      delay(5); // keep 100% for a while to let user see the completed progress
      xEventGroupSetBits(board->status.init_evt, INIT_EVENT_MINER_READY);   
    }
  }

  // loading details update
  if(loading_page.lb_details.obj != NULL){
    lv_color_t color = lv_color_hex(board->status.ui.page.loading.details.color);
    lv_obj_set_style_text_color(loading_page.lb_details.obj, color, LV_PART_MAIN);
    lv_label_set_text(loading_page.lb_details.obj, board->status.ui.page.loading.details.msg.c_str());
  }
}

void ui_config_page_update(void* args) {
  board_sal_t *board = (board_sal_t*)args;
  if(!board){ LOG_E("board is null\r\n"); return; }

  // ── Countdown label: refresh every second for all boards ──────────────────────────────
  static uint32_t last_timeout_ms = millis();
  static uint8_t  cnt_anim        = 0;
  if(millis() - last_timeout_ms >= 1000){
    last_timeout_ms = millis();
    if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
      // Use LVGL display inactive time to detect real touch events.
      // This avoids the false-reset caused by ui_screen_saver_page_update()
      // continuously setting last_active_ms while INIT_EVENT_MINER_READY is not set.
      uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
      if(inactive_ms < 1500){
        // Fresh touch: extend the countdown back to its initial value
        board->status.wifi.config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
      } else if(board->status.wifi.config_timeout > 0){
        // UI thread owns the 1-second decrement for NMQAxe++ (thread skips it)
        board->status.wifi.config_timeout--;
      }
      lv_label_set_text_fmt(config_page.lb_cfg_timeout.obj, "%ds", board->status.wifi.config_timeout);
    } else {
      // NMAxe / NMAxeGamma: existing animated label + alignment logic
      String config_str[4] = {"config   ","config.  ","config.. ","config..."};
      String timeout = board->status.wifi.client_connected
                       ? config_str[cnt_anim++%4]
                       : (String(board->status.wifi.config_timeout) + "s");
      if(board->status.wifi.client_connected)
        lv_obj_align(config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, config_page.lb_cfg_timeout.coord.x - 10, config_page.lb_cfg_timeout.coord.y);
      else
        lv_obj_align(config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, config_page.lb_cfg_timeout.coord.x, config_page.lb_cfg_timeout.coord.y);
      lv_label_set_text(config_page.lb_cfg_timeout.obj, timeout.c_str());
    }
  }
  if(board->info.spec.name != BOARD_NMQAXE_PLUS_PLUS_NAME) return;

  // ════════════════════════════════════════════════════════════════════════════════════════
  // NMQAxe++ WiFi Setup state machine  (UI thread only — static locals, no extra mutex)
  //
  //  WsPhase::LIST       scan results list  +  refresh icon button (top-right)
  //  WsPhase::PWD_INPUT  modal dialog: password textarea + keyboard + Cancel/Save
  //  WsPhase::CONFIRM    modal dialog: confirm reboot + Cancel/Confirm
  //  WsPhase::SAVING     one-shot NVS write  →  reboot via daemon
  //
  //  AP mode is intentionally kept running as a secondary web-config path.
  //  Before scanning we upgrade WiFi mode to WIFI_AP_STA so both AP and STA are active.
  // ════════════════════════════════════════════════════════════════════════════════════════
  enum class WsPhase : uint8_t { LIST = 0, PWD_INPUT, CONFIRM, SAVING };

  static WsPhase   ws_phase       = WsPhase::LIST;
  static WsPhase   ws_last_phase  = (WsPhase)0xFF;   // force initial build
  static int16_t   ws_scan_status = -2;               // -2=idle  -1=running  >=0=count
  static String    ws_pending_ssid;
  static String    ws_pending_pwd;

  // Phase-level root container and per-phase widget handles
  static lv_obj_t *ws_cont      = nullptr;
  static lv_obj_t *ws_ssid_list = nullptr;
  static lv_obj_t *ws_scan_lbl  = nullptr;
  static lv_obj_t *ws_pwd_ta    = nullptr;
  static lv_obj_t *ws_kbd       = nullptr;
  static lv_obj_t *ws_key_popup[4] = {nullptr, nullptr, nullptr, nullptr};  // iPhone-style: top/bottom/left/right

  lv_obj_t *page = board->status.ui.page.list[UI_PAGE_CONFIG];

  // ── Phase transition: destroy stale container, build fresh UI for new phase ──────────
  if(ws_phase != ws_last_phase){
    // Delete keyboard first: in PWD_INPUT it is a direct child of page (not ws_cont),
    // so it won't be freed by lv_obj_del(ws_cont).
    if(ws_key_popup[0]||ws_key_popup[1]||ws_key_popup[2]||ws_key_popup[3]){
      for(int _i=0;_i<4;_i++){ if(ws_key_popup[_i]){ lv_obj_del(ws_key_popup[_i]); ws_key_popup[_i]=nullptr; } }
    }
    if(ws_kbd){ lv_obj_del(ws_kbd); ws_kbd = nullptr; }
    if(ws_cont){ lv_obj_del(ws_cont); ws_cont = nullptr; }
    ws_ssid_list = ws_scan_lbl = ws_pwd_ta = nullptr;
    ws_last_phase = ws_phase;

    // ──── LIST phase ──────────────────────────────────────────────────────────────────
    if(ws_phase == WsPhase::LIST){
      ws_cont = lv_obj_create(page);
      lv_obj_set_size(ws_cont, SCREEN_WIDTH, SCREEN_HEIGHT - 22);
      lv_obj_align(ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_cont, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_cont, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(ws_cont, 0, LV_PART_MAIN);
      lv_obj_clear_flag(ws_cont, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(ws_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

      // Title bar (26 px)
      lv_obj_t *title_bar = lv_obj_create(ws_cont);
      lv_obj_set_size(title_bar, SCREEN_WIDTH, 26);
      lv_obj_align(title_bar, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x16213E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(title_bar, 0, LV_PART_MAIN);
      lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(title_bar, LV_OBJ_FLAG_EVENT_BUBBLE);

      // Title label — centered
      lv_obj_t *lb_title = lv_label_create(title_bar);
      lv_label_set_text(lb_title, "WiFi Setup");
      lv_obj_set_style_text_font(lb_title, &lv_font_montserrat_16, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_align(lb_title, LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_flag(lb_title, LV_OBJ_FLAG_EVENT_BUBBLE);

      // Refresh icon button (↺, top-right of title bar)
      lv_obj_t *btn_refresh = lv_btn_create(title_bar);
      lv_obj_set_size(btn_refresh, 32, 24);
      lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -2, 0);
      lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x16213E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btn_refresh, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(btn_refresh, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(btn_refresh, 2, LV_PART_MAIN);
      lv_obj_t *lb_refresh_icon = lv_label_create(btn_refresh);
      lv_label_set_text(lb_refresh_icon, LV_SYMBOL_LOOP);
      lv_obj_set_style_text_color(lb_refresh_icon, lv_color_hex(0x00BFFF), LV_PART_MAIN);
      lv_obj_center(lb_refresh_icon);
      lv_obj_add_event_cb(btn_refresh, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          WiFi.scanDelete();
          WiFi.mode(WIFI_AP_STA);
          WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
          ws_scan_status = -1;
          if(ws_ssid_list) lv_obj_add_flag(ws_ssid_list, LV_OBJ_FLAG_HIDDEN);
          if(ws_scan_lbl){
            lv_obj_clear_flag(ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ws_scan_lbl, "Scanning...");
          }
      }, LV_EVENT_CLICKED, nullptr);

      // "Scanning..." placeholder label
      ws_scan_lbl = lv_label_create(ws_cont);
      lv_label_set_text(ws_scan_lbl, "Scanning...");
      lv_obj_set_style_text_font(ws_scan_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(ws_scan_lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
      lv_obj_align(ws_scan_lbl, LV_ALIGN_CENTER, 0, 0);

      // SSID scroll container (hidden until scan completes)
      ws_ssid_list = lv_obj_create(ws_cont);
      lv_obj_set_size(ws_ssid_list, SCREEN_WIDTH, SCREEN_HEIGHT - 22 - 26);
      lv_obj_align(ws_ssid_list, LV_ALIGN_TOP_LEFT, 0, 26);
      lv_obj_set_style_bg_color(ws_ssid_list, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_ssid_list, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_ssid_list, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(ws_ssid_list, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_row(ws_ssid_list, 3, LV_PART_MAIN);
      lv_obj_set_flex_flow(ws_ssid_list, LV_FLEX_FLOW_COLUMN);
      lv_obj_add_flag(ws_ssid_list, LV_OBJ_FLAG_HIDDEN);

      // Trigger initial async scan (upgrade mode so scan works while AP stays up)
      WiFi.scanDelete();
      WiFi.mode(WIFI_AP_STA);
      WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
      ws_scan_status = -1;

    // ──── PWD_INPUT phase ─────────────────────────────────────────────────────────────
    // Layout (top → bottom, all pixel values for 320×240 landscape display):
    //   ws_cont: top section (SSID label + textarea + buttons), height=top_h
    //   ws_kbd:  direct child of page below ws_cont, fills remaining height
    // Keyboard is placed on `page` (NOT inside ws_cont) so its full width is guaranteed
    // and there is zero overlap between the input area and the key rows.
    } else if(ws_phase == WsPhase::PWD_INPUT){
      const lv_coord_t usable_h = SCREEN_HEIGHT - 22;  // below status-bar area
      const lv_coord_t pad      = 6;
      const lv_coord_t inner_w  = SCREEN_WIDTH - pad * 2;
      const lv_coord_t btn_h    = 36;
      const lv_coord_t btn_w    = inner_w / 2 - 2;
      // ssid(18) + gap(4) + ta(32) + gap(4) + btn(36) + 2*pad(12) = 106
      const lv_coord_t ssid_h  = 18;
      const lv_coord_t ta_h    = 32;
      const lv_coord_t top_h   = pad + ssid_h + 4 + ta_h + 4 + btn_h + pad; // 106
      const lv_coord_t kbd_h   = usable_h - top_h;

      // Top container — SSID + textarea + buttons only, no keyboard inside
      ws_cont = lv_obj_create(page);
      lv_obj_set_size(ws_cont, SCREEN_WIDTH, top_h);
      lv_obj_align(ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_cont, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_cont, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(ws_cont, pad, LV_PART_MAIN);
      lv_obj_clear_flag(ws_cont, LV_OBJ_FLAG_SCROLLABLE);

      // Row 1: SSID label (y_offset=0 in content area)
      lv_obj_t *lb_ssid = lv_label_create(ws_cont);
      lv_label_set_text_fmt(lb_ssid, LV_SYMBOL_WIFI "  %s", ws_pending_ssid.c_str());
      lv_obj_set_style_text_font(lb_ssid, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_ssid, lv_color_hex(0x00BFFF), LV_PART_MAIN);
      lv_obj_set_width(lb_ssid, inner_w);
      lv_label_set_long_mode(lb_ssid, LV_LABEL_LONG_DOT);
      lv_obj_align(lb_ssid, LV_ALIGN_TOP_MID, 0, 0);

      // Row 2: Password textarea — full inner_w; eye icon overlaid inside right edge
      ws_pwd_ta = lv_textarea_create(ws_cont);
      lv_obj_set_size(ws_pwd_ta, inner_w, ta_h);
      lv_obj_align(ws_pwd_ta, LV_ALIGN_TOP_LEFT, 0, ssid_h + 4);
      lv_textarea_set_one_line(ws_pwd_ta, true);
      lv_textarea_set_password_mode(ws_pwd_ta, true);
      lv_textarea_set_max_length(ws_pwd_ta, 64);
      lv_textarea_set_placeholder_text(ws_pwd_ta, "Enter password...");
      lv_textarea_set_text(ws_pwd_ta, "");
      lv_obj_set_style_text_font(ws_pwd_ta, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(ws_pwd_ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_set_style_bg_color(ws_pwd_ta, lv_color_hex(0x0F3460), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_pwd_ta, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(ws_pwd_ta, lv_color_hex(0x00BFFF), LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_pwd_ta, 1, LV_PART_MAIN);
      // Right padding so typed text doesn't slide under the eye icon (26px icon + 4px gap)
      lv_obj_set_style_pad_right(ws_pwd_ta, 30, LV_PART_MAIN);
      lv_obj_clear_flag(ws_pwd_ta, LV_OBJ_FLAG_SCROLLABLE);

      // Eye button: overlaid inside the textarea on the right, transparent background
      lv_obj_t *btn_eye = lv_btn_create(ws_cont);
      lv_obj_set_size(btn_eye, 28, ta_h - 4);
      lv_obj_align(btn_eye, LV_ALIGN_TOP_RIGHT, -2, ssid_h + 4 + 2);
      lv_obj_set_style_bg_opa(btn_eye, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(btn_eye, 0, LV_PART_MAIN);
      lv_obj_set_style_border_width(btn_eye, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(btn_eye, 0, LV_PART_MAIN);
      lv_obj_t *lb_eye = lv_label_create(btn_eye);
      lv_label_set_text(lb_eye, LV_SYMBOL_EYE_OPEN);
      lv_obj_set_style_text_font(lb_eye, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_eye, lv_color_hex(0x888888), LV_PART_MAIN);
      lv_obj_center(lb_eye);
      lv_obj_add_event_cb(btn_eye, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          if(!ws_pwd_ta) return;
          lv_obj_t *btn  = lv_event_get_target(e);
          lv_obj_t *icon = lv_obj_get_child(btn, 0);
          bool pw_mode = lv_textarea_get_password_mode(ws_pwd_ta);
          lv_textarea_set_password_mode(ws_pwd_ta, !pw_mode);  // toggle
          // When revealed: bright blue eye-close icon; when hidden: grey eye-open icon
          if(icon) lv_label_set_text(icon, pw_mode ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
          lv_obj_set_style_text_color(icon,
              pw_mode ? lv_color_hex(0x00BFFF) : lv_color_hex(0x888888), LV_PART_MAIN);
      }, LV_EVENT_CLICKED, nullptr);

      // Row 3: Cancel + Save buttons (y_offset = ssid_h + gap + ta_h + gap = 58)
      const lv_coord_t btn_y = ssid_h + 4 + ta_h + 4;  // 58

      lv_obj_t *btn_cancel = lv_btn_create(ws_cont);
      lv_obj_set_size(btn_cancel, btn_w, btn_h);
      lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 0, btn_y);
      lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x555555), LV_PART_MAIN);
      lv_obj_t *lb_cancel = lv_label_create(btn_cancel);
      lv_label_set_text(lb_cancel, LV_SYMBOL_LEFT "  Cancel");
      lv_obj_set_style_text_font(lb_cancel, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_center(lb_cancel);
      lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          ws_pending_ssid = "";
          ws_pending_pwd  = "";
          ws_phase        = WsPhase::LIST;
      }, LV_EVENT_CLICKED, nullptr);

      lv_obj_t *btn_save = lv_btn_create(ws_cont);
      lv_obj_set_size(btn_save, btn_w, btn_h);
      lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, 0, btn_y);
      lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x007ACC), LV_PART_MAIN);
      lv_obj_t *lb_save = lv_label_create(btn_save);
      lv_label_set_text(lb_save, LV_SYMBOL_OK "  Save");
      lv_obj_set_style_text_font(lb_save, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_center(lb_save);
      lv_obj_add_event_cb(btn_save, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          if(ws_pwd_ta) ws_pending_pwd = String(lv_textarea_get_text(ws_pwd_ta));
          ws_phase = WsPhase::CONFIRM;
      }, LV_EVENT_CLICKED, nullptr);

      // Keyboard: direct child of page, placed immediately below ws_cont.
      // Being on the page level guarantees full SCREEN_WIDTH and no overlap with ws_cont.
      ws_kbd = lv_keyboard_create(page);
      lv_obj_set_size(ws_kbd, SCREEN_WIDTH, kbd_h);
      lv_obj_align(ws_kbd, LV_ALIGN_TOP_LEFT, 0, top_h);
      lv_keyboard_set_textarea(ws_kbd, ws_pwd_ta);
      lv_obj_set_style_bg_color(ws_kbd, lv_color_hex(0x16213E), LV_PART_MAIN);
      lv_obj_set_style_text_font(ws_kbd, &lv_font_montserrat_14, LV_PART_MAIN);
      // Block long-press repeat for all keys EXCEPT backspace (which should repeat).
      // PREPROCESS runs before the btnmatrix class event_cb, so stopping here prevents
      // VALUE_CHANGED from being sent and keeps NO_REPEAT reliable across mode switches.
      lv_obj_add_event_cb(ws_kbd, [](lv_event_t *e){
          lv_obj_t *kbd = lv_event_get_target(e);
          lv_btnmatrix_t *bm = (lv_btnmatrix_t *)kbd;
          uint16_t sel = bm->btn_id_sel;
          if(sel != LV_BTNMATRIX_BTN_NONE){
              const char *txt = lv_btnmatrix_get_btn_text(kbd, sel);
              if(txt && strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) return; // allow repeat
          }
          lv_event_stop_processing(e);
      }, (lv_event_code_t)(LV_EVENT_LONG_PRESSED_REPEAT | LV_EVENT_PREPROCESS), nullptr);
      // Enter key and √ key both send LV_EVENT_READY to the textarea.
      // Intercept it to trigger the same Save → CONFIRM flow as the Save button.
      lv_obj_add_event_cb(ws_pwd_ta, [](lv_event_t *e){
          if(ws_pwd_ta) ws_pending_pwd = String(lv_textarea_get_text(ws_pwd_ta));
          ws_phase = WsPhase::CONFIRM;
      }, LV_EVENT_READY, nullptr);

      // Four-bubble magnifier: top / bottom / left / right of the touch point.
      // No matter how the finger covers the key, at least one bubble stays visible.
      lv_obj_add_event_cb(ws_kbd, [](lv_event_t *e){
          lv_event_code_t code = lv_event_get_code(e);
          lv_obj_t       *kbd  = lv_event_get_target(e);
          // Helper that creates one bubble at absolute position (px, py).
          // Bubble is sized to tightly wrap the character — padding=0, font fills the box.
          auto make_bubble = [](const char *txt, lv_coord_t px, lv_coord_t py,
                                lv_coord_t pop_w, lv_coord_t pop_h) -> lv_obj_t* {
              lv_obj_t *b = lv_obj_create(lv_layer_top());
              lv_obj_set_size(b, pop_w, pop_h);
              lv_obj_set_pos(b, px, py);
              lv_obj_set_style_bg_color(b, lv_color_hex(0x00BFFF), LV_PART_MAIN);
              lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
              lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
              lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
              lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);  // no inner padding — char fills box
              lv_obj_set_style_shadow_width(b, 14, LV_PART_MAIN);
              lv_obj_set_style_shadow_color(b, lv_color_hex(0x0050A0), LV_PART_MAIN);
              lv_obj_set_style_shadow_opa(b, LV_OPA_70, LV_PART_MAIN);
              lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
              lv_obj_t *lbl = lv_label_create(b);
              lv_label_set_text(lbl, txt);
              lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
              lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
              // Scale the label to fill the bubble area
              lv_obj_set_size(lbl, pop_w, pop_h);
              lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
              lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
              return b;
          };
          if(code == LV_EVENT_PRESSED){
              uint16_t btn_id = lv_btnmatrix_get_selected_btn(kbd);
              if(btn_id == LV_BTNMATRIX_BTN_NONE) return;
              const char *txt = lv_btnmatrix_get_btn_text(kbd, btn_id);
              if(!txt || strlen(txt) != 1 || (uint8_t)txt[0] < 0x21) return;
              lv_indev_t *ev_indev = lv_event_get_indev(e);
              lv_point_t tp = {0, 0};
              if(ev_indev) lv_indev_get_point(ev_indev, &tp);
              // Delete any existing bubbles
              for(int i=0;i<4;i++){ if(ws_key_popup[i]){ lv_obj_del(ws_key_popup[i]); ws_key_popup[i]=nullptr; } }
              // pw/ph: bubble size — close to font_20 glyph size so char fills the box
              // gap: distance from touch centre to near edge of bubble — large enough that
              //      a typical fingertip (≈15–20 px radius) does not obscure any bubble.
              const lv_coord_t pw = 28, ph = 28, gap = 40;
              // Bubble positions: top, bottom, left, right
              // Each is clamped so it stays fully on-screen.
              lv_coord_t p0x = (lv_coord_t)(tp.x - pw/2),     p0y = (lv_coord_t)(tp.y - ph - gap);
              lv_coord_t p1x = (lv_coord_t)(tp.x - pw/2),     p1y = (lv_coord_t)(tp.y + gap);
              lv_coord_t p2x = (lv_coord_t)(tp.x - pw - gap), p2y = (lv_coord_t)(tp.y - ph/2);
              lv_coord_t p3x = (lv_coord_t)(tp.x + gap),      p3y = (lv_coord_t)(tp.y - ph/2);
              struct { lv_coord_t x, y; } pos[4] = {{p0x,p0y},{p1x,p1y},{p2x,p2y},{p3x,p3y}};
              for(int i=0;i<4;i++){
                  lv_coord_t cx = pos[i].x, cy = pos[i].y;
                  if(cx < 0) cx = 0;
                  if(cx + pw > SCREEN_WIDTH)  cx = SCREEN_WIDTH  - pw;
                  if(cy < 0) cy = 0;
                  if(cy + ph > SCREEN_HEIGHT) cy = SCREEN_HEIGHT - ph;
                  ws_key_popup[i] = make_bubble(txt, cx, cy, pw, ph);
              }
          } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST){
              for(int i=0;i<4;i++){ if(ws_key_popup[i]){ lv_obj_del(ws_key_popup[i]); ws_key_popup[i]=nullptr; } }
          }
      }, LV_EVENT_ALL, nullptr);

    // ──── CONFIRM phase: reboot confirmation dialog ───────────────────────────────────
    } else if(ws_phase == WsPhase::CONFIRM){
      // Dim overlay behind the dialog
      ws_cont = lv_obj_create(page);
      lv_obj_set_size(ws_cont, SCREEN_WIDTH, SCREEN_HEIGHT - 22);
      lv_obj_align(ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(ws_cont, lv_color_hex(0x000000), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_cont, LV_OPA_70, LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_cont, 0, LV_PART_MAIN);
      lv_obj_clear_flag(ws_cont, LV_OBJ_FLAG_SCROLLABLE);

      // Dialog card (centered)
      const lv_coord_t dlg_w = SCREEN_WIDTH  - 24;
      const lv_coord_t dlg_h = 150;
      lv_obj_t *dlg = lv_obj_create(ws_cont);
      lv_obj_set_size(dlg, dlg_w, dlg_h);
      lv_obj_align(dlg, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_bg_color(dlg, lv_color_hex(0x16213E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(dlg, lv_color_hex(0x007ACC), LV_PART_MAIN);
      lv_obj_set_style_border_width(dlg, 1, LV_PART_MAIN);
      lv_obj_set_style_radius(dlg, 6, LV_PART_MAIN);
      lv_obj_set_style_pad_all(dlg, 10, LV_PART_MAIN);
      lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

      // Message
      lv_obj_t *lb_msg = lv_label_create(dlg);
      lv_label_set_text_fmt(lb_msg,
          "Connect to \"%s\"?\nDevice will reboot.", ws_pending_ssid.c_str());
      lv_obj_set_style_text_font(lb_msg, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_msg, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_set_style_text_align(lb_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_set_width(lb_msg, dlg_w - 20);
      lv_label_set_long_mode(lb_msg, LV_LABEL_LONG_WRAP);
      lv_obj_align(lb_msg, LV_ALIGN_TOP_MID, 0, 0);

      const lv_coord_t btn_w = (dlg_w - 20 - 8) / 2;
      const lv_coord_t btn_h = 36;

      // Cancel button
      lv_obj_t *btn_cancel = lv_btn_create(dlg);
      lv_obj_set_size(btn_cancel, btn_w, btn_h);
      lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
      lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x555555), LV_PART_MAIN);
      lv_obj_t *lb_cancel = lv_label_create(btn_cancel);
      lv_label_set_text(lb_cancel, "Cancel");
      lv_obj_set_style_text_font(lb_cancel, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_center(lb_cancel);
      lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          ws_phase = WsPhase::PWD_INPUT;  // go back to password entry
      }, LV_EVENT_CLICKED, nullptr);

      // Confirm button
      lv_obj_t *btn_confirm = lv_btn_create(dlg);
      lv_obj_set_size(btn_confirm, btn_w, btn_h);
      lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
      lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x00AA55), LV_PART_MAIN);
      lv_obj_t *lb_confirm = lv_label_create(btn_confirm);
      lv_label_set_text(lb_confirm, LV_SYMBOL_OK "  Confirm");
      lv_obj_set_style_text_font(lb_confirm, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_center(lb_confirm);
      lv_obj_add_event_cb(btn_confirm, [](lv_event_t *e){
          if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          ws_phase = WsPhase::SAVING;
      }, LV_EVENT_CLICKED, nullptr);

    // ──── SAVING phase ────────────────────────────────────────────────────────────────
    } else if(ws_phase == WsPhase::SAVING){
      // Persist credentials to NVS then trigger reboot via daemon thread
      nvs_config_set_string(NVS_CONFIG_WIFI_SSID, ws_pending_ssid.c_str());
      nvs_config_set_string(NVS_CONFIG_WIFI_PASS, ws_pending_pwd.c_str());
      LOG_I("WiFi credentials saved: SSID=%s", ws_pending_ssid.c_str());

      ws_cont = lv_obj_create(page);
      lv_obj_set_size(ws_cont, SCREEN_WIDTH, SCREEN_HEIGHT - 22);
      lv_obj_align(ws_cont, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_style_bg_color(ws_cont, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(ws_cont, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(ws_cont, 0, LV_PART_MAIN);
      lv_obj_clear_flag(ws_cont, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *lb_ok = lv_label_create(ws_cont);
      lv_label_set_text(lb_ok, "Saved!\nRebooting...");
      lv_obj_set_style_text_font(lb_ok, &lv_font_montserrat_16, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_ok, lv_color_hex(0x00FF88), LV_PART_MAIN);
      lv_obj_set_style_text_align(lb_ok, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_center(lb_ok);

      reboot_intent_set(REBOOT_INTENT_LCD_WIFI_SAVED,
                        "saved SSID=%s via on-screen wizard",
                        ws_pending_ssid.c_str());
      xSemaphoreGive(board->status.reboot_xsem);
    }
  }

  // ── Per-frame poll: collect WiFi scan results while in LIST phase ─────────────────────
  if(ws_phase == WsPhase::LIST && ws_scan_status == -1){
    int16_t n = (int16_t)WiFi.scanComplete();
    if(n >= 0){
      ws_scan_status = n;
      if(ws_scan_lbl)  lv_obj_add_flag(ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
      if(ws_ssid_list) lv_obj_clear_flag(ws_ssid_list, LV_OBJ_FLAG_HIDDEN);
      if(ws_ssid_list) lv_obj_clean(ws_ssid_list);
      for(int16_t i = 0; i < n; i++){
        String ssid = WiFi.SSID(i);
        if(ssid.length() == 0) continue;
        int32_t rssi = WiFi.RSSI(i);
        // List item btn: [child0: icon_lbl, child1: ssid_lbl, child2: rssi_lbl]
        // ws_pending_ssid is set from child1 (pure SSID, no RSSI suffix)
        lv_obj_t *btn = lv_btn_create(ws_ssid_list);
        lv_obj_set_size(btn, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 4, LV_PART_MAIN);
        // child 0: wifi icon
        lv_obj_t *icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0x00BFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 0, 0);
        // child 1: ssid text (pure SSID, used for ws_pending_ssid)
        lv_obj_t *ssid_lbl = lv_label_create(btn);
        lv_label_set_text(ssid_lbl, ssid.c_str());
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid_lbl, SCREEN_WIDTH - 22 - 60);  // leave room for rssi
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 22, 0);
        // child 2: rssi label (right side)
        lv_obj_t *rssi_lbl = lv_label_create(btn);
        lv_label_set_text_fmt(rssi_lbl, "%ddBm", (int)rssi);
        lv_obj_set_style_text_color(rssi_lbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_align(rssi_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *e){
            if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            lv_obj_t *b = lv_event_get_target(e);
            lv_obj_t *lbl = lv_obj_get_child(b, 1); // child 1 = pure SSID label
            if(lbl) ws_pending_ssid = String(lv_label_get_text(lbl));
            ws_phase = WsPhase::PWD_INPUT;
        }, LV_EVENT_CLICKED, nullptr);
      }
      if(n == 0 && ws_scan_lbl){
        lv_obj_clear_flag(ws_scan_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ws_scan_lbl, "No networks found");
      }
      WiFi.scanDelete();
    }
  }
}

void ui_miner_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static uint32_t last_update = 0;
  if(millis() - last_update < 1000) return;

  lv_color_t font_color = lv_color_hex(0xFFFFFF);
  String uptime = convert_uptime_to_string(board->status.miner.uptime_session);
  String hashrate = formatNumber(board->status.miner.hashrate._3m, 3);
  String hashuint = (board->status.miner.hashrate._3m > 0) ? (String(hashrate.charAt(hashrate.length() - 1)) + "H/s") : "";
  String last_diff = formatNumber(board->status.miner.diff.last, 1);
  String best_session = formatNumber(board->status.miner.diff.best_session, 3);
  String best_ever = formatNumber(board->status.miner.diff.best_ever, 1);
  String network_diff = formatNumber(board->status.miner.diff.network, 3);
  String voltage = formatNumber(board->status.power.vbus/1000.0, 3);
  String power = formatNumber(board->status.power.vbus*board->status.power.ibus/1000.0/1000.0, 2);
  String price = (millis() - board->market->get_last_update() <= MINER_MARKET_CONNECT_TIMEOUT) ? formatNumber(board->market->get_main_pair().price, 6) : "";
  String fan_asic  = String(board->status.fan.list[0].rpm) ;
  String fan_vcore = String(board->status.fan.list[1].rpm) ;

  //diff symbol color update
  if(board->status.miner.diff.last != 0){//avoid the first time update
    if(board->status.miner.diff.last == board->status.miner.diff.best_session) font_color = lv_color_hex(0x00ff00);//green
    else if(board->status.miner.diff.last == board->status.miner.diff.best_ever) font_color = lv_color_hex(0xffa500);//yellow
    else font_color = lv_color_hex(0xA9A9A9);//gray
    lv_obj_set_style_text_color(miner_page.lb_diff_symbol.obj, font_color, LV_PART_MAIN); 

  }

  //temp symbol color update
  if(board->status.temp.vcore >= board->info.spec.pwr.temp_limit.high) font_color = lv_color_hex(0xff0000);//red
  else if(board->status.temp.vcore >= board->info.spec.pwr.temp_limit.medium) font_color = lv_color_hex(0xffa500);//yellow
  else font_color = lv_color_hex(0x00ff00);//green
  lv_obj_set_style_text_color(miner_page.lb_temp_symb.obj, font_color, LV_PART_MAIN); 

  //wifi rssi symbol color update
  if(board->status.wifi.rssi >= MINER_WIFI_RSSI_STRONG) font_color = lv_color_hex(0x00ff00);//green
  else if(board->status.wifi.rssi >= MINER_WIFI_RSSI_GOOD) font_color = lv_color_hex(0xffa500);//yellow
  else  font_color = lv_color_hex(0xff0000);//red
  lv_obj_set_style_text_color(miner_page.lb_wifi_symbol.obj, font_color, LV_PART_MAIN); 

  //share symbol color update
  static uint32_t last_share_cnt = board->status.miner.share_accepted;
  if(last_share_cnt != board->status.miner.share_accepted){
    font_color = lv_color_hex(0x00ff00);//green
    last_share_cnt = board->status.miner.share_accepted;
  }
  else font_color = lv_color_hex(0xA9A9A9);//gray
  lv_obj_set_style_text_color(miner_page.lb_share_symb.obj, font_color, LV_PART_MAIN);

  //fan symbol color update, blink
  static bool fan_color_update = false;
  if(board->status.fan.list[0].rpm > 0){
    if(fan_color_update)font_color = lv_color_hex(0xA9A9A9);
    else font_color = lv_color_hex(0x00ff00);
    fan_color_update =!fan_color_update;
  }
  else if(board->status.fan.list[0].rpm == 0) font_color = lv_color_hex(0xA9A9A9);//gray
  lv_obj_set_style_text_color(miner_page.lb_fan_symb.obj, font_color, LV_PART_MAIN);

  //price color update, blink
  if(millis() - board->market->get_last_update() <= MINER_MARKET_CONNECT_TIMEOUT){
    static float last_price = board->market->get_main_pair().price;
    if(last_price != board->market->get_main_pair().price){
      font_color = lv_color_hex(0x00ff00);//green
      last_price = board->market->get_main_pair().price;
    }
    else font_color = lv_color_hex(0xFFFFFF);//white

    lv_coord_t width = lv_txt_get_width(("$" + price).c_str(), strlen(("$" + price).c_str()), miner_page.lb_price.font, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(miner_page.lb_price.obj, width);
    lv_obj_set_style_text_color(miner_page.lb_price.obj, font_color, LV_PART_MAIN);
    lv_label_set_text_fmt(miner_page.lb_price.obj,  "$%s", price.c_str());
  }
  //hashrate
  lv_label_set_text_fmt(miner_page.lb_hashrate.obj, "%s", hashrate.substring(0, hashrate.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(miner_page.lb_hr_unit.obj, "%s", hashuint.c_str());

  //block hit
  if(board->status.miner.hits <= 9){
    lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, miner_page.lb_blk_hit.font, LV_PART_MAIN);
    lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x, miner_page.lb_blk_hit.coord.y);
  }else if (board->status.miner.hits <= 99){
    if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
      lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x, miner_page.lb_blk_hit.coord.y + 8);
      lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, &ds_digib_font_38, LV_PART_MAIN);
    }
    else {
      lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x + 2, miner_page.lb_blk_hit.coord.y + 11);
      lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, &ds_digib_font_28, LV_PART_MAIN);
    }
  }
  lv_label_set_text_fmt(miner_page.lb_blk_hit.obj, "%d", board->status.miner.hits);

  //version
#if HAS_VERSION_CHECK_FEATURE
  if(compareVersions(board->info.base.fw_version, board->info.base.fw_latest_release) == -1){
    static uint8_t version_cnt = 0;
    if(version_cnt++ % 2 == 0){
      font_color = lv_color_hex(0x00ff00);//green
      lv_obj_set_style_text_color(miner_page.lb_ver.obj, font_color, LV_PART_MAIN);
      lv_label_set_text_fmt(miner_page.lb_ver.obj, "%s", board->info.base.fw_latest_release.substring(1, board->info.base.fw_latest_release.length()).c_str());
      lv_label_set_text( miner_page.lb_ver.obj, board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
    }
    else{
      font_color = lv_color_hex(0xFFFFFF);//white
      lv_obj_set_style_text_color(miner_page.lb_ver.obj, font_color, LV_PART_MAIN);
      lv_label_set_text_fmt(miner_page.lb_ver.obj, "%s", board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
      lv_label_set_text( miner_page.lb_ver.obj, board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
    }
  }
#else
  font_color = lv_color_hex(0xFFFFFF);//white
  lv_obj_set_style_text_color(miner_page.lb_ver.obj, font_color, LV_PART_MAIN);
  lv_label_set_text_fmt(miner_page.lb_ver.obj, "%s", board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
  lv_label_set_text(miner_page.lb_ver.obj, board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
#endif


  //Diff
  // lv_label_set_text_fmt(miner_page.lb_diff.obj, "%s/%s/%s/%s", last_diff.c_str(), best_session.c_str(), best_ever.c_str(), network_diff.c_str());
  lv_label_set_text_fmt(miner_page.lb_diff.obj, "%s/%s", best_session.c_str(), network_diff.c_str());
  //Temp
  lv_label_set_text_fmt(miner_page.lb_temp.obj,   "%s'C/%s'C", formatNumber(board->status.temp.vcore, 2).c_str(), formatNumber(board->status.temp.asic, 2).c_str());
  //WiFi
  lv_label_set_text_fmt(miner_page.lb_ip.obj,   "%s", board->status.wifi.ip.toString().c_str());
  //uptime hms
  lv_label_set_text_fmt(miner_page.lb_uptime_hms.obj,    "%s", uptime.substring(5, uptime.length()).c_str());
  //uptime day
  lv_label_set_text_fmt(miner_page.lb_uptime_day.obj,    "%s", uptime.substring(0,3).c_str());
  //share
  lv_label_set_text_fmt(miner_page.lb_share.obj,  "%d/%d", board->status.miner.share_rejected, board->status.miner.share_accepted);
  //power
  lv_label_set_text_fmt(miner_page.lb_power.obj,  "%sV/%sW", voltage.c_str(), power.c_str()); 

  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME)
    //fan for vcore only show on NMQAXE ++
    lv_label_set_text_fmt(miner_page.lb_fan.obj, "%s/%s", fan_vcore.c_str(), fan_asic.c_str());
  else
    //fan for asic only one fan 
    lv_label_set_text_fmt(miner_page.lb_fan.obj, "%s rpm", fan_asic.c_str());
    

  // only for NMQ AXE ++
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    if(miner_page.lb_swarm_best_diff.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_best_diff.obj, "%s", formatNumber(board->status.swarm.best_session_bd, 4).c_str());
    }
    if(miner_page.lb_swarm_workers.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_workers.obj, "%d", board->status.swarm.total_workers);
    }
    if(miner_page.lb_swarm_total_hashrate.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_total_hashrate.obj, "%sH/s", formatNumber(board->status.swarm.total_hr, 2).c_str());
    }
    if(miner_page.lb_utc_time.obj != NULL){
      String utc_time = "";
      if(12 == board->status.time.format.time) {
        utc_time = convert_time_to_local_12h(board->status.time.utc).substring(11, 11 + 5);
      }
      else if(24 == board->status.time.format.time) {
        utc_time = convert_time_to_local_24h(board->status.time.utc).substring(11, 11 + 5);
      } 
      lv_label_set_text_fmt(miner_page.lb_utc_time.obj, "%s", utc_time.c_str());
    }
  }

  last_update = millis();
}

void ui_countdown_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static lv_obj_t * overlay = NULL, *lb_countdown = NULL, *lb_reminder = NULL;
  static lv_style_t style;
  static bool style_inited = false;

  if(TOUCH_LONGPRESS_EVT ==  board->status.touch.evt){
    //create style one time
    if(!style_inited) {
      lv_style_init(&style);
      lv_style_set_bg_color(&style, lv_color_black());
      lv_style_set_bg_opa(&style, LV_OPA_80); 
      lv_style_set_border_width(&style, 0); 
      lv_style_set_border_opa(&style, LV_OPA_TRANSP);
      style_inited = true;
    }
    if(overlay == NULL){
        //create overlay
        overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        //apply style
        lv_obj_add_style(overlay, &style, LV_PART_MAIN);
    }
    if(lb_countdown == NULL && overlay != NULL){
        //create countdown label
        lb_countdown = lv_label_create(overlay);
        lv_obj_set_style_text_font(lb_countdown, &ds_digib_font_120, LV_PART_MAIN);
        lv_obj_set_style_text_color(lb_countdown, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
        lv_obj_align(lb_countdown, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_foreground(overlay);  // bring to front
    }
    if(lb_reminder == NULL && overlay != NULL){
        //create reminder label
        String reminder_str = "Recover to factory settings...";
        const lv_font_t *font = &lv_font_montserrat_20;
        lv_coord_t width = lv_txt_get_width(reminder_str.c_str(), strlen(reminder_str.c_str()), font, 0, LV_TEXT_FLAG_NONE);
        lb_reminder = lv_label_create(overlay);
        lv_obj_set_width(lb_reminder, width > SCREEN_WIDTH ? SCREEN_WIDTH : width);
        lv_label_set_long_mode(lb_reminder, LV_LABEL_LONG_SCROLL);
        lv_obj_set_style_text_font(lb_reminder, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(lb_reminder, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
        lv_label_set_text(lb_reminder, reminder_str.c_str());
        lv_obj_align(lb_reminder, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_move_foreground(overlay);  // bring to front
        // initial countdown label update
        lv_label_set_text(lb_countdown, String(board->status.ui.page.countdown.timeout).c_str());
    }
  }
  else{
    if(lb_countdown != NULL){
      lv_obj_del(lb_countdown);
      lb_countdown = NULL;
    }
    if(lb_reminder != NULL){
      lv_obj_del(lb_reminder);
      lb_reminder = NULL;
    }
    if(overlay != NULL){
      lv_obj_del(overlay);
      overlay = NULL;
    }

    if((overlay == NULL) && (lb_countdown == NULL) && (lb_reminder == NULL)){
      return;
    } 
  }

  static uint32_t last = 0;
  if(millis() - last < 1000) return;
  last = millis();

  //update countdown label
  lv_label_set_text(lb_countdown, String(board->status.ui.page.countdown.timeout).c_str());
}

void ui_ota_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static lv_obj_t * overlay = NULL, *bar = NULL, *label_file = NULL, *label_progress = NULL;
  static char progress_text[10];
  static uint32_t dismiss_at = 0;

  if(!board->status.ota.running) {
    if(overlay != NULL) {
      if(dismiss_at == 0) {
        // First call after upload done: force-render 100%, then wait ~1.2s before dismissing
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
        lv_label_set_text(label_progress, "100%");
        lv_label_set_text(label_file, board->status.ota.filename.c_str());
        dismiss_at = millis() + 1200;
      } else if(millis() >= dismiss_at) {
        lv_obj_del(overlay);
        overlay = NULL;
        bar = NULL;
        label_file = NULL;
        label_progress = NULL;
        dismiss_at = 0;
      }
      // else: still in hold period, keep overlay visible
    } else {
      dismiss_at = 0;
    }
    return;
  }

  static uint32_t last_update = millis();
  // When progress just reached 100%, bypass the 1s throttle to render it immediately
  bool is_complete = (board->status.ota.progress >= 100);
  if(!is_complete && millis() - last_update < 1000) return;
  static lv_style_t style;
  if(overlay == NULL){
      //create overlay
      overlay = lv_obj_create(lv_scr_act());
      lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
      lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
      //create style
      lv_style_init(&style);
      lv_style_set_bg_color(&style, lv_color_black());
      lv_style_set_bg_opa(&style, LV_OPA_70); 
      lv_style_set_border_width(&style, 0); 
      lv_style_set_border_opa(&style, LV_OPA_TRANSP);
      //apply style
      lv_obj_add_style(overlay, &style, LV_PART_MAIN);

      //create bar
      bar = lv_bar_create(overlay);
      lv_bar_set_range(bar, 0, 100);
      lv_obj_set_size(bar, SCREEN_WIDTH * 0.81, 5);
      lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
      //create file name label
      label_file = lv_label_create(overlay);
      lv_obj_set_style_text_color(label_file, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
      lv_obj_align(label_file, LV_ALIGN_CENTER, 0, -30);
      //create progress label
      label_progress = lv_label_create(overlay);
      lv_obj_set_style_text_color(label_progress, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
      lv_obj_align(label_progress, LV_ALIGN_LEFT_MID, 0, 0); 

      lv_obj_move_foreground(overlay);  // bring to front
  }

  lv_coord_t bar_x = lv_obj_get_x(bar);
  lv_coord_t bar_w = lv_obj_get_width(bar);
  lv_coord_t label_x = bar_x + (bar_w * board->status.ota.progress / 100.0f) - lv_obj_get_width(label_progress)/2;
  //update progress label
  snprintf(progress_text, sizeof(progress_text), "%d%%", board->status.ota.progress);
  lv_obj_set_pos(label_progress, label_x, 10);
  lv_label_set_text(label_progress, progress_text);

  //update bar value
  lv_bar_set_value(bar, board->status.ota.progress, LV_ANIM_ON);
  lv_label_set_text(label_file, board->status.ota.filename.c_str());
  last_update = millis();
}

void ui_hits_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static lv_style_t style_overlay;
  static lv_obj_t *container = NULL, *back_img_obj = NULL;
  static lv_img_dsc_t *back_img_dsc = nullptr;
  static bool first_time = true;

  static uint32_t last_update = 0;
  if(millis() - last_update < 1000) return;


  if(first_time){
    if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
      back_img_dsc = &block_hits_page_img_240_320;
      first_time = false;
    }
    else if( board->info.spec.name == BOARD_NMAXE_GAMMA_NAME || board->info.spec.name == BOARD_NMAXE_NAME){
      back_img_dsc = &block_hits_page_img_135_240;
      first_time = false;
    }
    else{
      LOG_E("unsupported board for hits page\r\n");
      return;
    }
  }

  EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT, pdFALSE, pdTRUE, 0);
  if ((bits & SYS_EVENT_MINER_BLOCK_HIT ) != SYS_EVENT_MINER_BLOCK_HIT) {
    if(container != nullptr){
      lv_obj_del(container);  // deletes back_img_obj as child automatically
      container = nullptr;
      back_img_obj = nullptr;
    }
    return;
  }

  //create parent object
  if(container == nullptr){
    // create hits page
    container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);  // center align
    lv_obj_set_style_pad_all(container, 0, 0);  
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(container, pressed_event_cb, LV_EVENT_PRESSED, NULL);
    
    // create and configure style
    lv_style_init(&style_overlay);
    lv_style_set_bg_color(&style_overlay, lv_color_black());
    // lv_style_set_bg_opa(&style_overlay, LV_OPA_70);  // 70% opacity black background
    lv_style_set_border_width(&style_overlay, 0);
    lv_style_set_border_opa(&style_overlay, LV_OPA_TRANSP);
    
    // connect style to hits_page
    lv_obj_add_style(container, &style_overlay, LV_PART_MAIN);
    
    // create and configure image object
    back_img_obj = lv_img_create(container);
    lv_img_set_src(back_img_obj, back_img_dsc);
    lv_obj_set_size(back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(back_img_obj, LV_ALIGN_CENTER, 0, 0);
    
    // set image opacity
    lv_obj_set_style_img_opa(back_img_obj, LV_OPA_COVER, LV_PART_MAIN);
    // show overlay
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(container);  // bring to front
  }

  last_update = millis(); 
}

void ui_achieve_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static uint32_t last_update = 0;
  static bool     fading_out    = false;
  static uint32_t fade_start_ms = 0;
  // During fade-out: let the caller's 50ms loop drive each opacity step.
  // Normal operation: rate-limit to once per second.
  if(!fading_out && (millis() - last_update < 1000)) return;

  static ui_element_t lb_best_ever_diff = {}, lb_time_ago = {};
  static lv_style_t style_overlay;
  static lv_obj_t *container = NULL, *back_img_obj = NULL;
  static lv_img_dsc_t *back_img_dsc = nullptr;
  static bool first_time = true;
  static uint32_t achieve_time = 0;
  if(first_time){
    if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
      back_img_dsc = &new_achievement_page_img_240_320;
      first_time = false;
    }
    else if(board->info.spec.name == BOARD_NMAXE_GAMMA_NAME || board->info.spec.name == BOARD_NMAXE_NAME){
      back_img_dsc = &new_achievement_page_img_135_240;
      first_time = false;
    }
    else{
      LOG_E("unsupported board for achieve page\r\n");
      return;
    }
  }

  EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED, pdFALSE, pdTRUE, 0);
  if((bits & SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED ) == SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED){
    // achieve_time is set only when the container is first created (see below)
  }
  else{
    if(container != nullptr){
      if(!fading_out) {
        // Kick off a 1-second fade-out
        fading_out    = true;
        fade_start_ms = millis();
        lv_obj_set_style_opa(container, LV_OPA_COVER, LV_PART_MAIN);
      }
      uint32_t elapsed = millis() - fade_start_ms;
      if(elapsed >= 1000) {
        // Fade complete — delete the overlay
        lv_obj_del(container);
        container             = nullptr;
        back_img_obj          = nullptr;
        lb_best_ever_diff.obj = nullptr;
        lb_time_ago.obj       = nullptr;
        fading_out            = false;
      } else {
        // Linearly interpolate opacity: 255 → 0 over 1000 ms
        lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER - (uint32_t)(LV_OPA_COVER) * elapsed / 1000);
        lv_obj_set_style_opa(container, opa, LV_PART_MAIN);
      }
    } else {
      fading_out = false; // container already gone, keep state clean
    }
    last_update = millis();
    return;
  }

  // Event is active — if the fade was triggered but event came back, restore opacity
  if(fading_out) {
    fading_out = false;
    if(container != nullptr)
      lv_obj_set_style_opa(container, LV_OPA_COVER, LV_PART_MAIN);
  }
  //create parent object
  if(container == nullptr){
    achieve_time = millis();  // record when this achievement was first displayed
    // create new achievement page
    container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);  // center align
    lv_obj_set_style_pad_all(container, 0, 0);  
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(container, pressed_event_cb, LV_EVENT_PRESSED, NULL);
    
    // create and configure style
    lv_style_init(&style_overlay);
    lv_style_set_bg_color(&style_overlay, lv_color_black());
    // lv_style_set_bg_opa(&style_overlay, LV_OPA_70);  // 70% opacity black background
    lv_style_set_border_width(&style_overlay, 0);
    lv_style_set_border_opa(&style_overlay, LV_OPA_TRANSP);
    
    // connect style to new achievement page
    lv_obj_add_style(container, &style_overlay, LV_PART_MAIN);
    
    // create and configure image object
    back_img_obj = lv_img_create(container);
    lv_img_set_src(back_img_obj, back_img_dsc);
    lv_obj_set_size(back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(back_img_obj, LV_ALIGN_CENTER, 0, 0);
    
    // set image opacity
    lv_obj_set_style_img_opa(back_img_obj, LV_OPA_COVER, LV_PART_MAIN);
    // show overlay
    lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(container);  // bring to front
  }
  
  // for NMQ AXE ++
  if((board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) && container != nullptr){
    lb_best_ever_diff.coord = {0, 70};
    lb_best_ever_diff.font  = &Inconsolata_26;
    lb_time_ago.coord = {0, 105};
    lb_time_ago.font  = &Inconsolata_18;
  }
  // for NMAXE and NMAXE GAMMA
  else if((board->info.spec.name == BOARD_NMAXE_GAMMA_NAME || board->info.spec.name == BOARD_NMAXE_NAME) && container != nullptr){
    lb_best_ever_diff.coord = {0, 23};
    lb_best_ever_diff.font  = &Inconsolata_18;
    lb_time_ago.coord       = {0, 60};
    lb_time_ago.font        = &Inconsolata_16;
  }

  if(lb_best_ever_diff.obj == nullptr){
    lb_best_ever_diff.obj   = lv_label_create(container);
    lv_obj_set_style_text_color(lb_best_ever_diff.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(lb_best_ever_diff.obj, lb_best_ever_diff.coord.x, lb_best_ever_diff.coord.y);
    lv_obj_set_style_text_font(lb_best_ever_diff.obj, lb_best_ever_diff.font, LV_PART_MAIN);
    lv_obj_align(lb_best_ever_diff.obj, LV_ALIGN_CENTER, lb_best_ever_diff.coord.x, lb_best_ever_diff.coord.y);
    lv_label_set_text_fmt(lb_best_ever_diff.obj, "%s", formatNumber(board->status.miner.diff.best_ever, 4).c_str());
  }else{
    // update best ever diff value
    lv_label_set_text_fmt(lb_best_ever_diff.obj, "%s", formatNumber(board->status.miner.diff.best_ever, 4).c_str());
  }

  if(lb_time_ago.obj == nullptr){
    lb_time_ago.obj   = lv_label_create(container);
    lv_obj_set_style_text_color(lb_time_ago.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(lb_time_ago.obj, lb_time_ago.coord.x, lb_time_ago.coord.y);
    lv_obj_set_style_text_font(lb_time_ago.obj, lb_time_ago.font, LV_PART_MAIN);
    lv_label_set_text(lb_time_ago.obj, "Just now");
    lv_obj_align(lb_time_ago.obj, LV_ALIGN_CENTER, lb_time_ago.coord.x, lb_time_ago.coord.y);
  }else{
    uint32_t seconds_ago = (millis() - achieve_time) / 1000;
    String time_ago_str = convert_uptime_to_string(seconds_ago);
    time_ago_str = (seconds_ago < 30) ? "Just now" : time_ago_str + " ago";
    lv_label_set_text_fmt(lb_time_ago.obj, "%s", time_ago_str.c_str());
    lv_obj_align(lb_time_ago.obj, LV_ALIGN_CENTER, lb_time_ago.coord.x, lb_time_ago.coord.y);
  }
  last_update = millis();
}

void ui_dashboard_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static uint32_t last_update = millis();

  // draw rings if not created
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_oc.obj.arc == NULL)) {
    dashboard_page.ring_oc.obj        = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_oc.cfg);
  }
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_pwr.obj.arc == NULL)) {
    dashboard_page.ring_pwr.obj       = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_pwr.cfg);
  }
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_vc_req.obj.arc == NULL)) {
    dashboard_page.ring_vc_req.obj    = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_vc_req.cfg);
  }
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_vc_real.obj.arc == NULL)) {
    dashboard_page.ring_vc_real.obj   = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_vc_real.cfg);
  }
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_asic_temp.obj.arc == NULL)) {
    dashboard_page.ring_asic_temp.obj = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_asic_temp.cfg);
  }
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (dashboard_page.ring_vcore_temp.obj.arc == NULL) && (board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME)){ // only NMQAXE ++ has vcore temp ring
    dashboard_page.ring_vcore_temp.obj = ui_draw_ring(board->status.ui.page.list[UI_PAGE_DASHBOARD], &dashboard_page.ring_vcore_temp.cfg);
  }

  // only for NMQAXE ++
  if((board->status.ui.page.list[UI_PAGE_DASHBOARD] != NULL) && (board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME)){
    static float step = 0.0f;
    step += 0.01f;
    lv_coord_t last_x = sin(step) * (SCREEN_WIDTH / 2 - dashboard_page.miner_img_dsc->header.w / 2);
    if (dashboard_page.img_miner.obj != NULL){
      lv_obj_set_x(dashboard_page.img_miner.obj, last_x);
    }
    if(dashboard_page.lb_diff.obj != NULL) {
      String diff = formatNumber(board->status.miner.diff.last, 4) + "\r" + formatNumber(board->status.miner.diff.best_session, 4);
      lv_label_set_text_fmt(dashboard_page.lb_diff.obj, "%s", diff.c_str());
      lv_obj_set_x(dashboard_page.lb_diff.obj, last_x + 15);
    }
  }
  

  if(millis() - last_update < 1000) return;

  limited_data_f limited_freq_req       = {board->info.spec.ui.dashboard_page.performance.asic_freq_req.min, board->info.spec.ui.dashboard_page.performance.asic_freq_req.max};
  limited_data_f limited_power          = {board->info.spec.ui.dashboard_page.power.power.min, board->info.spec.ui.dashboard_page.power.power.max};
  limited_data_f limited_vcore_req      = {board->info.spec.ui.dashboard_page.performance.vcore_req.min, board->info.spec.ui.dashboard_page.performance.vcore_req.max};
  limited_data_f limited_vcore_measure  = {board->info.spec.ui.dashboard_page.performance.vcore_measure.min, board->info.spec.ui.dashboard_page.performance.vcore_measure.max};
  limited_data_f limited_vcore_temp     = {board->info.spec.ui.dashboard_page.heat.vcore.min, board->info.spec.ui.dashboard_page.heat.vcore.max};
  limited_data_f limited_asic_temp      = {board->info.spec.ui.dashboard_page.heat.asic.min, board->info.spec.ui.dashboard_page.heat.asic.max};

  // update angles
  uint16_t arc_angle_full = dashboard_page.ring_oc.cfg.angle_full;
  uint16_t oc_angle            = arc_angle_full * (board->info.spec.asic.req_frq - limited_freq_req.min) / (limited_freq_req.max - limited_freq_req.min); 
  uint16_t pwr_angle           = arc_angle_full * (board->status.power.vbus * board->status.power.ibus/1000.0/1000.0 - limited_power.min) / (limited_power.max - limited_power.min);
  uint16_t vcore_req_angle     = arc_angle_full * (board->info.spec.asic.req_vcore/1000.0 - limited_vcore_req.min) / (limited_vcore_req.max - limited_vcore_req.min); 
  uint16_t asic_temp_angle     = arc_angle_full * (board->status.temp.asic - limited_asic_temp.min) / (limited_asic_temp.max - limited_asic_temp.min);
  uint16_t vcore_temp_angle    = arc_angle_full * (board->status.temp.vcore - limited_vcore_temp.min) / (limited_vcore_temp.max - limited_vcore_temp.min);
  float limit = max((float)board->status.power.vcore / 1000.0f, limited_vcore_measure.min);
  uint16_t vcore_measure_angle = arc_angle_full * (limit - limited_vcore_measure.min) / (limited_vcore_measure.max - limited_vcore_measure.min);

  // update rings
  ui_update_ring(&dashboard_page.ring_oc.obj, oc_angle, String(board->info.spec.asic.req_frq) + "M");
  ui_update_ring(&dashboard_page.ring_pwr.obj, pwr_angle, formatNumber(board->status.power.vbus * board->status.power.ibus/1000.0/1000.0, 3) + "w");
  ui_update_ring(&dashboard_page.ring_vc_req.obj, vcore_req_angle, formatNumber(board->info.spec.asic.req_vcore/1000.0, 4) + "v");
  ui_update_ring(&dashboard_page.ring_vc_real.obj, vcore_measure_angle, formatNumber(board->status.power.vcore/1000.0, 4) + "v");
  ui_update_ring(&dashboard_page.ring_asic_temp.obj, asic_temp_angle, formatNumber(board->status.temp.asic, 2) + "°C");
  ui_update_ring(&dashboard_page.ring_vcore_temp.obj, vcore_temp_angle, formatNumber(board->status.temp.vcore, 2) + "°C");

  // update hashrate
  String hr_value = formatNumber(board->status.miner.hashrate._3m, 3);
  String hr_unit  = (board->status.miner.hashrate._3m > 0) ? (String(hr_value.charAt(hr_value.length() - 1)) + "H/s") : "";
  lv_label_set_text_fmt(dashboard_page.lb_hr.obj, "%s", hr_value.substring(0, hr_value.length() - 1).c_str());
  lv_label_set_text_fmt(dashboard_page.lb_hr_unit.obj, "%s", hr_unit.c_str());

  last_update = millis();
}

void ui_hr_healthy_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static double last_hashrate = 0;
  static uint32_t last_update = 0;

  if(millis() - last_update < 1000) return;
  if(last_hashrate == board->status.miner.hashrate._3m) return;
  last_hashrate = board->status.miner.hashrate._3m;

  // update hashrate distribution map
  for (int i = 0; i < board->info.spec.ui.hashrate_dist_page.max_x_bars; i++) {
    uint8_t y = board->info.spec.ui.hashrate_dist_page.dist_map[i];
    lv_chart_set_value_by_id(hr_health_page.total_hr_chart.obj, hr_health_page.total_hr_chart.series, i, y);
  }

  String hr = formatNumber(last_hashrate, 3);
  String hr_unit = (last_hashrate > 0) ? (String(hr.charAt(hr.length() - 1)) + "H/s") : "";
  //hashrate
  lv_label_set_text_fmt(hr_health_page.lb_hr.obj, "%s", hr.substring(0, hr.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(hr_health_page.lb_hr_unit.obj, "%s", hr_unit.c_str());

  // update pie chart for NMQ AXE++
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    // draw pie chart if not created
    if((board->status.ui.page.list[UI_PAGE_HR_HEALTH] != NULL) && (hr_health_page.asic_hr_chart.obj.arcs[0] == NULL)) {
      hr_health_page.asic_hr_chart.obj = ui_draw_pie_chart(board->status.ui.page.list[UI_PAGE_HR_HEALTH], 
                                                            hr_health_page.asic_hr_chart.center_x, 
                                                            hr_health_page.asic_hr_chart.center_y, 
                                                            hr_health_page.asic_hr_chart.radius, 
                                                            hr_health_page.asic_hr_chart.cfg, board->miner->get_asic_count());
    }
    // update pie chart angles
    if((hr_health_page.asic_hr_chart.obj.arcs[0] != NULL) && (board->status.miner.asic_rsp_counter.size() ==  board->miner->get_asic_count())){
      // Calculate new angles based on ASIC response counters
      uint64_t total = 0;
      for(auto &pair : board->status.miner.asic_rsp_counter) {
        total += pair.second;
      }
      
      uint16_t new_angles[board->miner->get_asic_count()] = {0};
      for(auto &pair : board->status.miner.asic_rsp_counter){
        float per = (total == 0) ? 0.25f : ((float)pair.second / (float)total);
        new_angles[pair.first] = (uint16_t)(per * 360.0f);
      }
      
      // Adjust the last angle to ensure the sum is exactly 360 degrees
      uint32_t total_angle = 0;
      for(uint8_t i = 0; i < board->miner->get_asic_count() - 1; i++){
        total_angle += new_angles[i];
      }
      new_angles[board->miner->get_asic_count() - 1] = (360 - total_angle) > 0 ? (360 - total_angle) : 0;

      // Update pie chart with new angles
      ui_update_pie_chart(&hr_health_page.asic_hr_chart.obj, new_angles);
    }
  }
  last_update = millis();
}

void ui_clock_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static uint32_t last_update = 0;
  if(millis() - last_update < 1000) return;

  String hr       = formatNumber(board->status.miner.hashrate._3m, 3);
  String hr_unit = (board->status.miner.hashrate._3m > 0) ? (String(hr.charAt(hr.length() - 1)) + "H/s") : "";
  //hashrate
  lv_label_set_text_fmt(clock_page.lb_hr.obj, "%s", hr.substring(0, hr.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(clock_page.lb_hr_unit.obj, "%s", hr_unit.c_str());

  String datetime, char11, day, hms, am_pm = "";
  uint8_t index;
  // utc += 60*60; //add 24 hours every refresh to reduce time conversion calls
  if(12 == board->status.time.format.time) {
    datetime = convert_time_to_local_12h(board->status.time.utc, board->status.time.format.date);
    char11 = datetime.substring(11, 12);//remove '0' for hour < 10
    index = (char11 == "0") ? 12:11;
    am_pm = datetime.substring(index + 5, index + 8); //include AM/PM
  }
  else if(24 == board->status.time.format.time) {
    datetime = convert_time_to_local_24h(board->status.time.utc, board->status.time.format.date);
    index = 11;//always 11 for 24h format
  } 
  hms = datetime.substring(index, index + 5); //only HH:MM
  day = datetime.substring(0, 10);            //only YYYY-MM-DD

  // time
  lv_coord_t width = lv_txt_get_width(hms.c_str(), strlen(hms.c_str()), clock_page.lb_time.font, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(clock_page.lb_time.obj, width);
  lv_label_set_text_fmt(clock_page.lb_time.obj, "%s", hms.c_str());

  // AM PM
  width = lv_txt_get_width(am_pm.c_str(), strlen(am_pm.c_str()), clock_page.lb_ampm.font, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(clock_page.lb_ampm.obj, width);
  lv_label_set_text_fmt(clock_page.lb_ampm.obj, "%s", am_pm.c_str());
  width = lv_txt_get_width(hms.c_str(), strlen(hms.c_str()), clock_page.lb_time.font, 0, LV_TEXT_FLAG_NONE);
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME)
    lv_obj_align(clock_page.lb_ampm.obj, LV_ALIGN_CENTER, width / 1.7, -32);
  else 
    lv_obj_align(clock_page.lb_ampm.obj, LV_ALIGN_CENTER, width / 1.5, 2);
  
  // date
  width = lv_txt_get_width(day.c_str(), strlen(day.c_str()), clock_page.lb_date.font, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(clock_page.lb_date.obj, width);
  lv_label_set_text_fmt(clock_page.lb_date.obj, "%s", day.c_str());

  //block hit
  if(board->status.miner.hits < 10) lv_obj_align( clock_page.lb_hits.obj, LV_ALIGN_TOP_RIGHT, -10, 0);
  else lv_obj_align( clock_page.lb_hits.obj, LV_ALIGN_TOP_RIGHT, -30, 0);
  lv_label_set_text_fmt(clock_page.lb_hits.obj, "%s", String(board->status.miner.hits).c_str());

  //price value 
  String price_value = (board->market->get_main_pair().price > 1.0) ? String(board->market->get_main_pair().price, 1) : String(board->market->get_main_pair().price, 6);
  lv_color_t font_color;
  static float last_price = board->market->get_main_pair().price;
  if(last_price != board->market->get_main_pair().price){
    font_color = lv_color_hex(0x00ff00);//green
    last_price = board->market->get_main_pair().price;
  }
  else font_color = lv_color_hex(0x808080);//white
  lv_obj_set_style_text_color(clock_page.lb_price.obj, font_color, LV_PART_MAIN);
  lv_label_set_text_fmt(clock_page.lb_price.obj, "$%s", price_value.c_str());
  last_update = millis();
}

void ui_market_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board || !board->market) return;

  static uint32_t last_update = 0;
  if(millis() - last_update < 1000) return;

  const auto& wl = board->market->get_sorted_watchlist();

  static bool     inited       = false;
  static uint16_t max_per_page = 0;
  static uint16_t total_pages  = 1;
  static uint16_t cur_page     = 0;
  static uint32_t last_switch  = 0;

  if(!inited){
    market_page.lb_names.clear();
    market_page.lb_dollar_signs.clear();
    market_page.lb_prices.clear();
    market_page.lb_changes.clear();

    const lv_font_t *font   = market_page.coin_font        ? market_page.coin_font        : &Inconsolata_16;
    const lv_font_t *hr_f   = market_page.lb_hr.font       ? market_page.lb_hr.font       : &ds_digib_font_20;
    const lv_font_t *unit_f = market_page.lb_hr_unit.font  ? market_page.lb_hr_unit.font  : &lv_font_montserrat_16;

    lv_coord_t font_h  = lv_font_get_line_height(font);
    lv_coord_t hr_h    = lv_font_get_line_height(hr_f);
    lv_coord_t avail_h = SCREEN_HEIGHT - 2 - hr_h - 4;
    max_per_page       = (avail_h / font_h);
    if(max_per_page == 0) max_per_page = 1;

    lv_coord_t dollar_x = market_page.col_dollar_x ? market_page.col_dollar_x : 48;
    lv_coord_t price_x  = market_page.col_price_x  ? market_page.col_price_x  : 60;
    lv_coord_t change_x = market_page.col_change_x ? market_page.col_change_x : 195;

    for(uint16_t i = 0; i < max_per_page; i++){
      lv_coord_t y = 2 + (lv_coord_t)(i * font_h);

      // Column 1: coin name (cyan)
      lv_obj_t *ln = lv_label_create(market_page.container);
      lv_obj_set_style_text_font(ln, font, 0);
      lv_obj_set_style_text_color(ln, lv_color_hex(0x00D7FF), 0);
      lv_obj_set_pos(ln, 3, y);
      lv_obj_set_width(ln, dollar_x - 6);
      lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
      lv_label_set_text(ln, "");
      market_page.lb_names.push_back(ln);

      // Column 2: $ sign (left-aligned, color follows price direction)
      lv_obj_t *ld = lv_label_create(market_page.container);
      lv_obj_set_style_text_font(ld, font, 0);
      lv_obj_set_style_text_color(ld, lv_color_hex(0x00FF7F), 0);
      lv_obj_set_pos(ld, dollar_x, y);
      lv_obj_set_width(ld, price_x - dollar_x - 1);
      lv_label_set_long_mode(ld, LV_LABEL_LONG_DOT);
      lv_label_set_text(ld, "$");
      market_page.lb_dollar_signs.push_back(ld);

      // Column 3: price number (right-aligned)
      lv_obj_t *lp = lv_label_create(market_page.container);
      lv_obj_set_style_text_font(lp, font, 0);
      lv_obj_set_style_text_color(lp, lv_color_hex(0x00FF7F), 0);
      lv_obj_set_pos(lp, price_x, y);
      lv_obj_set_width(lp, change_x - price_x - 2);
      lv_obj_set_style_text_align(lp, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(lp, LV_LABEL_LONG_DOT);
      lv_label_set_text(lp, "");
      market_page.lb_prices.push_back(lp);

      // Column 4: change % (right-aligned)
      lv_obj_t *lc = lv_label_create(market_page.container);
      lv_obj_set_style_text_font(lc, font, 0);
      lv_obj_set_style_text_color(lc, lv_color_hex(0x00FF7F), 0);
      lv_obj_set_pos(lc, change_x, y);
      lv_obj_set_width(lc, SCREEN_WIDTH - change_x - 2);
      lv_obj_set_style_text_align(lc, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(lc, LV_LABEL_LONG_DOT);
      lv_label_set_text(lc, "");
      market_page.lb_changes.push_back(lc);
    }

    // Bottom-right: hashrate unit label
    market_page.lb_hr_unit.obj = lv_label_create(market_page.container);
    lv_obj_set_style_text_font(market_page.lb_hr_unit.obj, unit_f, 0);
    lv_obj_set_style_text_color(market_page.lb_hr_unit.obj, lv_color_hex(0xFFFFFF), 0);
    lv_coord_t width = lv_txt_get_width("kH/s", strlen("kH/s"), unit_f, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(market_page.lb_hr_unit.obj, width + 5); // add some padding
    lv_label_set_text(market_page.lb_hr_unit.obj, "---");
    lv_obj_align(market_page.lb_hr_unit.obj, LV_ALIGN_BOTTOM_RIGHT, 0, -3);

    // Bottom-right: hashrate value label (right-aligned, sits just left of unit)
    market_page.lb_hr.obj = lv_label_create(market_page.container);
    lv_obj_set_style_text_font(market_page.lb_hr.obj, hr_f, 0);
    lv_obj_set_style_text_color(market_page.lb_hr.obj, lv_color_hex(0xEE7D30), 0);
    lv_obj_set_width(market_page.lb_hr.obj, 110);
    lv_obj_set_style_text_align(market_page.lb_hr.obj, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(market_page.lb_hr.obj, "--");
    lv_obj_align(market_page.lb_hr.obj, LV_ALIGN_BOTTOM_RIGHT, -width - 8, 0); // 8 px gap from unit label

    inited = true;
    last_switch = millis();

    // Bottom-left: firmware version label
    const lv_font_t *ver_f = market_page.lb_version.font ? market_page.lb_version.font : &lv_font_montserrat_10;
    market_page.lb_version.obj = lv_label_create(market_page.container);
    lv_obj_set_style_text_font(market_page.lb_version.obj, ver_f, 0);
    lv_obj_set_style_text_color(market_page.lb_version.obj, lv_color_hex(0x888888), 0);
    lv_label_set_text(market_page.lb_version.obj, "");
    lv_obj_align(market_page.lb_version.obj, LV_ALIGN_BOTTOM_LEFT,
                 market_page.lb_version.coord.x, market_page.lb_version.coord.y);
  }

  // Always refresh hashrate with unit conversion (3 significant figures)
  String hr_val  = formatNumber(board->status.miner.hashrate._3m, 3);
  String hr_unit = (board->status.miner.hashrate._3m > 0)
                   ? (String(hr_val.charAt(hr_val.length() - 1)) + "H/s") : "";
  lv_label_set_text(market_page.lb_hr.obj,      hr_val.substring(0, hr_val.length() - 1).c_str());
  lv_label_set_text(market_page.lb_hr_unit.obj, hr_unit.c_str());
  lv_label_set_text(market_page.lb_version.obj, board->info.base.fw_version.c_str());

  if(wl.empty()) return;

  // Calculate pagination
  size_t total_coins = wl.size();
  total_pages = (uint16_t)((total_coins + max_per_page - 1) / max_per_page);
  if(total_pages == 0) total_pages = 1;
  if(cur_page >= total_pages) cur_page = 0;

  // Advance iterator to the current page's first coin
  auto it = wl.cbegin();
  for(uint16_t skip = 0; skip < cur_page * max_per_page && it != wl.cend(); ++skip) ++it;

  for(uint16_t i = 0; i < max_per_page; i++){
    if(it == wl.cend()){
      if(i < market_page.lb_names.size())        lv_label_set_text(market_page.lb_names[i],        "");
      if(i < market_page.lb_dollar_signs.size()) lv_label_set_text(market_page.lb_dollar_signs[i], "");
      if(i < market_page.lb_prices.size())       lv_label_set_text(market_page.lb_prices[i],       "");
      if(i < market_page.lb_changes.size())      lv_label_set_text(market_page.lb_changes[i],      "");
      continue;
    }
    const String    &sym = it->first;   // e.g. "BTCUSDT"
    const CoinPrice &cp  = it->second;
    ++it;

    // Strip "USDT" suffix for display
    String name = sym.endsWith("USDT") ? sym.substring(0, sym.length() - 4) : sym;

    // Format price number (no $ prefix) with thousand separator
    String price_str;
    int decimals = (cp.price < 0.01f) ? 7 : (cp.price < 1.0f) ? 5 : 2;
    if(cp.price >= 1000.0f){
      int    int_part = (int)cp.price;
      float  dec_part = cp.price - int_part;
      String int_str  = String(int_part);
      String fmt_int  = "";
      int    len      = int_str.length();
      for(int j = 0; j < len; j++){
        if(j > 0 && (len - j) % 3 == 0) fmt_int += ",";
        fmt_int += int_str.charAt(j);
      }
      price_str = fmt_int + String(dec_part, decimals).substring(1);
    } else {
      price_str = String(cp.price, decimals);
    }

    uint32_t color      = (cp.change_pct >= 0.0f) ? 0x00FF7F : 0xFF4444;
    String   change_str = (cp.change_pct >= 0.0f ? "+" : "") + String(cp.change_pct, 1) + "%";

    if(i < market_page.lb_names.size())
      lv_label_set_text(market_page.lb_names[i], name.c_str());

    if(i < market_page.lb_dollar_signs.size()){
      lv_obj_set_style_text_color(market_page.lb_dollar_signs[i], lv_color_hex(color), 0);
      lv_label_set_text(market_page.lb_dollar_signs[i], "$");
    }
    if(i < market_page.lb_prices.size()){
      lv_obj_set_style_text_color(market_page.lb_prices[i], lv_color_hex(color), 0);
      lv_label_set_text(market_page.lb_prices[i], price_str.c_str());
    }
    if(i < market_page.lb_changes.size()){
      lv_obj_set_style_text_color(market_page.lb_changes[i], lv_color_hex(color), 0);
      lv_label_set_text(market_page.lb_changes[i], change_str.c_str());
    }
  }

  // Auto-advance page every 30 s when there are multiple pages
  if(total_pages > 1 && millis() - last_switch > 30000){
    cur_page    = (cur_page + 1) % total_pages;
    last_switch = millis();
  }

  last_update = millis();
}

void ui_setting_or_swarm_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  if((board->info.spec.name == BOARD_NMAXE_GAMMA_NAME) || (board->info.spec.name == BOARD_NMAXE_NAME)){
    static bool swarm_inited = false;
    if(!swarm_inited){
      swarm_inited = true;
      lv_obj_t *cont = setting_page.container;
      lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

      // ---- Title "SWARM" ----
      lv_obj_t *lb_title = lv_label_create(cont);
      lv_label_set_text(lb_title, "SWARM");
      lv_obj_set_style_text_font(lb_title, &Inconsolata_22, 0);
      lv_obj_set_style_text_color(lb_title, lv_color_hex(0xFFFFFF), 0);
      lv_obj_align(lb_title, LV_ALIGN_TOP_MID, 0, 2);

      const lv_coord_t W            = lv_disp_get_hor_res(NULL);
      const lv_coord_t right_pad    = 5;
      const lv_coord_t col1_x       = 5;
      const lv_coord_t col2_x       = 122;
      const lv_coord_t col_left_w   = col2_x - col1_x - 4;   // ~113px, left two cells
      const lv_coord_t right_col_w  = W - col2_x - right_pad; // ~113px, right two cells
      const lv_coord_t row1_lbl_y   = 26;
      const lv_coord_t row1_data_y  = 38;
      const lv_coord_t row2_lbl_y   = 74;
      const lv_coord_t row2_data_y  = 86;
      // ds_digib_font_32: line_height=31, base_line=6 → digit visual bottom = 31-6 = 25px from top
      // Inconsolata_16:   line_height=17, base_line=4 → cap  visual bottom = 17-4 = 13px from top
      // unit_y_off = 25 - 13 = 12, so both visual bottoms align
      const lv_coord_t unit_y_off   = 25 - 13;  // = 12

      // ---- Row1 Left: Workers (subtitle + value center-aligned in left column) ----
      lv_obj_t *lb_workers_lbl = lv_label_create(cont);
      lv_label_set_text(lb_workers_lbl, "Workers");
      lv_obj_set_style_text_font(lb_workers_lbl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_workers_lbl, lv_color_hex(0xAAAAAA), 0);
      lv_obj_set_width(lb_workers_lbl, col_left_w);
      lv_obj_set_style_text_align(lb_workers_lbl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(lb_workers_lbl, col1_x, row1_lbl_y);

      setting_page.lb_swarm_workers.obj = lv_label_create(cont);
      lv_label_set_text(setting_page.lb_swarm_workers.obj, "0");
      lv_obj_set_style_text_font(setting_page.lb_swarm_workers.obj, &ds_digib_font_32, 0);
      lv_obj_set_style_text_color(setting_page.lb_swarm_workers.obj, lv_color_hex(0xEE7D30), 0);
      lv_obj_set_width(setting_page.lb_swarm_workers.obj, col_left_w);
      lv_obj_set_style_text_align(setting_page.lb_swarm_workers.obj, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(setting_page.lb_swarm_workers.obj, col1_x, row1_data_y);

      // ---- Row1 Right: Hashrate (subtitle + value center-aligned; unit bottom-right in Inconsolata) ----
      lv_obj_t *lb_hash_lbl = lv_label_create(cont);
      lv_label_set_text(lb_hash_lbl, "Hashrate");
      lv_obj_set_style_text_font(lb_hash_lbl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_hash_lbl, lv_color_hex(0xAAAAAA), 0);
      lv_obj_set_width(lb_hash_lbl, right_col_w);
      lv_obj_set_style_text_align(lb_hash_lbl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(lb_hash_lbl, col2_x, row1_lbl_y);

      setting_page.lb_swarm_total_hr.obj = lv_label_create(cont);
      lv_label_set_text(setting_page.lb_swarm_total_hr.obj, "0.0");
      lv_obj_set_style_text_font(setting_page.lb_swarm_total_hr.obj, &ds_digib_font_32, 0);
      lv_obj_set_style_text_color(setting_page.lb_swarm_total_hr.obj, lv_color_hex(0xEE7D30), 0);
      lv_obj_set_width(setting_page.lb_swarm_total_hr.obj, right_col_w);
      lv_obj_set_style_text_align(setting_page.lb_swarm_total_hr.obj, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(setting_page.lb_swarm_total_hr.obj, col2_x, row1_data_y);

      // Unit label: Inconsolata_16, light gray, bottom-aligned with the data value
      lv_obj_t *lb_hr_unit = lv_label_create(cont);
      lv_label_set_text(lb_hr_unit, "H/s");
      lv_obj_set_style_text_font(lb_hr_unit, &Inconsolata_16, 0);
      lv_obj_set_style_text_color(lb_hr_unit, lv_color_hex(0xCCCCCC), 0);
      lv_obj_set_pos(lb_hr_unit, W - right_pad - 26 + 3, row1_data_y + unit_y_off - 1);

      // ---- Row2 Left: Best Session BD (subtitle + value center-aligned) ----
      lv_obj_t *lb_sess_lbl = lv_label_create(cont);
      lv_label_set_text(lb_sess_lbl, "Best Session");
      lv_obj_set_style_text_font(lb_sess_lbl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_sess_lbl, lv_color_hex(0xAAAAAA), 0);
      lv_obj_set_width(lb_sess_lbl, col_left_w);
      lv_obj_set_style_text_align(lb_sess_lbl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(lb_sess_lbl, col1_x, row2_lbl_y);

      setting_page.lb_swarm_best_sess_bd.obj = lv_label_create(cont);
      lv_label_set_text(setting_page.lb_swarm_best_sess_bd.obj, "0.0");
      lv_obj_set_style_text_font(setting_page.lb_swarm_best_sess_bd.obj, &ds_digib_font_32, 0);
      lv_obj_set_style_text_color(setting_page.lb_swarm_best_sess_bd.obj, lv_color_hex(0xEE7D30), 0);
      lv_obj_set_width(setting_page.lb_swarm_best_sess_bd.obj, col_left_w);
      lv_obj_set_style_text_align(setting_page.lb_swarm_best_sess_bd.obj, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(setting_page.lb_swarm_best_sess_bd.obj, col1_x, row2_data_y);

      // ---- Row2 Right: Best Ever BD (subtitle + value center-aligned) ----
      lv_obj_t *lb_ever_lbl = lv_label_create(cont);
      lv_label_set_text(lb_ever_lbl, "Best Ever");
      lv_obj_set_style_text_font(lb_ever_lbl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_ever_lbl, lv_color_hex(0xAAAAAA), 0);
      lv_obj_set_width(lb_ever_lbl, right_col_w);
      lv_obj_set_style_text_align(lb_ever_lbl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(lb_ever_lbl, col2_x, row2_lbl_y);

      setting_page.lb_swarm_best_ever_bd.obj = lv_label_create(cont);
      lv_label_set_text(setting_page.lb_swarm_best_ever_bd.obj, "0.0");
      lv_obj_set_style_text_font(setting_page.lb_swarm_best_ever_bd.obj, &ds_digib_font_32, 0);
      lv_obj_set_style_text_color(setting_page.lb_swarm_best_ever_bd.obj, lv_color_hex(0xEE7D30), 0);
      lv_obj_set_width(setting_page.lb_swarm_best_ever_bd.obj, right_col_w);
      lv_obj_set_style_text_align(setting_page.lb_swarm_best_ever_bd.obj, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(setting_page.lb_swarm_best_ever_bd.obj, col2_x, row2_data_y);
    }

    // ---- Refresh values ----
    if(setting_page.lb_swarm_workers.obj != NULL)
      lv_label_set_text_fmt(setting_page.lb_swarm_workers.obj, "%lu", (unsigned long)board->status.swarm.total_workers);
    if(setting_page.lb_swarm_total_hr.obj != NULL)
      lv_label_set_text_fmt(setting_page.lb_swarm_total_hr.obj, "%s", formatNumber(board->status.swarm.total_hr, 3).c_str());
    if(setting_page.lb_swarm_best_sess_bd.obj != NULL)
      lv_label_set_text_fmt(setting_page.lb_swarm_best_sess_bd.obj, "%s", formatNumber(board->status.swarm.best_session_bd, 4).c_str());
    if(setting_page.lb_swarm_best_ever_bd.obj != NULL)
      lv_label_set_text_fmt(setting_page.lb_swarm_best_ever_bd.obj, "%s", formatNumber(board->status.swarm.best_ever_bd, 4).c_str());
  }
  else if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    static bool     inited              = false;
    static uint32_t s_last_reload_at_ms = 0;
    if(!inited){
      // Enable vertical scrolling so all settings fit on the small screen
      lv_obj_set_scroll_dir(setting_page.container, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(setting_page.container, LV_SCROLLBAR_MODE_AUTO);

      const lv_coord_t W     = lv_disp_get_hor_res(NULL);
      const lv_coord_t pad   = 4;
      const lv_coord_t lbl_w = 64;   // label column width (fits "Frequency" in montserrat_12)
      const lv_coord_t ctrl_w = W - lbl_w - pad * 3 - 20; // control column width (trimmed ~2 char widths)
      const lv_coord_t ctrl_x = W - pad - ctrl_w;          // right-aligned start x, flush to screen right edge
      const lv_coord_t row_h = 28;   // row height
      const lv_coord_t drop_h = 28;   // dropdown row height (font 20px needs extra space)
      const lv_font_t *font  = &lv_font_montserrat_14;
      lv_coord_t y = pad;

      // ---- Row 1: Screen brightness (label + slider) ----
      setting_page.lb_brightness.obj = lv_label_create(setting_page.container);
      lv_label_set_text(setting_page.lb_brightness.obj, "Brightness");
      lv_obj_set_style_text_font(setting_page.lb_brightness.obj, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(setting_page.lb_brightness.obj, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(setting_page.lb_brightness.obj, pad, y + 9);

      setting_page.bar_brightness.obj = lv_slider_create(setting_page.container);
      lv_obj_set_size(setting_page.bar_brightness.obj, ctrl_w, 14);
      lv_obj_set_pos(setting_page.bar_brightness.obj, ctrl_x, y + 7);
      lv_slider_set_range(setting_page.bar_brightness.obj, 2, 100);
      lv_slider_set_value(setting_page.bar_brightness.obj, nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, board->info.spec.preference.screen.brightness), LV_ANIM_OFF);
      lv_obj_add_event_cb(setting_page.bar_brightness.obj, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
      y += row_h + pad;

      // ---- Row 2: Vcore (full row, label + wide dropdown) ----
      lv_obj_t *lb_vcore = lv_label_create(setting_page.container);
      lv_label_set_text(lb_vcore, "Vcore");
      lv_obj_set_style_text_font(lb_vcore, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_vcore, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(lb_vcore, pad, y + (drop_h - 14) / 2);

      setting_page.list_asic_vcore.obj = lv_dropdown_create(setting_page.container);
      lv_obj_set_size(setting_page.list_asic_vcore.obj, ctrl_w, drop_h);
      lv_obj_set_pos(setting_page.list_asic_vcore.obj, ctrl_x, y);
      lv_obj_set_style_text_font(setting_page.list_asic_vcore.obj, &lv_font_montserrat_20, LV_PART_MAIN);
      lv_obj_set_style_pad_top(setting_page.list_asic_vcore.obj, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(setting_page.list_asic_vcore.obj, 4, LV_PART_MAIN);
      {
        const std::vector<work_option_t>& vc_opts = board->info.spec.ui.setting_page.vc;
        String opts = "";
        uint16_t sel_idx = 0, idx = 0;
        uint16_t cur =   board->info.spec.asic.req_vcore;
        bool found = false;
        for (const auto& item : vc_opts) {
          if (opts.length() > 0) opts += "\n";
          opts += item.name;
          if (!found && item.value == cur) { sel_idx = idx; found = true; }
          idx++;
        }
        lv_dropdown_set_options(setting_page.list_asic_vcore.obj, opts.c_str());
        lv_dropdown_set_selected(setting_page.list_asic_vcore.obj, sel_idx);
        lv_obj_t *vc_list = lv_dropdown_get_list(setting_page.list_asic_vcore.obj);
        if(vc_list) lv_obj_set_style_text_font(vc_list, &lv_font_montserrat_20, LV_PART_MAIN);
      }
      y += drop_h + pad;

      // ---- Row 3: Frequency (full row, label + wide dropdown) ----
      lv_obj_t *lb_freq = lv_label_create(setting_page.container);
      lv_label_set_text(lb_freq, "Frequency");
      lv_obj_set_style_text_font(lb_freq, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_freq, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(lb_freq, pad, y + (drop_h - 14) / 2);

      setting_page.list_asic_freq.obj = lv_dropdown_create(setting_page.container);
      lv_obj_set_size(setting_page.list_asic_freq.obj, ctrl_w, drop_h);
      lv_obj_set_pos(setting_page.list_asic_freq.obj, ctrl_x, y);
      lv_obj_set_style_text_font(setting_page.list_asic_freq.obj, &lv_font_montserrat_20, LV_PART_MAIN);
      lv_obj_set_style_pad_top(setting_page.list_asic_freq.obj, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(setting_page.list_asic_freq.obj, 4, LV_PART_MAIN);
      {
        const std::vector<work_option_t>& oc_opts = board->info.spec.ui.setting_page.oc;
        String opts = "";
        uint16_t sel_idx = 0, idx = 0;
        uint16_t cur =   board->info.spec.asic.req_frq;
        bool found = false;
        for (const auto& item : oc_opts) {
          if (opts.length() > 0) opts += "\n";
          opts += item.name;
          if (!found && item.value == cur) { sel_idx = idx; found = true; }
          idx++;
        }
        lv_dropdown_set_options(setting_page.list_asic_freq.obj, opts.c_str());
        lv_dropdown_set_selected(setting_page.list_asic_freq.obj, sel_idx);
        lv_obj_t *freq_list = lv_dropdown_get_list(setting_page.list_asic_freq.obj);
        if(freq_list) lv_obj_set_style_text_font(freq_list, &lv_font_montserrat_20, LV_PART_MAIN);
      }
      y += drop_h + pad;

      // ---- Row 4: Screen Saver (label + wide dropdown) ----
      lv_obj_t *lb_saver = lv_label_create(setting_page.container);
      lv_label_set_text(lb_saver, "Screen Saver");
      lv_obj_set_style_text_font(lb_saver, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(lb_saver, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(lb_saver, pad, y + (drop_h - 14) / 2);

      setting_page.list_saver.obj = lv_dropdown_create(setting_page.container);
      lv_obj_set_size(setting_page.list_saver.obj, ctrl_w, drop_h);
      lv_obj_set_pos(setting_page.list_saver.obj, ctrl_x, y);
      lv_obj_set_style_text_font(setting_page.list_saver.obj, &lv_font_montserrat_20, LV_PART_MAIN);
      lv_obj_set_style_pad_top(setting_page.list_saver.obj, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(setting_page.list_saver.obj, 4, LV_PART_MAIN);
      {
        lv_dropdown_set_options(setting_page.list_saver.obj,
          "never\n30s\n1m\n5m\n15m\n30m\n1h\n2h\n6h");
        static const uint32_t SAVER_VALS[] = {0, 30, 60, 300, 900, 1800, 3600, 7200, 21600};
        uint8_t  en  = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, board->info.spec.preference.screen.saver_enable);
        uint32_t tmo = nvs_config_get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, board->info.spec.preference.screen.saver_timeout);
        uint16_t sel_idx = 0;
        if (en) {
          for (int i = 1; i < 9; i++) {
            if (SAVER_VALS[i] == tmo) { sel_idx = (uint16_t)i; break; }
          }
        }
        lv_dropdown_set_selected(setting_page.list_saver.obj, sel_idx);
        lv_obj_t *saver_list = lv_dropdown_get_list(setting_page.list_saver.obj);
        if(saver_list) lv_obj_set_style_text_font(saver_list, &lv_font_montserrat_20, LV_PART_MAIN);
      }
      y += drop_h + pad;
      setting_page.checkbox_auto_rolling.obj = lv_checkbox_create(setting_page.container);
      lv_checkbox_set_text(setting_page.checkbox_auto_rolling.obj, "Screen Roll");
      lv_obj_set_style_text_font(setting_page.checkbox_auto_rolling.obj, font, 0);
      lv_obj_set_style_text_color(setting_page.checkbox_auto_rolling.obj, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(setting_page.checkbox_auto_rolling.obj, pad, y + 4);
      if(nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, board->info.spec.preference.screen.auto_rolling)) {
        lv_obj_add_state(setting_page.checkbox_auto_rolling.obj, LV_STATE_CHECKED);
      }
      lv_obj_add_event_cb(setting_page.checkbox_auto_rolling.obj, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

      setting_page.checkbox_screen_flip.obj = lv_checkbox_create(setting_page.container);
      lv_checkbox_set_text(setting_page.checkbox_screen_flip.obj, "Flip Screen");
      lv_obj_set_style_text_font(setting_page.checkbox_screen_flip.obj, font, 0);
      lv_obj_set_style_text_color(setting_page.checkbox_screen_flip.obj, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_pos(setting_page.checkbox_screen_flip.obj, W / 2, y + 4);
      if(nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, board->info.spec.preference.screen.flip)) {
        lv_obj_add_state(setting_page.checkbox_screen_flip.obj, LV_STATE_CHECKED);
      }
      lv_obj_add_event_cb(setting_page.checkbox_screen_flip.obj, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
      y += row_h + pad;

      // ---- Row 5: Save | Restart two equal-width buttons ----
      lv_coord_t btn_w = (W - pad * 3) / 2;
      lv_coord_t btn_h = 38;

      setting_page.btn_save.obj = lv_btn_create(setting_page.container);
      lv_obj_set_size(setting_page.btn_save.obj, btn_w, btn_h);
      lv_obj_set_pos(setting_page.btn_save.obj, pad, y);
      {
        lv_obj_t *lbl = lv_label_create(setting_page.btn_save.obj);
        lv_label_set_text(lbl, "Save");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_center(lbl);
      }
      lv_obj_add_event_cb(setting_page.btn_save.obj, button_event_cb, LV_EVENT_CLICKED, NULL);

      setting_page.btn_restart.obj = lv_btn_create(setting_page.container);
      lv_obj_set_size(setting_page.btn_restart.obj, btn_w, btn_h);
      lv_obj_set_pos(setting_page.btn_restart.obj, pad * 2 + btn_w, y);
      {
        lv_obj_t *lbl = lv_label_create(setting_page.btn_restart.obj);
        lv_label_set_text(lbl, "Restart");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_center(lbl);
      }
      lv_obj_add_event_cb(setting_page.btn_restart.obj, button_event_cb, LV_EVENT_CLICKED, NULL);
      y += btn_h + pad;

      // ---- Input keyboard (hidden by default, floats at the bottom) ----
      setting_page.kb_input.obj = lv_keyboard_create(setting_page.container);
      lv_obj_set_size(setting_page.kb_input.obj, W, 120);
      lv_obj_align(setting_page.kb_input.obj, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_add_flag(setting_page.kb_input.obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_event_cb(setting_page.kb_input.obj, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
      lv_obj_add_event_cb(setting_page.kb_input.obj, keyboard_event_cb, LV_EVENT_READY, NULL);

      inited = true;
      s_last_reload_at_ms = s_setting_page_entered_ms; // mark initial load done
    }

    // ── Reload NVS values into all controls each time setting page is entered ───
    if (s_last_reload_at_ms != s_setting_page_entered_ms) {
      s_last_reload_at_ms = s_setting_page_entered_ms;

      // brightness
      lv_slider_set_value(setting_page.bar_brightness.obj,
          nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, board->info.spec.preference.screen.brightness),
          LV_ANIM_OFF);

      // vcore
      if (setting_page.list_asic_vcore.obj) {
        const std::vector<work_option_t>& vc_opts = board->info.spec.ui.setting_page.vc;
        uint16_t cur = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, board->info.spec.asic.req_vcore);
        uint16_t idx = 0;
        for (uint16_t i = 0; i < (uint16_t)vc_opts.size(); i++) {
          if (vc_opts[i].value == cur) { idx = i; break; }
        }
        lv_dropdown_set_selected(setting_page.list_asic_vcore.obj, idx);
      }

      // freq
      if (setting_page.list_asic_freq.obj) {
        const std::vector<work_option_t>& oc_opts = board->info.spec.ui.setting_page.oc;
        uint16_t cur = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, board->info.spec.asic.req_frq);
        uint16_t idx = 0;
        for (uint16_t i = 0; i < (uint16_t)oc_opts.size(); i++) {
          if (oc_opts[i].value == cur) { idx = i; break; }
        }
        lv_dropdown_set_selected(setting_page.list_asic_freq.obj, idx);
      }

      // screen saver
      if (setting_page.list_saver.obj) {
        static const uint32_t SAVER_VALS[] = {0, 30, 60, 300, 900, 1800, 3600, 7200, 21600};
        uint8_t  en  = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, board->info.spec.preference.screen.saver_enable);
        uint32_t tmo = nvs_config_get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, board->info.spec.preference.screen.saver_timeout);
        uint16_t idx = 0;
        if (en) {
          for (int i = 1; i < 9; i++) {
            if (SAVER_VALS[i] == tmo) { idx = (uint16_t)i; break; }
          }
        }
        lv_dropdown_set_selected(setting_page.list_saver.obj, idx);
      }

      // checkboxes — defaults from board spec (compile-time constant, never hardcoded)
      if (nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, board->info.spec.preference.screen.auto_rolling))
        lv_obj_add_state(setting_page.checkbox_auto_rolling.obj, LV_STATE_CHECKED);
      else
        lv_obj_clear_state(setting_page.checkbox_auto_rolling.obj, LV_STATE_CHECKED);

      if (nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, board->info.spec.preference.screen.flip))
        lv_obj_add_state(setting_page.checkbox_screen_flip.obj, LV_STATE_CHECKED);
      else
        lv_obj_clear_state(setting_page.checkbox_screen_flip.obj, LV_STATE_CHECKED);
    }
  }
}

void ui_screen_saver_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  // Keep resetting the idle timer until miner is ready so the screensaver
  // timeout only starts counting from the moment mining begins.
  if(!(xEventGroupGetBits(board->status.init_evt) & INIT_EVENT_MINER_READY)){
    board->status.ui.last_active_ms = millis();
    return;
  }

  // ── GIF PSRAM cache (function-scope, avoids polluting module namespace) ──────
  static uint8_t *gif_ram_buf  = nullptr;
  static size_t   gif_ram_size = 0;

  // ── Register LVGL 'M' (PSRAM) FS driver once on first call ───────────────────
  // The +[] lambdas below access gif_ram_buf/gif_ram_size as static locals
  // (no capture needed — static locals have static storage duration).
  static bool s_ram_drv_registered = false;
  if (!s_ram_drv_registered) {
    static lv_fs_drv_t ram_drv;
    lv_fs_drv_init(&ram_drv);
    ram_drv.letter = 'M';
    ram_drv.open_cb = +[](lv_fs_drv_t*, const char*, lv_fs_mode_t) -> void* {
        if (!gif_ram_buf) return nullptr;
        return (void*)(new size_t(0));
    };
    ram_drv.close_cb = +[](lv_fs_drv_t*, void *fp) -> lv_fs_res_t {
        delete (size_t*)fp; return LV_FS_RES_OK;
    };
    ram_drv.read_cb = +[](lv_fs_drv_t*, void *fp, void *buf, uint32_t btr, uint32_t *br) -> lv_fs_res_t {
        size_t *pos = (size_t*)fp;
        uint32_t avail = (gif_ram_size > *pos) ? (uint32_t)(gif_ram_size - *pos) : 0u;
        *br = (btr < avail) ? btr : avail;
        memcpy(buf, gif_ram_buf + *pos, *br);
        *pos += *br;
        return LV_FS_RES_OK;
    };
    ram_drv.seek_cb = +[](lv_fs_drv_t*, void *fp, uint32_t pos, lv_fs_whence_t whence) -> lv_fs_res_t {
        size_t *cur = (size_t*)fp;
        if      (whence == LV_FS_SEEK_SET) *cur = (size_t)pos;
        else if (whence == LV_FS_SEEK_CUR) *cur += (size_t)pos;
        else if (whence == LV_FS_SEEK_END) *cur = (size_t)((int32_t)gif_ram_size + (int32_t)pos);
        return LV_FS_RES_OK;
    };
    ram_drv.tell_cb = +[](lv_fs_drv_t*, void *fp, uint32_t *pos_p) -> lv_fs_res_t {
        *pos_p = (uint32_t)*(size_t*)fp; return LV_FS_RES_OK;
    };
    lv_fs_drv_register(&ram_drv);
    LOG_I("LVGL PSRAM FS driver registered (letter='M')");
    s_ram_drv_registered = true;
  }

  static bool     fading_out       = false;
  static uint32_t fade_start_ms    = 0;
  static lv_obj_t *overlay         = nullptr;
  static lv_obj_t *lb_text         = nullptr;  // error label (no GIF)
  static lv_obj_t *gif_obj         = nullptr;  // GIF object (deleted with overlay)
  static lv_style_t style;
  static bool style_inited         = false;

  // ── Overlay visible: manage fade-out or keep showing ────────────────────
  if(overlay != nullptr){
    // Find me takes priority: immediately dismiss screensaver so find-me overlay can appear
    if (xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) {
      xEventGroupClearBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
      lv_obj_del(overlay);       // also deletes gif_obj / lb_text (children)
      overlay   = nullptr;
      lb_text   = nullptr;
      gif_obj   = nullptr;
      fading_out = false;
      board->status.ui.last_active_ms = millis();
      return;
    }
    EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED, pdFALSE, pdTRUE, 0);
    if((bits & SYS_EVENT_SCREEN_SAVER_TRIGGERED) == 0){
      // Event cleared (touch / button) → 1-second fade-out
      if(!fading_out){
        fading_out    = true;
        fade_start_ms = millis();
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      uint32_t elapsed = millis() - fade_start_ms;
      if(elapsed >= 1000){
        lv_obj_del(overlay);       // also deletes gif_obj / lb_text (children)
        overlay          = nullptr;
        lb_text          = nullptr;
        gif_obj          = nullptr;
        fading_out       = false;
        board->status.ui.last_active_ms = millis();
      } else {
        lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER - (uint32_t)(LV_OPA_COVER) * elapsed / 1000);
        lv_obj_set_style_opa(overlay, opa, LV_PART_MAIN);
      }
      return;
    } else {
      // Still active — cancel any in-progress fade
      if(fading_out){
        fading_out = false;
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      return;
    }
  }

  // ── Screen saver disabled or timeout not yet reached ───────────────────
  if(!board->status.preference.screen.saver_enable) return;
  if((millis() - board->status.ui.last_active_ms) < ((uint32_t)board->status.preference.screen.saver_timeout * 1000UL)) return;

  // ── OTA in progress: suppress screensaver entirely ───────────────────────
  // Overlay creation + GIF decode must not run while OTA is active:
  // flash writes pause the CPU cache, which would corrupt the GIF decoder
  // and freeze the display task. Reset last_active_ms so the inactivity
  // timer restarts from zero once OTA finishes.
  if (board->status.ota.running) {
    board->status.ui.last_active_ms = millis();
    return;
  }

  // ── Find me active: suppress screensaver, reset idle timer ──────────────
  if (xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) {
    board->status.ui.last_active_ms = millis();
    return;
  }

  // Create overlay background style once
  if(!style_inited){
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_border_width(&style, 0);
    lv_style_set_border_opa(&style, LV_OPA_TRANSP);
    style_inited = true;
  }

  // Create full-screen overlay
  overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_style(overlay, &style, LV_PART_MAIN);
  lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_event_cb(overlay, pressed_event_cb, LV_EVENT_PRESSED, NULL);
  // Fallback: LV_EVENT_PRESSING fires continuously while finger is held.
  // If PRESSED was somehow missed (e.g. indev==NULL path), PRESSING still clears the bit.
  lv_obj_add_event_cb(overlay, +[](lv_event_t*) {
    g_board.status.ui.last_active_ms = millis();
    xEventGroupClearBits(g_board.status.sys_evt,
      SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED);
  }, LV_EVENT_PRESSING, NULL);
  lv_obj_move_foreground(overlay);

  // ── Mode 1: black (screen off) — pure black overlay, no GIF ─────────────
  if (board->status.preference.screen.saver_mode == 1) {
    // overlay is already full-screen black; touch/button events are wired via
    // pressed_event_cb and the PRESSING lambda above, so wake-up works normally.
    xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
    LOG_I("[screensaver] mode=black, screen off");
    return;
  }

  // ── Mode 0 (default): GIF ────────────────────────────────────────────────
  // ── Select GIF filename based on board resolution ────────────────────────
  // NMAxe / Gamma: 240×135  →  screen_saver_240x135.gif
  // QAxe++:        320×240  →  screen_saver_320x240.gif
  const char* gif_spiffs_name = ((board->info.spec.name == BOARD_NMAXE_NAME) ||
                                  (board->info.spec.name == BOARD_NMAXE_GAMMA_NAME))
                                 ? "/screen_saver_240x135.gif"
                                 : "/screen_saver_320x240.gif";

  // ── Try to load GIF from SPIFFS ──────────────────────────────────────────
  if(SPIFFS.exists(gif_spiffs_name)){
    // Reload from SPIFFS on every trigger so a newly uploaded GIF takes effect
    // immediately on next screensaver entry without reboot.
    if (gif_ram_buf) {
      heap_caps_free(gif_ram_buf);
      gif_ram_buf = nullptr;
      gif_ram_size = 0;
    }

    File gf = SPIFFS.open(gif_spiffs_name, "r");
    if (gf) {
      gif_ram_size = gf.size();
      gif_ram_buf  = (uint8_t*)heap_caps_malloc(gif_ram_size, MALLOC_CAP_SPIRAM);
      if (gif_ram_buf) {
        size_t read_bytes = gf.read(gif_ram_buf, gif_ram_size);
        if (read_bytes != gif_ram_size) {
          LOG_W("[screensaver] GIF read short: %u/%u bytes, fallback to SPIFFS", (unsigned)read_bytes, (unsigned)gif_ram_size);
          heap_caps_free(gif_ram_buf);
          gif_ram_buf = nullptr;
          gif_ram_size = 0;
        } else {
          LOG_I("[screensaver] GIF reloaded into PSRAM: %u bytes from %s", (unsigned)gif_ram_size, gif_spiffs_name);
        }
      } else {
        gif_ram_size = 0;
        LOG_W("[screensaver] PSRAM alloc failed, fallback to SPIFFS");
      }
      gf.close();
    } else {
      LOG_W("[screensaver] open failed for %s, fallback to SPIFFS", gif_spiffs_name);
    }

    gif_obj = lv_gif_create(overlay);
    // Use PSRAM 'M' driver if cache loaded, else SPIFFS 'S' driver as fallback.
    // The filename after the colon is just an identifier for the FS driver.
    lv_gif_set_src(gif_obj, gif_ram_buf ? "M:screensaver.gif" : (String("S:") + (gif_spiffs_name + 1)).c_str());
    lv_obj_center(gif_obj);
    // gif_obj sits on top of overlay; clear CLICKABLE so touch events fall through to overlay's pressed_event_cb
    lv_obj_clear_flag(gif_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(overlay);
    LOG_I("[screensaver] GIF loaded from %s", gif_ram_buf ? "PSRAM" : "SPIFFS");
  } else {
    // GIF absent — show error hint so user knows what to do
    lb_text = lv_label_create(overlay);
    lv_obj_set_style_text_color(lb_text, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lb_text, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(lb_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lb_text, SCREEN_WIDTH - 10);
    lv_obj_align(lb_text, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lb_text, "Screen saver file lost!\nupload via AxeOS");
    lv_obj_move_foreground(overlay);
    LOG_W("[screensaver] %s not found, showing placeholder", gif_spiffs_name);
  }

  // Mark screen saver as active — touch / button will clear this bit to exit
  xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
}

void ui_find_me_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board) return;

  static bool      fading_out    = false;
  static uint32_t  fade_start_ms = 0;
  static lv_obj_t *overlay       = nullptr;
  static lv_obj_t *lb_text       = nullptr;
  static lv_style_t style;
  static bool style_inited       = false;

  // ── Overlay visible: manage fade-out or keep showing ────────────────────
  if (overlay != nullptr) {
    EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED, pdFALSE, pdTRUE, 0);
    if ((bits & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) == 0) {
      // Event cleared → 1-second fade-out
      if (!fading_out) {
        fading_out    = true;
        fade_start_ms = millis();
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      uint32_t elapsed = millis() - fade_start_ms;
      if (elapsed >= 1000) {
        lv_obj_del(overlay);
        overlay      = nullptr;
        lb_text      = nullptr;
        fading_out   = false;
      } else {
        lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER - (uint32_t)(LV_OPA_COVER) * elapsed / 1000);
        lv_obj_set_style_opa(overlay, opa, LV_PART_MAIN);
      }
      return;
    } else {
      // Still active — cancel any in-progress fade
      if (fading_out) {
        fading_out = false;
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      return;
    }
  }

  // ── Event not set: nothing to show ──────────────────────────────────────
  EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED, pdFALSE, pdTRUE, 0);
  if ((bits & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) == 0) return;

  // ── Create full-screen overlay ───────────────────────────────────────────
  if (!style_inited) {
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_white());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_border_width(&style, 0);
    lv_style_set_border_opa(&style, LV_OPA_TRANSP);
    style_inited = true;
  }

  overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_style(overlay, &style, LV_PART_MAIN);
  lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_event_cb(overlay, pressed_event_cb, LV_EVENT_PRESSED, NULL);
  // Fallback: LV_EVENT_PRESSING fires continuously while finger is held.
  // If PRESSED was somehow missed (e.g. indev==NULL path), PRESSING still clears the bit.
  lv_obj_add_event_cb(overlay, +[](lv_event_t*) {
    g_board.status.ui.last_active_ms = millis();
    xEventGroupClearBits(g_board.status.sys_evt,
      SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
  }, LV_EVENT_PRESSING, NULL);
  lv_obj_move_foreground(overlay);

  static const char* const FIND_ME_TEXT = ">>> I am here <<<";
  const bool is_nmqaxepp = (board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME);
  const char* const FIND_ME_HINT = is_nmqaxepp ? "Touch to exit" : "Press key to exit";

  lb_text = lv_label_create(overlay);
  lv_obj_set_style_text_font(lb_text, &Inconsolata_26, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_text, lv_color_black(), LV_PART_MAIN); // black
  lv_obj_set_style_text_align(lb_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(lb_text, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(lb_text, LV_HOR_RES - 4);
  lv_label_set_text(lb_text, FIND_ME_TEXT);
  lv_obj_align(lb_text, LV_ALIGN_CENTER, 0, -18);

  lv_obj_t* lb_hint = lv_label_create(overlay);
  lv_obj_set_style_text_font(lb_hint, &Inconsolata_26, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_hint, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_align(lb_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(lb_hint, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(lb_hint, LV_HOR_RES - 4);
  lv_label_set_text(lb_hint, FIND_ME_HINT);
  lv_obj_align(lb_hint, LV_ALIGN_CENTER, 0, 18);
  lv_obj_move_foreground(overlay);
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark status overlay
// Shown after loading completes when bm_mode==1. Covers existing pages.
// Updated every 1 s via the ui_thread_entry loop.
// Layout (portrait 240×135 or landscape 320×240):
//   [BENCHMARK]
//   Freq 500MHz / Vcore 1100mV
//   Phase: Stab  123s / 120s
//   HR: 123.4 GH/s  Temp: 45.2°C
//   [━━━━━━━━━━━━━━━━━━] 80%
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Benchmark status overlay — table layout, monospaced Inconsolata alignment.
// Adapts to screen size at first call:
//   Small (Axe / Gamma  240×135): Inconsolata_16 title, Inconsolata_14 rows, 5 data lines
//   Large (QAxe++       320×240): Inconsolata_22 title, Inconsolata_18 rows, 9 data lines
// ─────────────────────────────────────────────────────────────────────────────
void ui_benchmark_overlay_update(void* args) {
  board_sal_t *board = (board_sal_t*)args;
  if (!board) return;

  static lv_obj_t  *overlay    = nullptr;
  static lv_obj_t  *lb_title   = nullptr;
  static lv_obj_t  *lb_rows[9] = {};   // up to 9 data row labels
  static lv_obj_t  *bar        = nullptr;
  static lv_style_t style_bg;
  static bool style_inited = false;
  static bool is_large     = false;    // set once at create time
  static int  n_rows       = 0;

  // ── Destroy overlay when benchmark finishes ──────────────────────────────
  if (!board->status.bm.active) {
    if (overlay != nullptr) {
      lv_obj_del(overlay);
      overlay  = nullptr;
      lb_title = nullptr;
      for (int i = 0; i < 9; i++) lb_rows[i] = nullptr;
      bar    = nullptr;
      n_rows = 0;
    }
    return;
  }

  // ── Throttle to 1 update per second ─────────────────────────────────────
  static uint32_t last_ms = 0;
  if (overlay != nullptr && (millis() - last_ms < 1000)) return;

  // ── Helper: format seconds as "Xh Ym" / "Xm Ys" / "Xs" ─────────────────
  auto fmt_time = [](char* buf, size_t len, uint32_t s) {
    if      (s >= 3600) snprintf(buf, len, "%uh%02um", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
    else if (s >= 60)   snprintf(buf, len, "%um%02us", (unsigned)(s / 60),   (unsigned)(s % 60));
    else                snprintf(buf, len, "%us",       (unsigned)s);
  };

  // ── Create overlay (once) ────────────────────────────────────────────────
  if (overlay == nullptr) {
    is_large = (LV_HOR_RES >= 300); // QAxe++ 320×240; Axe/Gamma 240×135

    if (!style_inited) {
      lv_style_init(&style_bg);
      lv_style_set_bg_color(&style_bg, lv_color_black());
      lv_style_set_bg_opa(&style_bg, LV_OPA_90);
      lv_style_set_border_width(&style_bg, 0);
      lv_style_set_pad_all(&style_bg, 0);
      style_inited = true;
    }

    overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(overlay, &style_bg, LV_PART_MAIN);
    lv_obj_move_foreground(overlay);

    if (is_large) {
      // ── Large screen 320×240: Inconsolata_22 title + Inconsolata_18 data ──
      // Layout (y positions):
      //   4  : title    (line_height=20)
      //   28 : row[0]   Freq         (line_height=19 each)
      //   47 : row[1]   Vcore
      //   66 : row[2]   ASIC temp
      //   85 : row[3]   VRM temp
      //   104: row[4]   Phase
      //   123: row[5]   Time left
      //   142: row[6]   Hashrate
      //   161: row[7]   Round (F x/N  V x/M)
      //   180: row[8]   ETA
      //   204: bar      (h=8)
      n_rows = 9;

      lb_title = lv_label_create(overlay);
      lv_obj_set_style_text_font(lb_title, &Inconsolata_22, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_title, lv_color_hex(0x22D3EE), LV_PART_MAIN);
      lv_obj_set_pos(lb_title, 0, 4);
      lv_obj_set_width(lb_title, LV_HOR_RES);
      lv_obj_set_style_text_align(lb_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_label_set_text(lb_title, "[ BENCHMARK ]");

      const lv_color_t init_colors[9] = {
        lv_color_hex(0xFFFFFF), // Freq
        lv_color_hex(0xFFFFFF), // Vcore
        lv_color_hex(0xFFFFFF), // ASIC
        lv_color_hex(0xFFFFFF), // VRM
        lv_color_hex(0xFACC15), // Phase (yellow=stab; updated dynamically)
        lv_color_hex(0xFFFFFF), // Left
        lv_color_hex(0x4ADE80), // HR
        lv_color_hex(0xAAAAAA), // Round
        lv_color_hex(0xAAAAAA), // ETA
      };
      const int row_y_large[9] = {28, 47, 66, 85, 104, 123, 142, 161, 180};
      for (int i = 0; i < n_rows; i++) {
        lb_rows[i] = lv_label_create(overlay);
        lv_obj_set_style_text_font(lb_rows[i], &Inconsolata_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(lb_rows[i], init_colors[i], LV_PART_MAIN);
        lv_obj_set_pos(lb_rows[i], 8, row_y_large[i]);
        lv_label_set_text(lb_rows[i], "");
      }

      bar = lv_bar_create(overlay);
      lv_bar_set_range(bar, 0, 100);
      lv_obj_set_size(bar, (lv_coord_t)(LV_HOR_RES - 24), 8);
      lv_obj_set_pos(bar, 12, 204);

    } else {
      // ── Small screen 240×135: Inconsolata_16 title + Inconsolata_16 data ─
      // Layout (y positions, line_height=17 each row):
      //   2  : title    (line_height=17)
      //   21 : row[0]   F:500MHz  Vc:1100mV
      //   40 : row[1]   A: 45.2C  Vr: 38.1C
      //   59 : row[2]   Stab  -  120s left
      //   78 : row[3]   HR:123.4 GH/s  /  (Stab)
      //   97 : row[4]   F2/5 V1/3 ~2h30m
      //  124 : bar      (h=6)
      n_rows = 5;

      lb_title = lv_label_create(overlay);
      lv_obj_set_style_text_font(lb_title, &Inconsolata_16, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_title, lv_color_hex(0x22D3EE), LV_PART_MAIN);
      lv_obj_set_pos(lb_title, 0, 2);
      lv_obj_set_width(lb_title, LV_HOR_RES);
      lv_obj_set_style_text_align(lb_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_label_set_text(lb_title, "[ BENCHMARK ]");

      const lv_color_t init_colors_sm[5] = {
        lv_color_hex(0xFFFFFF), // Freq/Vcore
        lv_color_hex(0xFFFFFF), // Temps
        lv_color_hex(0xFACC15), // Phase (updated dynamically)
        lv_color_hex(0x4ADE80), // HR
        lv_color_hex(0xAAAAAA), // Round/ETA
      };
      const int row_y_small[5] = {21, 40, 59, 78, 97};
      for (int i = 0; i < n_rows; i++) {
        lb_rows[i] = lv_label_create(overlay);
        lv_obj_set_style_text_font(lb_rows[i], &Inconsolata_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lb_rows[i], init_colors_sm[i], LV_PART_MAIN);
        lv_obj_set_pos(lb_rows[i], 4, row_y_small[i]);
        lv_label_set_text(lb_rows[i], "");
      }

      bar = lv_bar_create(overlay);
      lv_bar_set_range(bar, 0, 100);
      lv_obj_set_size(bar, (lv_coord_t)(LV_HOR_RES - 16), 6);
      lv_obj_set_pos(bar, 8, 124);
    }
  }

  // ── Compute derived values ────────────────────────────────────────────────
  auto& bm = board->status.bm;

  uint16_t freq_total = (bm.freq_step > 0) ? ((bm.freq_max - bm.freq_min) / bm.freq_step + 1) : 1;
  uint16_t freq_idx   = (bm.freq_step > 0) ? ((bm.cur_freq  - bm.freq_min) / bm.freq_step + 1) : 1;
  uint16_t vc_total   = (bm.vcore_step > 0) ? ((bm.vcore_max - bm.vcore_min) / bm.vcore_step + 1) : 1;
  uint16_t vc_idx     = (bm.vcore_step > 0) ? ((bm.cur_vcore - bm.vcore_min) / bm.vcore_step + 1) : 1;

  uint32_t phase_remaining = (bm.phase_total > bm.phase_elapsed)
                             ? (bm.phase_total - bm.phase_elapsed) : 0;

  char eta_str[16];
  fmt_time(eta_str, sizeof(eta_str), bm.eta_sec);

  int pct = (bm.phase_total > 0) ? (int)(100u * bm.phase_elapsed / bm.phase_total) : 0;
  if (pct > 100) pct = 100;

  char buf[48];

  if (is_large) {
    // ── Large screen row updates ──────────────────────────────────────────
    // Column alignment: label field 9 chars (left-aligned) + ": " + value field
    // All rows start at x=8, monospaced → perfect column alignment
    snprintf(buf, sizeof(buf), "  Freq   : %4d MHz",   bm.cur_freq);
    lv_label_set_text(lb_rows[0], buf);

    snprintf(buf, sizeof(buf), "  Vcore  : %4d mV",    bm.cur_vcore);
    lv_label_set_text(lb_rows[1], buf);

    snprintf(buf, sizeof(buf), "  ASIC   : %5.1f C",   bm.asic_temp);
    lv_label_set_text(lb_rows[2], buf);

    snprintf(buf, sizeof(buf), "  VRM    : %5.1f C",   bm.vcore_temp);
    lv_label_set_text(lb_rows[3], buf);

    snprintf(buf, sizeof(buf), "  Phase  : %s",        bm.in_stab ? "Stabilize" : "Sampling ");
    lv_label_set_text(lb_rows[4], buf);
    lv_obj_set_style_text_color(lb_rows[4],
      bm.in_stab ? lv_color_hex(0xFACC15) : lv_color_hex(0x60A5FA), LV_PART_MAIN);

    snprintf(buf, sizeof(buf), "  Left   : %5lu s",    (unsigned long)phase_remaining);
    lv_label_set_text(lb_rows[5], buf);

    if (bm.in_stab) {
      snprintf(buf, sizeof(buf), "  Avg    : (Stab) / %4.0f GH", bm.exp_hr_ghs);
      lv_label_set_text(lb_rows[6], buf);
    } else {
      snprintf(buf, sizeof(buf), "  Avg    : %4.1f / %4.0f GH", bm.avg_hr_ghs, bm.exp_hr_ghs);
      lv_label_set_text(lb_rows[6], buf);
    }

    snprintf(buf, sizeof(buf), "  Round  :  F%u/%u  V%u/%u", freq_idx, freq_total, vc_idx, vc_total);
    lv_label_set_text(lb_rows[7], buf);

    snprintf(buf, sizeof(buf), "  ETA    : ~%s",        eta_str);
    lv_label_set_text(lb_rows[8], buf);

  } else {
    // ── Small screen row updates ──────────────────────────────────────────
    // Row 0: Freq + Vcore on one line (fixed-width, aligned at column 10)
    snprintf(buf, sizeof(buf), "F:%3dMHz  Vc:%4dmV",   bm.cur_freq, bm.cur_vcore);
    lv_label_set_text(lb_rows[0], buf);

    // Row 1: ASIC + VRM temps (aligned at column 10)
    snprintf(buf, sizeof(buf), "A:%4.1fC  Vr:%4.1fC",  bm.asic_temp, bm.vcore_temp);
    lv_label_set_text(lb_rows[1], buf);

    // Row 2: Phase name + seconds remaining (color changes dynamically)
    snprintf(buf, sizeof(buf), "%-4s     -%4lus left",
      bm.in_stab ? "Stab" : "Samp", (unsigned long)phase_remaining);
    lv_label_set_text(lb_rows[2], buf);
    lv_obj_set_style_text_color(lb_rows[2],
      bm.in_stab ? lv_color_hex(0xFACC15) : lv_color_hex(0x60A5FA), LV_PART_MAIN);

    // Row 3: Hashrate real/expected
    if (bm.in_stab) {
      snprintf(buf, sizeof(buf), "Avg:Stab /%4.0fGH", bm.exp_hr_ghs);
      lv_label_set_text(lb_rows[3], buf);
    } else {
      snprintf(buf, sizeof(buf), "Avg:%4.1f/%4.0f GH", bm.avg_hr_ghs, bm.exp_hr_ghs);
      lv_label_set_text(lb_rows[3], buf);
    }

    // Row 4: Round indices + ETA
    snprintf(buf, sizeof(buf), "F%u/%u V%u/%u ~%s",
      freq_idx, freq_total, vc_idx, vc_total, eta_str);
    lv_label_set_text(lb_rows[4], buf);
  }

  // ── Progress bar ─────────────────────────────────────────────────────────
  lv_bar_set_value(bar, pct, LV_ANIM_ON);

  last_ms = millis();
}

void ui_aphorism_page_update(void* args){
  board_sal_t *board = (board_sal_t*)args;
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static uint32_t last_update_ms = millis();
  static bool     fading_out    = false;
  static uint32_t fade_start_ms = 0;
  static lv_obj_t *overlay = nullptr, *lb_quote = nullptr, *lb_author = nullptr;
  static lv_style_t style;
  static bool style_inited = false;

  auto &aphorism = board->status.aphorism;

  // If overlay is visible, check whether the user has dismissed it (press/button clears the event)
  if(overlay != nullptr) {
    EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED, pdFALSE, pdTRUE, 0);
    if((bits & SYS_EVENT_SCREEN_SAVER_TRIGGERED) == 0) {
      // Event cleared — kick off / continue 1-second fade-out
      if(!fading_out) {
        fading_out    = true;
        fade_start_ms = millis();
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      uint32_t elapsed = millis() - fade_start_ms;
      if(elapsed >= 1000) {
        // Fade complete — destroy overlay
        lv_obj_del(overlay);
        overlay    = nullptr;
        lb_quote   = nullptr;
        lb_author  = nullptr;
        fading_out = false;
        last_update_ms = millis(); // reset timer so next quote waits a full 60 s
      } else {
        // Linearly interpolate opacity: 255 → 0 over 1000 ms
        lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER - (uint32_t)(LV_OPA_COVER) * elapsed / 1000);
        lv_obj_set_style_opa(overlay, opa, LV_PART_MAIN);
      }
      return;
    } else {
      // Event still set — screen saver active; cancel any in-progress fade
      if(fading_out) {
        fading_out = false;
        lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
      }
      return; // Screen saver still showing — do NOT pop a new quote or update labels
    }
  }

  // Rate-limit: wait 1 minute after screen saver was dismissed before showing the next quote
  if(millis() - last_update_ms <= 1000*60) return;

  String quote, author;

  xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
  if(!aphorism.pool.empty()) {
      auto qt = aphorism.pool.front();
      aphorism.pool.erase(aphorism.pool.begin());
      size_t remaining = aphorism.pool.size(); // already erased, so this is post-pop count
      xSemaphoreGive(aphorism.mutex);
      LOG_W("[tick] +--------------------------------------------------+");
      LOG_I("[tick] |  \"%s\"", qt.quote.c_str());
      LOG_I("[tick] |  -- %s  [kw: %s]  [%d left]", qt.author.c_str(), qt.keyword.c_str(), (int)remaining);
      LOG_W("[tick] +--------------------------------------------------+");
      quote = qt.quote;
      author = qt.author;
  } else {
      xSemaphoreGive(aphorism.mutex);
      LOG_W("aphorism pool is empty, skip this screen saver update");
      last_update_ms = millis();
      return;
  }

  // Create style once
  if(!style_inited) {
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_90);
    lv_style_set_border_width(&style, 0);
    lv_style_set_border_opa(&style, LV_OPA_TRANSP);
    style_inited = true;
  }
  if(overlay == NULL){
      overlay = lv_obj_create(lv_scr_act());
      lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
      lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_pad_all(overlay, 0, 0);
      lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
      lv_obj_add_style(overlay, &style, LV_PART_MAIN);
      lv_obj_set_style_opa(overlay, LV_OPA_COVER, LV_PART_MAIN); // ensure full opacity on (re-)create
      lv_obj_add_event_cb(overlay, pressed_event_cb, LV_EVENT_PRESSED, NULL);
      // Fallback: fires while finger is held, ensures immediate exit even if PRESSED was missed
      lv_obj_add_event_cb(overlay, +[](lv_event_t*) {
        g_board.status.ui.last_active_ms = millis();
        xEventGroupClearBits(g_board.status.sys_evt,
          SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED);
      }, LV_EVENT_PRESSING, NULL);
      // Mark screen saver as active so press/button can clear it to trigger fade
      xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
  }
  if(lb_quote == NULL && overlay != NULL){
      const lv_font_t *font = ((board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) ? &Inconsolata_26 : &Inconsolata_22);
      lb_quote = lv_label_create(overlay);
      lv_obj_set_width(lb_quote, SCREEN_WIDTH - 5);
      lv_label_set_long_mode(lb_quote, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(lb_quote, font, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_quote, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_align(lb_quote, LV_ALIGN_TOP_MID, 0, 5);
      lv_obj_move_foreground(overlay);
  }
  if(lb_author == NULL && overlay != NULL){
      const lv_font_t *font = &lv_font_montserrat_14;
      lb_author = lv_label_create(overlay);
      lv_obj_set_width(lb_quote, SCREEN_WIDTH - 5);
      lv_label_set_long_mode(lb_author, LV_LABEL_LONG_SCROLL);
      lv_obj_set_style_text_font(lb_author, font, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_author, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_align(lb_author, LV_ALIGN_BOTTOM_RIGHT, 0, -5);
      lv_obj_move_foreground(overlay);
  }

  lv_label_set_text(lb_quote, quote.c_str());
  lv_label_set_text(lb_author, ("-- " + author).c_str());
  lv_obj_move_foreground(overlay);
  last_update_ms = millis();
}

static void ui_goto_page_unlocked(int8_t page, lv_anim_enable_t anim) {
  if(parent_wall && page >= UI_PAGE_LOADING && page <= UI_PAGE_SETTING_SWARM && g_board.status.ui.page.list[page]) {
    g_board.status.ui.page.current = page;
    lv_obj_set_tile(parent_wall, g_board.status.ui.page.list[page], anim);
  }else{
    LOG_E("invalid page index or parent docker is null!!!");
  }
}

void ui_goto_page(int8_t page, lv_anim_enable_t anim) {
  SemaphoreHandle_t mutex = g_board.status.ui.lvgl.drv_xMutex;
  if(mutex == nullptr) {
    ui_goto_page_unlocked(page, anim);
    return;
  }

  if(xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    LOG_W("ui_goto_page lock timeout, page=%d", page);
    return;
  }

  ui_goto_page_unlocked(page, anim);
  xSemaphoreGive(mutex);
}

void ui_switch_next_page_cb(){
  uint8_t current_index = g_board.status.ui.page.current;
  uint8_t next_index    = current_index;

  next_index = (g_board.status.ui.page.current == UI_PAGE_SETTING_SWARM) ? UI_PAGE_CONFIG : g_board.status.ui.page.current;
  next_index++;

  ui_goto_page(next_index, LV_ANIM_ON);
}

void ui_switch_prev_page_cb(){
  uint8_t current_index = g_board.status.ui.page.current;
  uint8_t prev_index    = current_index;

  prev_index = (g_board.status.ui.page.current == UI_PAGE_MINER) ? UI_PAGE_SETTING_SWARM + 1 : g_board.status.ui.page.current;
  prev_index--;
  
  ui_goto_page(prev_index, LV_ANIM_ON);
}

// ─── Power Fault Alert Overlay (OC / OT) ─────────────────────────────────────
// Handles both SYS_EVENT_POWER_OC_FAULT and SYS_EVENT_POWER_OT_FAULT in one overlay.
// Checks both bits each cycle; OC takes priority if both fire simultaneously.
// Content (title colour, body text, action button) adapts to the active event.
// OC action:  reset ASIC freq/vcore to factory defaults, then reboot.
// OT action:  reboot only (cooling issue, no NVS change needed).
// Dismiss:    clears both fault bits; overlay removed on next update cycle.
void ui_power_alert_update(void* args) {
  board_sal_t *board = (board_sal_t*)args;
  if (!board) return;

  static lv_obj_t   *overlay      = NULL;
  static lv_obj_t   *lb_title     = NULL;
  static lv_obj_t   *lb_body      = NULL;
  static lv_obj_t   *btn_yes      = NULL;
  static lv_obj_t   *btn_no       = NULL;
  static lv_style_t  style_overlay;
  static lv_style_t  style_btn_yes;
  static lv_style_t  style_btn_no;
  static bool        styles_inited = false;
  static EventBits_t active_event  = 0;  // which event created the current overlay

  EventBits_t bits       = xEventGroupGetBits(board->status.sys_evt);
  bool        oc_active  = (bits & SYS_EVENT_POWER_OC_FAULT) != 0;
  bool        ot_active  = (bits & SYS_EVENT_POWER_OT_FAULT) != 0;
  bool        any_active = oc_active || ot_active;

  // Destroy overlay when the event that created it has been cleared
  if (overlay != NULL && active_event != 0 && !(bits & active_event)) {
    lv_obj_del(overlay);
    overlay = lb_title = lb_body = btn_yes = btn_no = NULL;
    active_event = 0;
  }
  if (!any_active) return;
  if (overlay != NULL) return;  // already visible

  // OC takes priority if both bits happen to fire simultaneously
  active_event  = oc_active ? SYS_EVENT_POWER_OC_FAULT : SYS_EVENT_POWER_OT_FAULT;
  const bool is_oc = (active_event == SYS_EVENT_POWER_OC_FAULT);

  // ── Init constant styles once ─────────────────────────────────────────────
  if (!styles_inited) {
    lv_style_init(&style_overlay);
    lv_style_set_bg_color(&style_overlay, lv_color_black());
    lv_style_set_bg_opa(&style_overlay, LV_OPA_90);
    lv_style_set_border_width(&style_overlay, 0);
    lv_style_set_border_opa(&style_overlay, LV_OPA_TRANSP);

    lv_style_init(&style_btn_no);
    lv_style_set_bg_color(&style_btn_no, lv_color_hex(0x424242));  // grey — dismiss
    lv_style_set_bg_opa(&style_btn_no, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_no, 0);

    styles_inited = true;
  }
  // style_btn_yes colour varies per event — reinit each time (objects already deleted)
  lv_style_init(&style_btn_yes);
  lv_style_set_bg_color(&style_btn_yes, lv_color_hex(is_oc ? 0xD32F2F : 0xFF6D00));
  lv_style_set_bg_opa(&style_btn_yes, LV_OPA_COVER);
  lv_style_set_border_width(&style_btn_yes, 0);

  // ── Create full-screen overlay ────────────────────────────────────────────
  overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  lv_obj_add_style(overlay, &style_overlay, LV_PART_MAIN);
  lv_obj_move_foreground(overlay);

  // ── Title ─────────────────────────────────────────────────────────────────
  lb_title = lv_label_create(overlay);
  lv_obj_set_style_text_font(lb_title, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_title,
    lv_color_hex(is_oc ? 0xFF5252 : 0xFF6D00), LV_PART_MAIN);  // red / deep-orange
  lv_label_set_text(lb_title,
    is_oc ? LV_SYMBOL_WARNING " Overcurrent Fault"
          : LV_SYMBOL_WARNING " Overtemp Fault");
  lv_obj_align(lb_title, LV_ALIGN_TOP_MID, 0, 10);

  // ── Body text ─────────────────────────────────────────────────────────────
  lb_body = lv_label_create(overlay);
  lv_obj_set_style_text_font(lb_body, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_body, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_align(lb_body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(lb_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lb_body, LV_HOR_RES - 16);
  lv_label_set_text(lb_body,
    is_oc ? "Reset freq & voltage to defaults,\nthen reboot?"
          : "ASIC powered down.\nCheck cooling & lower OC settings,\nthen restart.");
  lv_obj_align(lb_body, LV_ALIGN_CENTER, 0, -8);

  lv_coord_t btn_w = (LV_HOR_RES - 50) / 2;
  lv_coord_t btn_h = 32;

  // ── Action button (left) ──────────────────────────────────────────────────
  btn_yes = lv_btn_create(overlay);
  lv_obj_add_style(btn_yes, &style_btn_yes, LV_PART_MAIN);
  lv_obj_set_size(btn_yes, btn_w, btn_h);
  lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_LEFT, 20, -8);
  lv_obj_t *lb_yes = lv_label_create(btn_yes);
  lv_obj_set_style_text_font(lb_yes, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_label_set_text(lb_yes, is_oc ? "Reset & Reboot" : "Restart");
  lv_obj_center(lb_yes);
  if (is_oc) {
    lv_obj_add_event_cb(btn_yes, +[](lv_event_t* e) {
      board_sal_t* b = (board_sal_t*)lv_event_get_user_data(e);
      if (!b) return;
      nvs_handle handle;
      if (nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u16(handle, NVS_CONFIG_ASIC_FREQ,    b->info.spec.asic.default_frq);
        nvs_set_u16(handle, NVS_CONFIG_ASIC_VOLTAGE, b->info.spec.asic.default_vcore);
        nvs_commit(handle);
        nvs_close(handle);
      }
      LOG_W("OC alert: reset freq=%u vcore=%u — rebooting...",
            b->info.spec.asic.default_frq, b->info.spec.asic.default_vcore);
      esp_restart();
    }, LV_EVENT_CLICKED, (void*)board);
  } else {
    lv_obj_add_event_cb(btn_yes, +[](lv_event_t* e) {
      LOG_W("OT alert: user confirmed restart");
      esp_restart();
    }, LV_EVENT_CLICKED, NULL);
  }

  // ── "Dismiss" button (right, grey) ───────────────────────────────────────
  btn_no = lv_btn_create(overlay);
  lv_obj_add_style(btn_no, &style_btn_no, LV_PART_MAIN);
  lv_obj_set_size(btn_no, btn_w, btn_h);
  lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
  lv_obj_t *lb_no = lv_label_create(btn_no);
  lv_obj_set_style_text_font(lb_no, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_label_set_text(lb_no, "Dismiss");
  lv_obj_center(lb_no);
  lv_obj_add_event_cb(btn_no, +[](lv_event_t* e) {
    board_sal_t* b = (board_sal_t*)lv_event_get_user_data(e);
    if (!b) return;
    xEventGroupClearBits(b->status.sys_evt, SYS_EVENT_POWER_OC_FAULT | SYS_EVENT_POWER_OT_FAULT);
    // overlay removed on next ui_power_alert_update() call
  }, LV_EVENT_CLICKED, (void*)board);
}
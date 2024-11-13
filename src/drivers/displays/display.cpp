#include "display.h"
#include "lvgl.h"
#include "logger.h"
#include <TFT_eSPI.h>
#include "global.h"
#include "helper.h" 
#include "image.h"
#include "nm_pwr.h"


LV_FONT_DECLARE(ds_digib_font_16)
LV_FONT_DECLARE(ds_digib_font_18)
LV_FONT_DECLARE(ds_digib_font_20)
LV_FONT_DECLARE(ds_digib_font_36)
LV_FONT_DECLARE(ds_digib_font_38)
LV_FONT_DECLARE(ds_digib_font_50)
LV_FONT_DECLARE(symbol_14)


static TFT_eSPI tft = TFT_eSPI();
static SemaphoreHandle_t lvgl_xMutex = xSemaphoreCreateMutex();

static lv_obj_t *parent_docker = NULL, *loading_page = NULL, *config_page = NULL ,*miner_page = NULL, *staus_page = NULL;
static lv_obj_t *lb_version = NULL, *lb_cfg_timeout = NULL, *lb_hashrate = NULL, *lb_blk_hit = NULL, *lb_temp = NULL, *lb_power = NULL, *lb_wifi = NULL, *lb_uptime_day = NULL, *lb_uptime_hms = NULL, *lb_diff = NULL;
static lv_obj_t *lb_share = NULL, *lb_fan = NULL, *lb_hr_unit = NULL, *lb_uptime_day_unit = NULL;
static lv_obj_t *lb_uptime_symbol = NULL, *lb_wifi_symbol = NULL, *lb_diff_symbol = NULL, *lb_share_symb = NULL, *lb_temp_symb = NULL, *lb_fan_symb = NULL;
static lv_obj_t *lb_price = NULL;



static void tft_init(){
  pinMode(NM_AXE_TFT_PWER_PIN, OUTPUT);
  digitalWrite(NM_AXE_TFT_PWER_PIN, LOW);
  tft.begin(); 
  if(g_nmaxe.screen.flip)tft.setRotation(1); 
  else tft.setRotation(3); 
}

static void disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ){
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready(disp_drv);
}

static void lvgl_tick_task(void *args){
  char *name = (char*)malloc(20);
  uint16_t tick_interval = 100;
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);

  uint32_t last_tick = millis();
  while(true){
    if (xSemaphoreTake(lvgl_xMutex, tick_interval/2) == pdTRUE){
      lv_tick_inc(millis() - last_tick);
      lv_timer_handler(); /* let the GUI do its work */
      xSemaphoreGive(lvgl_xMutex); 
      last_tick = millis();
    }
    delay(tick_interval/2);
  }
}

static void ui_drv_register(void){
  LOG_I("lvgl version: %s", (String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch()).c_str());

  static lv_disp_draw_buf_t lvgl_draw_buf;
  static lv_disp_drv_t      disp_drv;
  static lv_color_t         color_buf[ SCREEN_WIDTH * SCREEN_HEIGHT / 10 ];

  lv_disp_draw_buf_init( &lvgl_draw_buf, color_buf, NULL, SCREEN_WIDTH * SCREEN_HEIGHT / 10 );
  /*Initialize the display*/
  lv_disp_drv_init( &disp_drv );
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &lvgl_draw_buf;
  lv_disp_drv_register( &disp_drv );
}

static void ui_layout_init(void){
  //wait a bit for lvgl tick task to start, necessary for lvgl to work properly
  delay(10);

  //create parent object
  parent_docker = lv_obj_create(lv_scr_act());
  lv_obj_set_size(parent_docker, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2); 
  lv_obj_set_pos(parent_docker, 0, 0);
  lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF); 
  lv_obj_set_scroll_dir(parent_docker, LV_DIR_ALL); 
  lv_obj_set_scroll_snap_x(parent_docker, LV_SCROLL_SNAP_START); 
  lv_obj_set_scroll_snap_y(parent_docker, LV_SCROLL_SNAP_START); 
  lv_obj_set_style_pad_all(parent_docker, 0, 0);
  lv_obj_set_style_border_width(parent_docker, 0, 0);
  lv_obj_align(parent_docker, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_opa(parent_docker, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_border_opa(parent_docker, LV_OPA_TRANSP, LV_PART_INDICATOR);

  // Create loading page
  loading_page = lv_obj_create(parent_docker);
  lv_obj_set_size(loading_page, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(loading_page, 0 * SCREEN_WIDTH, 0);
  lv_obj_set_style_pad_all(loading_page, 0, 0);
  lv_obj_set_style_border_width(loading_page, 0, 0);
  lv_obj_set_scrollbar_mode(loading_page, LV_SCROLLBAR_MODE_OFF); 
  lv_obj_t *loading_img_obj = lv_img_create(loading_page);
  lv_img_set_src(loading_img_obj, &loading_img);
  lv_obj_set_size(loading_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(loading_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create config page
  config_page = lv_obj_create(parent_docker);
  lv_obj_set_size(config_page, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(config_page, 0, SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(config_page, 0, 0);
  lv_obj_set_style_border_width(config_page, 0, 0);
  lv_obj_set_scrollbar_mode(config_page, LV_SCROLLBAR_MODE_OFF); 
  lv_obj_t *config_img_obj = lv_img_create(config_page);
  lv_img_set_src(config_img_obj, &config_img);
  lv_obj_set_size(config_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(config_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create miner page  
  miner_page = lv_obj_create(parent_docker);
  lv_obj_set_size(miner_page, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(miner_page, 1 * SCREEN_WIDTH, 0);
  lv_obj_set_style_pad_all(miner_page, 0, 0);
  lv_obj_set_style_border_width(miner_page, 0, 0);
  lv_obj_set_scrollbar_mode(miner_page, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *miner_img_obj = lv_img_create(miner_page);
  lv_img_set_src(miner_img_obj, &main_page_img);
  lv_obj_set_size(miner_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(miner_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create status page  
  staus_page = lv_obj_create(parent_docker);
  lv_obj_set_size(staus_page, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(staus_page, 1 * SCREEN_WIDTH, 1 * SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(staus_page, 0, 0);
  lv_obj_set_style_border_width(staus_page, 0, 0);
  lv_obj_set_scrollbar_mode(staus_page, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *status_img_obj = lv_img_create(staus_page);
  lv_img_set_src(status_img_obj, &status_page_img);
  lv_obj_set_size(status_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(status_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  //////////////////////////////////////loading page layout///////////////////////////////////////////////
  //Version
  const lv_font_t *font = &lv_font_montserrat_14;
  lv_color_t font_color = lv_color_hex(0xFFFFFF);
  lb_version   = lv_label_create( loading_page );
  lv_obj_set_width(lb_version, SCREEN_WIDTH);
  lv_label_set_text( lb_version, g_nmaxe.board.fw_version.c_str());
  lv_obj_set_style_text_font(lb_version, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_version, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_version, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_version, LV_ALIGN_TOP_MID, SCREEN_WIDTH - (uint16_t)(g_nmaxe.board.fw_version.length() * 7.2), SCREEN_HEIGHT - 15);
  //////////////////////////////////////config page layout///////////////////////////////////////////////
  //config timeout
  font_color = lv_color_hex(0xFFFFFF);
  lb_cfg_timeout   = lv_label_create( config_page );
  lv_obj_set_width(lb_cfg_timeout, SCREEN_WIDTH);
  lv_label_set_text( lb_cfg_timeout, "");
  lv_obj_set_style_text_font(lb_cfg_timeout, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_cfg_timeout, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_cfg_timeout, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_cfg_timeout, LV_ALIGN_BOTTOM_MID, 175, 0);
  //////////////////////////////////////miner page layout///////////////////////////////////////////////
  //Hashrate value
  font = &ds_digib_font_38;
  font_color = lv_color_hex(0xFFA500);
  lb_hashrate   = lv_label_create( miner_page );
  lv_obj_set_width(lb_hashrate, 80);
  lv_label_set_text( lb_hashrate, " ");
  lv_obj_set_style_text_font(lb_hashrate, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_hashrate, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_hashrate, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( lb_hashrate, LV_ALIGN_BOTTOM_MID, 40, 0);
  //Hit value
  font = &ds_digib_font_50;
  font_color = lv_color_hex(0xFFA500);
  lb_blk_hit   = lv_label_create( miner_page );
  lv_obj_set_width(lb_blk_hit, SCREEN_WIDTH);
  lv_label_set_text( lb_blk_hit, " ");
  lv_obj_set_style_text_font(lb_blk_hit, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_blk_hit, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_blk_hit, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( lb_blk_hit, LV_ALIGN_TOP_MID, 7, 39); 
  //price value
  font = &ds_digib_font_20;
  font_color = lv_color_hex(0xFFFFFF);
  lb_price   = lv_label_create( miner_page );
  lv_obj_set_width(lb_price, SCREEN_WIDTH);
  lv_label_set_text( lb_price, " ");
  lv_obj_set_style_text_font(lb_price, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_price, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_price, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_price, LV_ALIGN_LEFT_MID, 33, 29 ); 
  //power value
  font = &ds_digib_font_18;
  font_color = lv_color_hex(0xFFFFFF);
  lb_power   = lv_label_create( miner_page );
  lv_obj_set_width(lb_power, 95);
  lv_label_set_text( lb_power, " ");
  lv_obj_set_style_text_font(lb_power, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_power, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_power, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_power, LV_ALIGN_TOP_LEFT, 10, 114 ); 
  //wifi value
  font = &ds_digib_font_16;
  font_color = lv_color_hex(0xFFFFFF);
  lb_wifi    = lv_label_create( miner_page );
  lv_obj_set_width(lb_wifi, 90);
  lv_label_set_text( lb_wifi, " ");
  lv_obj_set_style_text_font(lb_wifi, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_wifi, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(lb_wifi, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_wifi, LV_ALIGN_TOP_LEFT, 140, 2 );

  //uptime value, hour , minute, second
  font = &ds_digib_font_16;
  font_color = lv_color_hex(0xFFFFFF);
  lb_uptime_hms    = lv_label_create( miner_page );
  lv_obj_set_width(lb_uptime_hms, 88);
  lv_label_set_text( lb_uptime_hms, " ");
  lv_obj_set_style_text_font(lb_uptime_hms, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_uptime_hms, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_uptime_hms, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_uptime_hms, LV_ALIGN_TOP_LEFT, 65, 2 );
  //uptime value, day
  font = &ds_digib_font_16;
  font_color = lv_color_hex(0xFFFFFF);
  lb_uptime_day    = lv_label_create( miner_page );
  lv_obj_set_width(lb_uptime_day, 88);
  lv_label_set_text( lb_uptime_day, " ");
  lv_obj_set_style_text_font(lb_uptime_day, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_uptime_day, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_uptime_day, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_uptime_day, LV_ALIGN_TOP_LEFT, 32, 2 );
  //uptime day unit  
  font = &lv_font_montserrat_14;
  font_color = lv_color_hex(0xFFA500);
  lb_uptime_day_unit    = lv_label_create( miner_page );
  lv_obj_set_width(lb_uptime_day_unit, 20);
  lv_label_set_text( lb_uptime_day_unit, "d");
  lv_obj_set_style_text_font(lb_uptime_day_unit, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_uptime_day_unit, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_uptime_day_unit, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_uptime_day_unit, LV_ALIGN_TOP_LEFT, 56, 2 );

  //Diff value
  font = &ds_digib_font_18;
  font_color = lv_color_hex(0xFFFFFF);
  lb_diff    = lv_label_create( miner_page );
  lv_obj_set_width(lb_diff, 100);
  lv_label_set_text( lb_diff, " ");
  lv_obj_set_style_text_font(lb_diff, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_diff, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(lb_diff, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_diff, LV_ALIGN_TOP_LEFT, 132, 26 );
  //share value
  font = &ds_digib_font_18;
  font_color = lv_color_hex(0xFFFFFF);
  lb_share   = lv_label_create( miner_page );
  lv_obj_set_width(lb_share, SCREEN_WIDTH);
  lv_label_set_text( lb_share, " ");
  lv_obj_set_style_text_font(lb_share, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_share, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_share, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_share, LV_ALIGN_TOP_LEFT, 132, 42 ); 
  //temp value
  font = &ds_digib_font_18;
  font_color = lv_color_hex(0xFFFFFF);
  lb_temp   = lv_label_create( miner_page );
  lv_obj_set_width(lb_temp, SCREEN_WIDTH);
  lv_label_set_text( lb_temp, " ");
  lv_obj_set_style_text_font(lb_temp, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_temp, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_temp, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_temp, LV_ALIGN_TOP_LEFT, 132, 59 );
  //Fan value
  font = &ds_digib_font_18;
  font_color = lv_color_hex(0xFFFFFF);
  lb_fan   = lv_label_create( miner_page );
  lv_obj_set_width(lb_fan, SCREEN_WIDTH);
  lv_label_set_text( lb_fan, " ");
  lv_obj_set_style_text_font(lb_fan, font, LV_PART_MAIN);
  lv_label_set_long_mode(lb_fan, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(lb_fan, font_color, LV_PART_MAIN); 
  lv_obj_align( lb_fan, LV_ALIGN_TOP_LEFT, 132, 76); 
  //Hashrate uint
  font = &ds_digib_font_20;
  font_color = lv_color_hex(0xFFFFFF);
  lb_hr_unit   = lv_label_create( miner_page );
  lv_obj_set_width(lb_hr_unit, SCREEN_WIDTH);
  lv_label_set_text( lb_hr_unit, " ");
  lv_obj_set_style_text_font(lb_hr_unit, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_hr_unit, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_hr_unit, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_hr_unit, LV_ALIGN_TOP_MID, 195, 115); 
  // symbol uptime
  font = &lv_font_montserrat_14;
  font_color = lv_color_hex(0xFFA500);
  lb_uptime_symbol   = lv_label_create( miner_page );
  lv_obj_set_width(lb_uptime_symbol, SCREEN_WIDTH);
  lv_label_set_text( lb_uptime_symbol, LV_SYMBOL_BELL); 
  lv_obj_set_style_text_font(lb_uptime_symbol, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_uptime_symbol, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_uptime_symbol, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_uptime_symbol, LV_ALIGN_TOP_MID, 18, 1); 
  // symbol wifi
  font = &lv_font_montserrat_14;
  font_color = lv_color_hex(0xFFA500);
  lb_wifi_symbol   = lv_label_create( miner_page );
  lv_obj_set_width(lb_wifi_symbol, SCREEN_WIDTH);
  lv_label_set_text( lb_wifi_symbol, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(lb_wifi_symbol, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_wifi_symbol, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_wifi_symbol, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_wifi_symbol, LV_ALIGN_TOP_MID, 123, 1); 

  //diff symbol
  font = &symbol_14;
  font_color = lv_color_hex(0xA9A9A9);
  lb_diff_symbol   = lv_label_create( miner_page );
  lv_obj_set_width(lb_diff_symbol, SCREEN_WIDTH);
  lv_label_set_text( lb_diff_symbol, "\xEF\x82\x80");
  lv_obj_set_style_text_font(lb_diff_symbol, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_diff_symbol, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_diff_symbol, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_diff_symbol, LV_ALIGN_TOP_MID, 108, 26); 
  // share symbol
  font = &symbol_14;
  font_color = lv_color_hex(0xA9A9A9);
  lb_share_symb   = lv_label_create( miner_page );
  lv_obj_set_width(lb_share_symb, SCREEN_WIDTH);
  lv_label_set_text( lb_share_symb, "\xEF\x8E\x82");
  lv_obj_set_style_text_font(lb_share_symb, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_share_symb, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_share_symb, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_share_symb, LV_ALIGN_TOP_MID, 108, 42); 
  //temp symbol
  font = &symbol_14;
  font_color = lv_color_hex(0xA9A9A9);
  lb_temp_symb   = lv_label_create( miner_page );
  lv_obj_set_width(lb_temp_symb, SCREEN_WIDTH);
  lv_label_set_text( lb_temp_symb, "\xEF\x8B\x88");
  lv_obj_set_style_text_font(lb_temp_symb, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_temp_symb, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_temp_symb, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_temp_symb, LV_ALIGN_TOP_MID, 113, 59); 
  //fan symbol
  font = &symbol_14;
  font_color = lv_color_hex(0xA9A9A9);
  lb_fan_symb   = lv_label_create( miner_page );
  lv_obj_set_width(lb_fan_symb, SCREEN_WIDTH);
  lv_label_set_text( lb_fan_symb, "\xEF\xA1\xA3");
  lv_obj_set_style_text_font(lb_fan_symb, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lb_fan_symb, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(lb_fan_symb, LV_LABEL_LONG_DOT);
  lv_obj_align( lb_fan_symb, LV_ALIGN_TOP_MID, 110, 76);
}

static void ui_update_loading_string(String str, uint32_t color, bool prgress_update) {
    static const lv_font_t *font = &lv_font_montserrat_14;
    static lv_obj_t *lb_loading = NULL;
    static lv_obj_t * bar = NULL;
    static lv_obj_t * label_progress = NULL;
    static uint8_t progress = 0, progress_total = 12;

    lv_color_t font_color = lv_color_hex(color);

    if (lb_loading == NULL){
      lb_loading = lv_label_create(loading_page);
      lv_obj_set_width(lb_loading, SCREEN_WIDTH);
      lv_obj_set_style_text_font(lb_loading, font, LV_PART_MAIN);
      lv_label_set_long_mode(lb_loading, LV_LABEL_LONG_DOT);
      lv_obj_align(lb_loading, LV_ALIGN_BOTTOM_LEFT, 3, 0);

      //bar 
      bar = lv_bar_create(loading_page);
      lv_bar_set_range(bar, 0, progress_total);
      lv_bar_set_value(bar, progress, LV_ANIM_ON);
      lv_obj_set_size(bar, SCREEN_WIDTH * 0.9, 5);
      lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

      //progress label
      label_progress = lv_label_create(loading_page);
      lv_label_set_text(label_progress, "");
      lv_obj_set_style_text_color(label_progress, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_align(label_progress, LV_ALIGN_LEFT_MID, 0, 0);
    }

    if(prgress_update){
      lv_coord_t bar_x = lv_obj_get_x(bar);
      lv_coord_t bar_w = lv_obj_get_width(bar);
      lv_coord_t label_x = bar_x + (bar_w * progress / progress_total) - lv_obj_get_width(label_progress) / 2;
      lv_obj_set_pos(label_progress, label_x, 10);
      lv_label_set_text(label_progress, (String(100 * (float)progress/(float)progress_total, 0) + "%").c_str());
      lv_bar_set_value(bar, progress, LV_ANIM_ON);
      progress++;
    }
    lv_obj_set_style_text_color(lb_loading, font_color, LV_PART_MAIN);
    lv_label_set_text(lb_loading, str.c_str());
}

static void ui_update_ota_bar(int progress){
  static lv_obj_t * overlay = NULL, *bar = NULL, *label_file = NULL, *label_progress = NULL;
  static char progress_text[10];
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
  }

  lv_coord_t bar_x = lv_obj_get_x(bar);
  lv_coord_t bar_w = lv_obj_get_width(bar);
  lv_coord_t label_x = bar_x + (bar_w * progress / 100.0f) - lv_obj_get_width(label_progress)/2;
  //update progress label
  snprintf(progress_text, sizeof(progress_text), "%d%%", progress);
  lv_obj_set_pos(label_progress, label_x, 10);
  lv_label_set_text(label_progress, progress_text);

  //update bar value
  lv_bar_set_value(bar, progress, LV_ANIM_ON);
  lv_label_set_text(label_file, g_nmaxe.ota.firmware.c_str());

}

void ui_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);

  const char* vbus_chk_str[] = {"Vbus check   ","Vbus check.  ","Vbus check.. ","Vbus check..."};
  const char* wifi_con_str[] = {"Wifi connect   ","Wifi connect.  ","Wifi connect.. ","Wifi connect..."};
  const char* fan_test_str[] = {"Fan test   ","Fan test.  ","Fan test.. ","Fan test..."};
  const char* market_con_str[] = {"Market connect   ","Market connect.  ","Market connect.. ","Market connect..."};
  const char* pool_con_str[] = {"Pool connect   ","Pool connect.  ","Pool connect.. ","Pool connect..."};

  tft_init();

  lv_init();

  ui_drv_register();

  //lvgl tick task
  String taskName = "(lvgl)";
  xTaskCreatePinnedToCore(lvgl_tick_task, taskName.c_str(), 1024*5, (void*)taskName.c_str(), TASK_PRIORITY_LVGL_DRV, NULL, 1);

  //ui layout init
  ui_layout_init();

  //set the first page to loading page
  lv_obj_scroll_to_view(loading_page, LV_ANIM_ON); 

  //Vbus check 
  ui_update_loading_string(vbus_chk_str[0], 0xFFFFFF, true);
  while (!g_nmaxe.power.is_adc_ready()){
    static uint8_t cnt = 0;
    ui_update_loading_string(vbus_chk_str[(cnt++)%4], 0xFFFFFF, false);
    delay(500);
  }
  
  //Vbus type check, DC or USB 
  if(g_nmaxe.power.is_dc_pluged()) ui_update_loading_string("DC pluged.", 0x00ff00, true);
  else ui_update_loading_string("USB pluged.", 0x00ff00, true);
  delay(500);

  //Vbus type check and voltage check
  while(g_nmaxe.power.get_vbus() < VBUS_MIN_VOLTAGE){
      static bool blink = false;
      uint32_t color = (blink) ? 0xFF0000 : 0xFFFFFF;
      String vbusString = "Vbus " + String(g_nmaxe.power.get_vbus()/1000.0, 1) + "v(at least" + String(VBUS_MIN_VOLTAGE / 1000.0, 1) + "v)";
      ui_update_loading_string(vbusString, color, false);
      blink = !blink;
      if(!g_nmaxe.power.is_dc_pluged()){
        disable_usb_uart();//disable usb uart to fit for typeA port PD , such as Apple divider 3/BC1.2 SDP/CDP/DCP protocol
        delay(500);
      }
      delay(500);
  }
  ui_update_loading_string("Vbus " + String(g_nmaxe.power.get_vbus() / 1000.0, 3) + "V.", 0x00FF00, true);
  delay(500);

  //Vcore check 
  ui_update_loading_string("Vcore check...", 0xFFFFFF, true);
  delay(500);
  //Vcore voltage check
  while(!g_nmaxe.power.is_vcore_good()){
    static bool blink = false;
    uint32_t color = (blink) ? 0xFF0000 : 0xFFFFFF;
    ui_update_loading_string("Vcore error.", color, false);
    blink = !blink;
    delay(500);
  }
  delay(200);//wait for vcore set to target voltage
  ui_update_loading_string(String("Vcore ") + String(g_nmaxe.power.get_vcore() / 1000.0, 3) + "v.", 0x00FF00, true);
  delay(500);

  //Loading status
  ui_update_loading_string("Loading...", 0xFFFFFF, true);
  delay(500);
  /***************************************wait for wifi connected****************************************/
  while(g_nmaxe.connection.wifi.status_param.status != WL_CONNECTED){
    static uint8_t cnt = 0;
    ui_update_loading_string(wifi_con_str[(cnt++)%4], 0xFFFFFF, false);
    if(xSemaphoreTake(g_nmaxe.connection.wifi.force_cfg_xsem, 100) == pdTRUE){
      ui_update_loading_string(String("Timeout!"), 0xFF0000, false);
      delay(500);
      //config background
      lv_obj_scroll_to_view(config_page, LV_ANIM_ON);

      //config label
      const lv_font_t *font = &lv_font_montserrat_12;
      lv_color_t font_color = lv_color_hex(0xFFFFFF);
      lv_obj_t *lb_cfg = lv_label_create(config_page);
      String str = "ssid:" + g_nmaxe.connection.wifi.conn_param.hostname + "\r\npwd: " + g_nmaxe.connection.wifi.softap_param.pwd + "\r\nlogin:"+ g_nmaxe.connection.wifi.softap_param.ip.toString();

      lv_obj_set_width(lb_cfg, 120);
      lv_label_set_text(lb_cfg, str.c_str());
      lv_obj_set_style_text_font(lb_cfg, font, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_cfg, font_color, LV_PART_MAIN);
      lv_label_set_long_mode(lb_cfg, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_line_space(lb_cfg, 0, LV_PART_MAIN); 
      lv_obj_align(lb_cfg, LV_ALIGN_LEFT_MID, 8, 45);

      //QR code
      lv_obj_t *qrcode = lv_qrcode_create(config_page, SCREEN_HEIGHT - 30, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
      String qr_str = "WIFI:T:WPA;S:" + g_nmaxe.connection.wifi.conn_param.hostname + ";P:" + g_nmaxe.connection.wifi.softap_param.pwd + ";H:false;";
      lv_qrcode_update(qrcode, (uint8_t*)qr_str.c_str(), qr_str.length());
      lv_obj_align(qrcode, LV_ALIGN_RIGHT_MID, 0, 0);

      while (true){
        lv_label_set_text(lb_cfg_timeout, (String(g_nmaxe.connection.wifi.status_param.config_timeout) + "s").c_str());
        delay(1000);//wait for configuration and miner will restart after configuration
      }
    }
  }
  ui_update_loading_string("Connected.", 0x00FF00, true);
  delay(500);
  /***************************************wait fan self test *******************************************/
  ui_update_loading_string(fan_test_str[0], 0xFFFFFF, true);
  while(!g_nmaxe.fan.self_test){
    static uint8_t cnt = 0;
    ui_update_loading_string(String(fan_test_str[cnt++ % 4]) + String(g_nmaxe.fan.rpm) + "/ " + String(FAN_FULL_RPM_MIN) + "rpm", 0xFFFFFF, false);
    delay(300);
  }
  ui_update_loading_string("Pass! [" + String(g_nmaxe.fan.rpm) + "/ " + String(FAN_FULL_RPM_MIN) + " rpm]", 0x00FF00, true);
  delay(3000);
  /***************************************wait for market connected************************************/
  ui_update_loading_string(market_con_str[0], 0xFFFFFF, true);
  while(!g_nmaxe.market.connected){
    static uint8_t cnt = 0;
    ui_update_loading_string(market_con_str[cnt++ % 4] , 0xFFFFFF, false);
    delay(300);
  }
  ui_update_loading_string("Connected!", 0x00FF00, true);
  delay(1000);
  /***************************************wait for pool connected**************************************/
  while(!g_nmaxe.stratum.is_subscribed()){
    static uint8_t cnt = 0;
    ui_update_loading_string(pool_con_str[(cnt++)%4], 0xFFFFFF, false);
    delay(300);
  }
  ui_update_loading_string("Connected.", 0x00FF00, true);
  delay(500);

  //wait for miner ready status update signal, blockingly
  xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, portMAX_DELAY);

  //scroll to miner page
  lv_obj_scroll_to_view(miner_page, LV_ANIM_ON); 

  lv_color_t font_color;
  while (true){
    //wait for miner status update forever
    xSemaphoreTake(g_nmaxe.mstatus.update_xsem, portMAX_DELAY);
    if(xSemaphoreTake(lvgl_xMutex, 0) == pdTRUE){
      String uptime = convert_uptime_to_string(g_nmaxe.mstatus.uptime);
      String hashrate = formatNumber(g_nmaxe.mstatus.hashrate, 3);
      String hashuint = (g_nmaxe.mstatus.hashrate > 0) ? (String(hashrate.charAt(hashrate.length() - 1)) + "H/s") : "";
      String last_diff = formatNumber(g_nmaxe.mstatus.last_diff, 1);
      String best_session = formatNumber(g_nmaxe.mstatus.best_session, 1);
      String best_ever = formatNumber(g_nmaxe.mstatus.best_ever, 1);
      String network_diff = formatNumber(g_nmaxe.mstatus.network_diff, 4);
      String voltage = formatNumber(g_nmaxe.board.vbus/1000.0, 3);
      String power = formatNumber(g_nmaxe.board.vbus*g_nmaxe.board.ibus/1000.0/1000.0, 3);
      String price = String(g_nmaxe.market.price, 1);

      //diff symbol color update
      if(g_nmaxe.mstatus.last_diff != 0){//avoid the first time update
        if(g_nmaxe.mstatus.last_diff == g_nmaxe.mstatus.best_session) font_color = lv_color_hex(0x00ff00);//green
        else if(g_nmaxe.mstatus.last_diff == g_nmaxe.mstatus.best_ever) font_color = lv_color_hex(0xffa500);//yellow
        else font_color = lv_color_hex(0xA9A9A9);//gray
        lv_obj_set_style_text_color(lb_diff_symbol, font_color, LV_PART_MAIN); 
      }

      //temp symbol color update
      if(g_nmaxe.board.temp_vcore >= VCORE_TEMP_DANGER) font_color = lv_color_hex(0xff0000);//red
      else if(g_nmaxe.board.temp_vcore >= (VCORE_TEMP_DANGER - 20)) font_color = lv_color_hex(0xffa500);//yellow
      else font_color = lv_color_hex(0x00ff00);//green
      lv_obj_set_style_text_color(lb_temp_symb, font_color, LV_PART_MAIN); 

      //wifi rssi symbol color update
      if(g_nmaxe.connection.wifi.status_param.rssi >= WIFI_RSSI_STRONG) font_color = lv_color_hex(0x00ff00);//green
      else if(g_nmaxe.connection.wifi.status_param.rssi >= WIFI_RSSI_GOOD) font_color = lv_color_hex(0xffa500);//yellow
      else  font_color = lv_color_hex(0xff0000);//red
      lv_obj_set_style_text_color(lb_wifi_symbol, font_color, LV_PART_MAIN); 

      //share symbol color update
      static uint32_t last_share_cnt = g_nmaxe.mstatus.share_accepted;
      if(last_share_cnt != g_nmaxe.mstatus.share_accepted){
        font_color = lv_color_hex(0x00ff00);//green
        last_share_cnt = g_nmaxe.mstatus.share_accepted;
      }
      else font_color = lv_color_hex(0xA9A9A9);//gray
      lv_obj_set_style_text_color(lb_share_symb, font_color, LV_PART_MAIN);

      //fan symbol color update, blink
      static bool fan_color_update = false;
      if(g_nmaxe.fan.rpm > 0){
        if(fan_color_update)font_color = lv_color_hex(0xA9A9A9);
        else font_color = lv_color_hex(0x00ff00);
        fan_color_update =!fan_color_update;
      }
      else if(g_nmaxe.fan.rpm == 0) font_color = lv_color_hex(0xA9A9A9);//gray
      lv_obj_set_style_text_color(lb_fan_symb, font_color, LV_PART_MAIN);

      //price color update, blink
      static float last_price = g_nmaxe.market.price;
      if(last_price != g_nmaxe.market.price){
        font_color = lv_color_hex(0x00ff00);//green
        last_price = g_nmaxe.market.price;
      }
      else font_color = lv_color_hex(0xFFFFFF);//white
      lv_obj_set_style_text_color(lb_price, font_color, LV_PART_MAIN);



      //ota progress layout
      if(g_nmaxe.ota.ota_running){
        ui_update_ota_bar(g_nmaxe.ota.progress);
      }

      //hashrate
      lv_label_set_text_fmt(lb_hashrate, "%s", hashrate.substring(0, hashrate.length() - 1).c_str());
      //hashrate unit
      lv_label_set_text_fmt(lb_hr_unit, "%s", hashuint.c_str());
      //block hit
      lv_label_set_text_fmt(lb_blk_hit, "%d", g_nmaxe.mstatus.block_hits);
      //Diff
      lv_label_set_text_fmt(lb_diff, "%s/%s/%s", last_diff.c_str(), best_session.c_str(), best_ever.c_str());
      //Temp
      lv_label_set_text_fmt(lb_temp,   "%s'C/%s'C", formatNumber(g_nmaxe.board.temp_vcore, 2).c_str(), formatNumber(g_nmaxe.asic.temp, 2).c_str());
      //WiFi
      lv_label_set_text_fmt(lb_wifi,   "%s / %d", g_nmaxe.connection.wifi.status_param.ip.toString().c_str(), g_nmaxe.connection.wifi.status_param.rssi);
      //uptime hms
      lv_label_set_text_fmt(lb_uptime_hms,    "%s", uptime.substring(5, uptime.length()).c_str());
      //uptime day
      lv_label_set_text_fmt(lb_uptime_day,    "%s", uptime.substring(0,3).c_str());
      //share
      lv_label_set_text_fmt(lb_share,  "%d/%d", g_nmaxe.mstatus.share_rejected, g_nmaxe.mstatus.share_accepted);
      //fan
      lv_label_set_text_fmt(lb_fan, "%d rpm", g_nmaxe.fan.rpm);
      //price
      lv_label_set_text_fmt(lb_price,  "$%s", price.c_str());
      //power
      lv_label_set_text_fmt(lb_power,  "%sV/%sW", voltage.c_str(), power.c_str()); 
      //release mutex
      xSemaphoreGive(lvgl_xMutex); 
    }
  }
}
#include "display.h"
#include "logger.h"
#include <TFT_eSPI.h>
#include "global.h"
#include "helper.h" 
#include "image.h"

/******************************************************************* global state variables ******************************************************************/
static uint16_t SCREEN_WIDTH  = 0;
static uint16_t SCREEN_HEIGHT = 0;
static TFT_eSPI *tftDriver = nullptr;

static lv_obj_t *ui_pages[] = {NULL, NULL, NULL, NULL, NULL, NULL};
static SemaphoreHandle_t lvgl_xMutex = xSemaphoreCreateMutex();

LV_FONT_DECLARE(ds_digib_font_16)
LV_FONT_DECLARE(ds_digib_font_18)
LV_FONT_DECLARE(ds_digib_font_20)
LV_FONT_DECLARE(ds_digib_font_24)
LV_FONT_DECLARE(ds_digib_font_28)
LV_FONT_DECLARE(ds_digib_font_36)
LV_FONT_DECLARE(ds_digib_font_38)
LV_FONT_DECLARE(ds_digib_font_42)
LV_FONT_DECLARE(ds_digib_font_50)
LV_FONT_DECLARE(ds_digib_font_56)
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

  ui_element_t  lb_cfg_timeout;
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
}dashboard_page;

// hashrate health page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
}hr_health_page;

// big digit page elements
struct{
  lv_obj_t      *container;
  lv_obj_t      *back_img_obj;
  lv_img_dsc_t  *back_img_dsc;
}big_digit_page;


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

static void tft_init(){
  SCREEN_WIDTH  = g_board.info.spec.tft.height;
  SCREEN_HEIGHT = g_board.info.spec.tft.width;
  // Power on TFT
  if(g_board.info.spec.tft.pwr_pin >= 0){
    pinMode(g_board.info.spec.tft.pwr_pin, OUTPUT);
    digitalWrite(g_board.info.spec.tft.pwr_pin, LOW);
    delay(10); //wait for tft power stable
  }
  // Initialize backlight PWM
  if(g_board.info.spec.tft.bl.pin >= 0){
    pinMode(g_board.info.spec.tft.bl.pin, OUTPUT);
    ledcSetup(g_board.info.spec.tft.bl.pwm_ch, g_board.info.spec.tft.bl.pwm_freq, g_board.info.spec.tft.bl.pwm_resolution);
    ledcAttachPin(g_board.info.spec.tft.bl.pin, g_board.info.spec.tft.bl.pwm_ch);
    tft_bl_ctrl(0);//sleep when boot up
  }

  tftDriver = new TFT_eSPI(SCREEN_HEIGHT, SCREEN_WIDTH);
  if(!tftDriver){
    LOG_E("Failed to create TFT_eSPI instance!");
    return;
  }
  else{
    LOG_I("TFT_eSPI instance created, screen size: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
  }

  tftDriver->begin(g_board.info.spec.spi.cs_pin, 
                  g_board.info.spec.tft.dc_pin,
                  g_board.info.spec.tft.rst_pin, 
                  g_board.info.spec.spi.sclk_pin,
                  g_board.info.spec.spi.miso_pin,
                  g_board.info.spec.spi.mosi_pin,
                  g_board.info.spec.tft.color_invert
                );
                
  if(g_board.status.preference.screen.flip)tftDriver->setRotation(1); 
  else tftDriver->setRotation(g_board.info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME ? 4 : 3); // NMQAxe++ use rotation 4, NMaxE use rotation 3
}

static void disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ){
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tftDriver->startWrite();
    tftDriver->setAddrWindow( area->x1, area->y1, w, h );
    tftDriver->pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tftDriver->endWrite();

    lv_disp_flush_ready(disp_drv);
}

static void ui_drv_register(void){
  LOG_I("lvgl version: %s", (String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch()).c_str());

  static lv_disp_draw_buf_t lvgl_draw_buf;
  static lv_disp_drv_t      disp_drv;
  
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
        LOG_I("✓ LVGL display buffer allocated in PSRAM: %d bytes at 0x%p", buffer_bytes, color_buf);
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
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &lvgl_draw_buf;
  lv_disp_drv_register( &disp_drv );
}

static void ui_page_element_init(board_sal_t* board){
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

  status_page_img_240_320.header.w = SCREEN_WIDTH;
  status_page_img_240_320.header.h = SCREEN_HEIGHT;
  status_page_img_240_320.data_size = SCREEN_WIDTH * SCREEN_HEIGHT * LV_COLOR_SIZE / 8;


  if((board->info.spec.name == BOARD_NMAXE_NAME) || (board->info.spec.name  == BOARD_NMAXE_GAMMA_NAME)){
    loading_page.back_img_dsc           = &loading_page_img_135_240;
    config_page.back_img_dsc            = &config_page_img_135_240;
    miner_page.back_img_dsc             = &mining_page_img_135_240;
    dashboard_page.back_img_dsc         = &status_page_img_135_240;
    hr_health_page.back_img_dsc         = &status_page_img_135_240;
    big_digit_page.back_img_dsc         = &black_page_img_135_240;
    miner_page.logo_img_dsc             = (board->info.spec.name == BOARD_NMAXE_NAME) ? &logo_worker_nmaxe : &logo_worker_nmaxegamma;
    config_page.logo_img_dsc            = (board->info.spec.name == BOARD_NMAXE_NAME) ? &logo_worker_nmaxe : &logo_worker_nmaxegamma;

    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {(lv_coord_t)(SCREEN_WIDTH - (uint16_t)(board->info.base.fw_version.length() * 9)), (lv_coord_t)(SCREEN_HEIGHT - 18) };
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 10};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_16;
    loading_page.lb_pool_url.coord      = {0, 35};
    /*********************************** Loading page *********************************/
    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {(lv_coord_t)(SCREEN_WIDTH - (uint16_t)(board->info.base.fw_version.length() * 9)), (lv_coord_t)(SCREEN_HEIGHT - 18) };
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 10};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_16;
    loading_page.lb_pool_url.coord      = {0, 35};
    /*********************************** Config page *********************************/
    config_page.img_logo.coord          = {20, 1};
    config_page.lb_cfg_timeout.font     = &lv_font_montserrat_14;
    config_page.lb_cfg_timeout.coord    = {175, 0 }; 

    /*********************************** mining page *********************************/
    miner_page.img_logo.coord           = {45, 20};
    miner_page.lb_share.font            = &ds_digib_font_18;
    miner_page.lb_share.coord           = {132, 41};
    miner_page.lb_hr_unit.font          = &ds_digib_font_28;
    miner_page.lb_hr_unit.coord         = {182, 110};
    miner_page.lb_blk_hit.font          = &ds_digib_font_50;
    miner_page.lb_blk_hit.coord         = {7, 39};
    miner_page.lb_fan_symb.font         = &symbol_14;
    miner_page.lb_fan_symb.coord        = {110, 76};
    miner_page.lb_hashrate.font         = &ds_digib_font_38;
    miner_page.lb_hashrate.coord        = {40, 0};
    miner_page.lb_price.font            = &ds_digib_font_20;
    miner_page.lb_price.coord           = {33, 29};
    miner_page.lb_diff.font             = &ds_digib_font_18;
    miner_page.lb_diff.coord            = {132, 25};
    miner_page.lb_ver.font              = &ds_digib_font_16;
    miner_page.lb_ver.coord             = {2, 22};
    miner_page.lb_power.font            = &ds_digib_font_18;
    miner_page.lb_power.coord           = {10, 114};
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
  }
  else if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    loading_page.back_img_dsc           = &loading_page_img_240_320;
    config_page.back_img_dsc            = &config_page_img_240_320;
    miner_page.back_img_dsc             = &mining_page_img_240_320;
    dashboard_page.back_img_dsc         = &status_page_img_240_320;
    hr_health_page.back_img_dsc         = &status_page_img_240_320;
    big_digit_page.back_img_dsc         = &black_page_img_240_320;

    miner_page.logo_img_dsc             = &logo_worker_nmqaxepp;
    config_page.logo_img_dsc            = &logo_worker_nmqaxepp;

    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {(lv_coord_t)(SCREEN_WIDTH - (uint16_t)(board->info.base.fw_version.length() * 9)), (lv_coord_t)(SCREEN_HEIGHT - 18) };
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 10};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_16;
    loading_page.lb_pool_url.coord      = {0, 35};
    /*********************************** Loading page *********************************/
    loading_page.lb_details.font        = &lv_font_montserrat_14;
    loading_page.lb_details.coord       = {3, 0};
    loading_page.lb_version.font        = &lv_font_montserrat_16;
    loading_page.lb_version.coord       = {(lv_coord_t)(SCREEN_WIDTH - (uint16_t)(board->info.base.fw_version.length() * 9)), (lv_coord_t)(SCREEN_HEIGHT - 18) };
    loading_page.lb_progress.font       = &lv_font_montserrat_16;
    loading_page.lb_progress.coord      = {0, -20};
    loading_page.bar_progress.font      = &lv_font_montserrat_20;
    loading_page.bar_progress.coord     = {0, -20};
    loading_page.lb_ip_and_slogan.font  = &lv_font_montserrat_20;
    loading_page.lb_ip_and_slogan.coord = {0, 10};
    loading_page.lb_pool_url.font       = &lv_font_montserrat_16;
    loading_page.lb_pool_url.coord      = {0, 35};
    /*********************************** Config page *********************************/
    config_page.img_logo.coord          = {20, 1};
    config_page.lb_cfg_timeout.font     = &lv_font_montserrat_14;
    config_page.lb_cfg_timeout.coord    = {175, 0 }; 

    /*********************************** mining page *********************************/
    miner_page.img_logo.coord           = {70, 44};
    miner_page.lb_hr_unit.font          = &ds_digib_font_28;
    miner_page.lb_hr_unit.coord         = {255 , 165};
    miner_page.lb_blk_hit.font          = &ds_digib_font_56;
    miner_page.lb_blk_hit.coord         = {20, 65};
    miner_page.lb_hashrate.font         = &ds_digib_font_42;
    miner_page.lb_hashrate.coord        = {50, -50};
    miner_page.lb_price.font            = &ds_digib_font_20;
    miner_page.lb_price.coord           = {33, 25};
    miner_page.lb_ver.font              = &ds_digib_font_16;
    miner_page.lb_ver.coord             = {15, 35};
    miner_page.lb_power.font            = &ds_digib_font_24;
    miner_page.lb_power.coord           = {30, 168};
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

    miner_page.lb_diff.font             = &ds_digib_font_20;
    miner_page.lb_diff.coord            = {132+ 55, 30};
    miner_page.lb_share.font            = &ds_digib_font_20;
    miner_page.lb_share.coord           = {132+ 55, 55};
    miner_page.lb_temp.font             = &ds_digib_font_20;
    miner_page.lb_temp.coord            = {132+ 55, 83};
    miner_page.lb_fan.font              = &ds_digib_font_20;
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
    miner_page.lb_utc_time.coord         = {3, 3};

    miner_page.lb_swarm_best_diff.font      = &ds_digib_font_24;
    miner_page.lb_swarm_best_diff.coord     = {3, 210};
    miner_page.lb_swarm_workers.font        = &ds_digib_font_24;
    miner_page.lb_swarm_workers.coord       = {145, 210};
    miner_page.lb_swarm_total_hashrate.font = &ds_digib_font_24;
    miner_page.lb_swarm_total_hashrate.coord= {237, 210}; 
  }
  else{
      LOG_E("Unknown board type for UI layout init: %s", board->info.spec.name);
  }
}

static void ui_layout_init(board_sal_t* board){
  static lv_obj_t *parent_docker = NULL;

  //wait a bit for lvgl tick task to start, necessary for lvgl to work properly
  delay(10);
  //create parent object
  parent_docker = lv_obj_create(lv_scr_act());
  lv_obj_set_size(parent_docker, SCREEN_WIDTH * 3, SCREEN_HEIGHT * 2); 
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
  loading_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(loading_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(loading_page.container, 0 * SCREEN_WIDTH, 0);
  lv_obj_set_style_pad_all(loading_page.container, 0, 0);
  lv_obj_set_style_border_width(loading_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(loading_page.container, LV_SCROLLBAR_MODE_OFF); 
  loading_page.back_img_obj = lv_img_create(loading_page.container);
  lv_img_set_src(loading_page.back_img_obj, loading_page.back_img_dsc);
  lv_obj_set_size(loading_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(loading_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create config page
  config_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(config_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(config_page.container, 0, SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(config_page.container, 0, 0);
  lv_obj_set_style_border_width(config_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(config_page.container, LV_SCROLLBAR_MODE_OFF); 
  config_page.back_img_obj = lv_img_create(config_page.container);//
  lv_img_set_src(config_page.back_img_obj, config_page.back_img_dsc);
  lv_obj_set_size(config_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(config_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  config_page.img_logo.obj = lv_img_create(config_page.container);//worker logo
  lv_img_set_src(config_page.img_logo.obj, config_page.logo_img_dsc); 
  lv_obj_align(config_page.img_logo.obj, LV_ALIGN_TOP_LEFT, config_page.img_logo.coord.x, config_page.img_logo.coord.y);
  // Create miner page  
  miner_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(miner_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(miner_page.container, 1 * SCREEN_WIDTH, 0);
  lv_obj_set_style_pad_all(miner_page.container, 0, 0);
  lv_obj_set_style_border_width(miner_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(miner_page.container, LV_SCROLLBAR_MODE_OFF);
  miner_page.back_img_obj = lv_img_create(miner_page.container);
  lv_img_set_src(miner_page.back_img_obj, miner_page.back_img_dsc);
  lv_obj_set_size(miner_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(miner_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  miner_page.img_logo.obj = lv_img_create(miner_page.container);//worker logo
  lv_img_set_src(miner_page.img_logo.obj, miner_page.logo_img_dsc); 
  lv_obj_align(miner_page.img_logo.obj, LV_ALIGN_TOP_LEFT, miner_page.img_logo.coord.x, miner_page.img_logo.coord.y);
  // Create dashboard page  
  dashboard_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(dashboard_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(dashboard_page.container, 1 * SCREEN_WIDTH, 1 * SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(dashboard_page.container, 0, 0);
  lv_obj_set_style_border_width(dashboard_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(dashboard_page.container, LV_SCROLLBAR_MODE_OFF);
  dashboard_page.back_img_obj = lv_img_create(dashboard_page.container);
  lv_img_set_src(dashboard_page.back_img_obj, dashboard_page.back_img_dsc);
  lv_obj_set_size(dashboard_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(dashboard_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create health page  
  hr_health_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(hr_health_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(hr_health_page.container, 2 * SCREEN_WIDTH, 1 * SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(hr_health_page.container, 0, 0);
  lv_obj_set_style_border_width(hr_health_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(hr_health_page.container, LV_SCROLLBAR_MODE_OFF);
  hr_health_page.back_img_obj = lv_img_create(hr_health_page.container);
  lv_img_set_src(hr_health_page.back_img_obj, hr_health_page.back_img_dsc);
  lv_obj_set_size(hr_health_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(hr_health_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create big digit page
  big_digit_page.container = lv_obj_create(parent_docker);
  lv_obj_set_size(big_digit_page.container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(big_digit_page.container, 2 * SCREEN_WIDTH, 0 * SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(big_digit_page.container, 0, 0);
  lv_obj_set_style_border_width(big_digit_page.container, 0, 0);
  lv_obj_set_scrollbar_mode(big_digit_page.container, LV_SCROLLBAR_MODE_OFF);
  big_digit_page.back_img_obj = lv_img_create(big_digit_page.container);
  lv_img_set_src(big_digit_page.back_img_obj, big_digit_page.back_img_dsc);
  lv_obj_set_size(big_digit_page.back_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_align(big_digit_page.back_img_obj, LV_ALIGN_TOP_LEFT, 0, 0);
  // Create ui_pages array
  ui_pages[0] = loading_page.container;
  ui_pages[1] = config_page.container;
  ui_pages[2] = miner_page.container;
  ui_pages[3] = dashboard_page.container;
  ui_pages[4] = hr_health_page.container;
  ui_pages[5] = big_digit_page.container;
  //////////////////////////////////////loading page layout///////////////////////////////////////////////
  //Version label
  lv_color_t font_color = lv_color_hex(0xFFFFFF);
  loading_page.lb_version.obj   = lv_label_create( loading_page.container );
  lv_obj_set_width(loading_page.lb_version.obj, SCREEN_WIDTH);
  lv_label_set_text( loading_page.lb_version.obj, g_board.info.base.fw_version.c_str());
  lv_obj_set_style_text_font(loading_page.lb_version.obj, loading_page.lb_version.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_version.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_version.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( loading_page.lb_version.obj, LV_ALIGN_TOP_MID, loading_page.lb_version.coord.x, loading_page.lb_version.coord.y);

  //details label
  loading_page.lb_details.obj = lv_label_create(loading_page.container);
  lv_obj_set_width(loading_page.lb_details.obj, SCREEN_WIDTH - (uint16_t)(g_board.info.base.fw_version.length() * 7.2));
  lv_obj_set_style_text_font(loading_page.lb_details.obj, loading_page.lb_details.font, LV_PART_MAIN);
  lv_label_set_long_mode(loading_page.lb_details.obj, LV_LABEL_LONG_DOT);
  lv_obj_align(loading_page.lb_details.obj, LV_ALIGN_BOTTOM_LEFT, loading_page.lb_details.coord.x, loading_page.lb_details.coord.y);

  //bar progress
  loading_page.bar_progress.obj = lv_bar_create(loading_page.container);
  lv_bar_set_range(loading_page.bar_progress.obj, 0, 16);
  lv_bar_set_value(loading_page.bar_progress.obj, 0, LV_ANIM_ON);
  lv_obj_set_style_bg_opa(loading_page.bar_progress.obj, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_size(loading_page.bar_progress.obj, SCREEN_WIDTH * 0.9, 5);
  lv_obj_align(loading_page.bar_progress.obj, LV_ALIGN_CENTER, loading_page.bar_progress.coord.x, loading_page.bar_progress.coord.y);
  lv_obj_set_style_bg_color(loading_page.bar_progress.obj, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(loading_page.bar_progress.obj, LV_OPA_COVER, LV_PART_INDICATOR);

  //progress label
  loading_page.lb_progress.obj = lv_label_create(loading_page.container);
  lv_label_set_text(loading_page.lb_progress.obj, "");
  lv_obj_set_style_text_color(loading_page.lb_progress.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_align(loading_page.lb_progress.obj, LV_ALIGN_LEFT_MID, loading_page.lb_progress.coord.x, loading_page.lb_progress.coord.y);

  //slogan label
  loading_page.lb_ip_and_slogan.obj   = lv_label_create(loading_page.container);
  String slogan_str = "Make it better";
  lv_coord_t width = lv_txt_get_width(slogan_str.c_str(), strlen(slogan_str.c_str()), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(loading_page.lb_ip_and_slogan.obj, width);
  lv_label_set_text( loading_page.lb_ip_and_slogan.obj, slogan_str.c_str());
  lv_obj_set_style_text_font(loading_page.lb_ip_and_slogan.obj, loading_page.lb_ip_and_slogan.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_ip_and_slogan.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_ip_and_slogan.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( loading_page.lb_ip_and_slogan.obj, LV_ALIGN_CENTER, loading_page.lb_ip_and_slogan.coord.x, loading_page.lb_ip_and_slogan.coord.y);

  //pool url label
  loading_page.lb_pool_url.obj   = lv_label_create(loading_page.container);
  lv_label_set_text( loading_page.lb_pool_url.obj, "");
  lv_obj_set_style_text_font(loading_page.lb_pool_url.obj, loading_page.lb_pool_url.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(loading_page.lb_pool_url.obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); 
  lv_label_set_long_mode(loading_page.lb_pool_url.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( loading_page.lb_pool_url.obj, LV_ALIGN_CENTER, loading_page.lb_pool_url.coord.x, loading_page.lb_pool_url.coord.y);
  //////////////////////////////////////config page layout///////////////////////////////////////////////
  //config timeout label
  font_color = lv_color_hex(0xFFFFFF);
  config_page.lb_cfg_timeout.obj   = lv_label_create( config_page.container );
  lv_obj_set_width(config_page.lb_cfg_timeout.obj, SCREEN_WIDTH);
  lv_label_set_text( config_page.lb_cfg_timeout.obj, "");
  lv_obj_set_style_text_font(config_page.lb_cfg_timeout.obj, config_page.lb_cfg_timeout.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(config_page.lb_cfg_timeout.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(config_page.lb_cfg_timeout.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, config_page.lb_cfg_timeout.coord.x, config_page.lb_cfg_timeout.coord.y);

  //////////////////////////////////////miner page layout///////////////////////////////////////////////
  //Hashrate value
  font_color = lv_color_hex(0xEE7D30);
  miner_page.lb_hashrate.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_hashrate.obj, 80);
  lv_label_set_text( miner_page.lb_hashrate.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_hashrate.obj, miner_page.lb_hashrate.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_hashrate.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_hashrate.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( miner_page.lb_hashrate.obj, LV_ALIGN_BOTTOM_MID, miner_page.lb_hashrate.coord.x, miner_page.lb_hashrate.coord.y);
  //Hit value
  font_color = lv_color_hex(0xEE7D30);
  miner_page.lb_blk_hit.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_blk_hit.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_blk_hit.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, miner_page.lb_blk_hit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_blk_hit.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_blk_hit.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x, miner_page.lb_blk_hit.coord.y); 
  //price value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_price.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_price.obj , SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_price.obj , "");
  lv_obj_set_style_text_font(miner_page.lb_price.obj , miner_page.lb_price.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_price.obj , LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_price.obj , font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_price.obj , LV_ALIGN_LEFT_MID, miner_page.lb_price.coord.x, miner_page.lb_price.coord.y ); 
  //version value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_ver.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_ver.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_ver.obj, board->info.base.fw_version.substring(1, board->info.base.fw_version.length()).c_str());
  lv_obj_set_style_text_font(miner_page.lb_ver.obj, miner_page.lb_ver.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_ver.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_ver.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_ver.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_ver.coord.x, miner_page.lb_ver.coord.y );
  //power value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_power.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_power.obj, SCREEN_WIDTH/2.5);
  lv_label_set_text( miner_page.lb_power.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_power.obj, miner_page.lb_power.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_power.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_power.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_power.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_power.coord.x, miner_page.lb_power.coord.y ); 
  //wifi value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_ip.obj    = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_ip.obj, SCREEN_WIDTH/2.5);
  lv_label_set_text( miner_page.lb_ip.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_ip.obj, miner_page.lb_ip.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_ip.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(miner_page.lb_ip.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_ip.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_ip.coord.x, miner_page.lb_ip.coord.y );

  //uptime value, hour , minute, second
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_uptime_hms.obj    = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_hms.obj, 88);
  lv_label_set_text( miner_page.lb_uptime_hms.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_uptime_hms.obj, miner_page.lb_uptime_hms.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_hms.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_hms.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_hms.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_hms.coord.x, miner_page.lb_uptime_hms.coord.y);
  //uptime value, day
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_uptime_day.obj    = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_day.obj, 88);
  lv_label_set_text( miner_page.lb_uptime_day.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_uptime_day.obj, miner_page.lb_uptime_day.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_day.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_day.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_day.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_day.coord.x, miner_page.lb_uptime_day.coord.y );
  //uptime day unit  
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_uptime_day_unit.obj    = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_day_unit.obj, 20);
  lv_label_set_text( miner_page.lb_uptime_day_unit.obj, "d");
  lv_obj_set_style_text_font(miner_page.lb_uptime_day_unit.obj, miner_page.lb_uptime_day_unit.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_uptime_day_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_uptime_day_unit.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_uptime_day_unit.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_uptime_day_unit.coord.x, miner_page.lb_uptime_day_unit.coord.y );

  //Diff value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_diff.obj    = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_diff.obj, SCREEN_WIDTH/2.4);
  lv_label_set_text( miner_page.lb_diff.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_diff.obj, miner_page.lb_diff.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_diff.obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(miner_page.lb_diff.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_diff.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_diff.coord.x, miner_page.lb_diff.coord.y );
  //share value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_share.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_share.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_share.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_share.obj, miner_page.lb_share.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_share.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_share.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_share.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_share.coord.x, miner_page.lb_share.coord.y);
  //temp value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_temp.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_temp.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_temp.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_temp.obj, miner_page.lb_temp.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_temp.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_temp.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_temp.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_temp.coord.x, miner_page.lb_temp.coord.y );
  //Fan value
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_fan.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_fan.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_fan.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_fan.obj, miner_page.lb_fan.font, LV_PART_MAIN);
  lv_label_set_long_mode(miner_page.lb_fan.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(miner_page.lb_fan.obj, font_color, LV_PART_MAIN); 
  lv_obj_align( miner_page.lb_fan.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_fan.coord.x, miner_page.lb_fan.coord.y); 
  //Hashrate uint
  font_color = lv_color_hex(0xFFFFFF);
  miner_page.lb_hr_unit.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_hr_unit.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_hr_unit.obj, " ");
  lv_obj_set_style_text_font(miner_page.lb_hr_unit.obj, miner_page.lb_hr_unit.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_hr_unit.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_hr_unit.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_hr_unit.obj, LV_ALIGN_TOP_MID, miner_page.lb_hr_unit.coord.x, miner_page.lb_hr_unit.coord.y); 
  // symbol uptime
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_uptime_symbol.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_uptime_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_uptime_symbol.obj, LV_SYMBOL_BELL); 
  lv_obj_set_style_text_font(miner_page.lb_uptime_symbol.obj, miner_page.lb_uptime_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_uptime_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_uptime_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_uptime_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_uptime_symbol.coord.x, miner_page.lb_uptime_symbol.coord.y); 
  // symbol wifi
  font_color = lv_color_hex(0xFFA500);
  miner_page.lb_wifi_symbol.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_wifi_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_wifi_symbol.obj, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(miner_page.lb_wifi_symbol.obj, miner_page.lb_wifi_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_wifi_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_wifi_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_wifi_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_wifi_symbol.coord.x, miner_page.lb_wifi_symbol.coord.y); 

  //diff symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_diff_symbol.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_diff_symbol.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_diff_symbol.obj, "\xEF\x82\x80");
  lv_obj_set_style_text_font(miner_page.lb_diff_symbol.obj, miner_page.lb_diff_symbol.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_diff_symbol.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_diff_symbol.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_diff_symbol.obj, LV_ALIGN_TOP_MID, miner_page.lb_diff_symbol.coord.x, miner_page.lb_diff_symbol.coord.y); 
  // share symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_share_symb.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_share_symb.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_share_symb.obj, "\xEF\x8E\x82");
  lv_obj_set_style_text_font(miner_page.lb_share_symb.obj, miner_page.lb_share_symb.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_share_symb.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_share_symb.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_share_symb.obj, LV_ALIGN_TOP_MID, miner_page.lb_share_symb.coord.x, miner_page.lb_share_symb.coord.y); 
  //temp symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_temp_symb.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
  lv_obj_set_width(miner_page.lb_temp_symb.obj, SCREEN_WIDTH);
  lv_label_set_text( miner_page.lb_temp_symb.obj, "\xEF\x8B\x88");
  lv_obj_set_style_text_font(miner_page.lb_temp_symb.obj, miner_page.lb_temp_symb.font, LV_PART_MAIN);
  lv_obj_set_style_text_color(miner_page.lb_temp_symb.obj, font_color, LV_PART_MAIN); 
  lv_label_set_long_mode(miner_page.lb_temp_symb.obj, LV_LABEL_LONG_DOT);
  lv_obj_align( miner_page.lb_temp_symb.obj, LV_ALIGN_TOP_MID, miner_page.lb_temp_symb.coord.x , miner_page.lb_temp_symb.coord.y); 
  //fan symbol
  font_color = lv_color_hex(0xA9A9A9);
  miner_page.lb_fan_symb.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
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
    miner_page.lb_swarm_best_diff.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_best_diff.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_best_diff.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_best_diff.obj, miner_page.lb_swarm_best_diff.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_best_diff.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_best_diff.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_best_diff.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_best_diff.coord.x, miner_page.lb_swarm_best_diff.coord.y);
    // swarm workers
    font_color = lv_color_hex(0xEE7D30);
    miner_page.lb_swarm_workers.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_workers.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_workers.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_workers.obj, miner_page.lb_swarm_workers.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_workers.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_workers.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_workers.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_workers.coord.x, miner_page.lb_swarm_workers.coord.y);
    // swarm total hashrate
    font_color = lv_color_hex(0xEE7D30);
    miner_page.lb_swarm_total_hashrate.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_swarm_total_hashrate.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_swarm_total_hashrate.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_swarm_total_hashrate.obj, miner_page.lb_swarm_total_hashrate.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_swarm_total_hashrate.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_swarm_total_hashrate.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_swarm_total_hashrate.obj, LV_ALIGN_TOP_MID, miner_page.lb_swarm_total_hashrate.coord.x, miner_page.lb_swarm_total_hashrate.coord.y);

    // utc time label
    font_color = lv_color_hex(0xFFFFFF);
    miner_page.lb_utc_time.obj   = lv_label_create( ui_pages[UI_PAGE_MINER] );
    lv_obj_set_width(miner_page.lb_utc_time.obj, SCREEN_WIDTH);
    lv_label_set_text( miner_page.lb_utc_time.obj, " ");
    lv_obj_set_style_text_font(miner_page.lb_utc_time.obj, miner_page.lb_utc_time.font, LV_PART_MAIN);
    lv_obj_set_style_text_color(miner_page.lb_utc_time.obj, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(miner_page.lb_utc_time.obj, LV_LABEL_LONG_DOT);
    lv_obj_align( miner_page.lb_utc_time.obj, LV_ALIGN_TOP_LEFT, miner_page.lb_utc_time.coord.x, miner_page.lb_utc_time.coord.y);
  }
}

static void ui_loading_str_update(String str, uint32_t color, bool prgress_update) {
    static uint8_t progress = 0 , progress_total = 16;
    static lv_coord_t width = 0;
    lv_color_t font_color = lv_color_hex(color);

    if((WL_CONNECTED == g_board.info.connection.wifi.status_param.status) && (loading_page.lb_ip_and_slogan.obj != NULL)){
      String ip_str = g_board.info.connection.wifi.status_param.ip.toString();
      width = lv_txt_get_width(ip_str.c_str(), strlen(ip_str.c_str()), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE);
      lv_obj_set_width(loading_page.lb_ip_and_slogan.obj, width);
      lv_obj_set_style_text_color(loading_page.lb_ip_and_slogan.obj, lv_color_hex(0x00FF00), LV_PART_MAIN); 
      lv_label_set_text(loading_page.lb_ip_and_slogan.obj, g_board.info.connection.wifi.status_param.ip.toString().c_str());
    }

    if((g_board.market->lastUpdate != 0) && (loading_page.lb_pool_url.obj != NULL)){
      String pool_str = (g_board.info.connection.pool_use.url + ":" + g_board.info.connection.pool_use.port);
      width = lv_txt_get_width(pool_str.c_str(), strlen(pool_str.c_str()), &lv_font_montserrat_16, 0, LV_TEXT_FLAG_NONE);
      width = (width > SCREEN_WIDTH) ? SCREEN_WIDTH : width;
      lv_obj_set_width(loading_page.lb_pool_url.obj, width);
      lv_label_set_text(loading_page.lb_pool_url.obj, (g_board.info.connection.pool_use.url + ":" + g_board.info.connection.pool_use.port).c_str());
    }

    if(prgress_update && (loading_page.bar_progress.obj != NULL) && (loading_page.lb_progress.obj != NULL)){
      lv_coord_t bar_x = lv_obj_get_x(loading_page.bar_progress.obj);
      lv_coord_t bar_w = lv_obj_get_width(loading_page.bar_progress.obj);
      lv_coord_t label_x = bar_x + (bar_w * progress / progress_total) - lv_obj_get_width(loading_page.lb_progress.obj) / 2;
      lv_obj_set_pos(loading_page.lb_progress.obj, label_x, -10);
      lv_label_set_text(loading_page.lb_progress.obj, (String(100 * (float)progress/(float)progress_total, 0) + "%").c_str());
      lv_bar_set_value(loading_page.bar_progress.obj, progress, LV_ANIM_ON);
      progress++;
    }

    if(loading_page.lb_details.obj != NULL){
      lv_obj_set_style_text_color(loading_page.lb_details.obj, font_color, LV_PART_MAIN);
      lv_label_set_text(loading_page.lb_details.obj, str.c_str());
    }
}

static void ui_miner_page_update(board_sal_t* board){
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  lv_color_t font_color = lv_color_hex(0xFFFFFF);


  String uptime = convert_uptime_to_string(board->status.miner.uptime_session);
  String hashrate = formatNumber(board->status.miner.hashrate._3m, 3);
  String hashuint = (board->status.miner.hashrate._3m > 0) ? (String(hashrate.charAt(hashrate.length() - 1)) + "H/s") : "";
  String last_diff = formatNumber(board->status.miner.diff.last, 1);
  String best_session = formatNumber(board->status.miner.diff.best_session, 1);
  String best_ever = formatNumber(board->status.miner.diff.best_ever, 1);
  String network_diff = formatNumber(board->status.miner.diff.network, 2);
  String voltage = formatNumber(board->status.power.vbus/1000.0, 3);
  String power = formatNumber(board->status.power.vbus*board->status.power.ibus/1000.0/1000.0, 3);
  String price = (millis() - board->market->lastUpdate <= MARKET_TIMEOUT) ? formatNumber(board->market->price, 6) : "";
  String fan_and_efficiency = String(board->status.fan.list[0].rpm) + " rpm";
  // String fan_and_efficiency = formatNumber(board->info.efficiency, 4) + "J/TH";

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
  if(board->info.connection.wifi.status_param.rssi >= WIFI_RSSI_STRONG) font_color = lv_color_hex(0x00ff00);//green
  else if(board->info.connection.wifi.status_param.rssi >= WIFI_RSSI_GOOD) font_color = lv_color_hex(0xffa500);//yellow
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
  if(millis() - board->market->lastUpdate <= MARKET_TIMEOUT){
    static float last_price = board->market->price;
    if(last_price != board->market->price){
      font_color = lv_color_hex(0x00ff00);//green
      last_price = board->market->price;
    }
    else font_color = lv_color_hex(0xFFFFFF);//white
    lv_obj_set_style_text_color(miner_page.lb_price.obj, font_color, LV_PART_MAIN);
  }

  //hashrate
  lv_label_set_text_fmt(miner_page.lb_hashrate.obj, "%s", hashrate.substring(0, hashrate.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(miner_page.lb_hr_unit.obj, "%s", hashuint.c_str());
  //block hit
  if(board->status.miner.hits <= 9){
    lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, miner_page.lb_blk_hit.font, LV_PART_MAIN);
    lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x, miner_page.lb_blk_hit.coord.y);
    lv_label_set_text_fmt(miner_page.lb_blk_hit.obj, "%d", board->status.miner.hits);
  }else if (board->status.miner.hits <= 99){
    lv_obj_align( miner_page.lb_blk_hit.obj, LV_ALIGN_TOP_MID, miner_page.lb_blk_hit.coord.x -10, miner_page.lb_blk_hit.coord.y + 11);
    lv_obj_set_style_text_font(miner_page.lb_blk_hit.obj, &ds_digib_font_28, LV_PART_MAIN);
    lv_label_set_text_fmt(miner_page.lb_blk_hit.obj, "%d", board->status.miner.hits);
  }

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
  lv_label_set_text_fmt(miner_page.lb_diff.obj, "%s/%s/%s/%s", last_diff.c_str(), best_session.c_str(), best_ever.c_str(), network_diff.c_str());
  //Temp
  lv_label_set_text_fmt(miner_page.lb_temp.obj,   "%s'C/%s'C", formatNumber(board->status.temp.vcore, 2).c_str(), formatNumber(board->status.temp.asic, 2).c_str());
  //WiFi
  lv_label_set_text_fmt(miner_page.lb_ip.obj,   "%s", board->info.connection.wifi.status_param.ip.toString().c_str());
  //uptime hms
  lv_label_set_text_fmt(miner_page.lb_uptime_hms.obj,    "%s", uptime.substring(5, uptime.length()).c_str());
  //uptime day
  lv_label_set_text_fmt(miner_page.lb_uptime_day.obj,    "%s", uptime.substring(0,3).c_str());
  //share
  lv_label_set_text_fmt(miner_page.lb_share.obj,  "%d/%d", board->status.miner.share_rejected, board->status.miner.share_accepted);
  //fan
  lv_label_set_text_fmt(miner_page.lb_fan.obj, "%s", fan_and_efficiency.c_str());
  //price
  if(millis() - board->market->lastUpdate <= MARKET_TIMEOUT){
    lv_label_set_text_fmt(miner_page.lb_price.obj,  "$%s", price.c_str());
  }
  //power
  lv_label_set_text_fmt(miner_page.lb_power.obj,  "%sV/%sW", voltage.c_str(), power.c_str()); 


  // only for NMQ AXE ++
  if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
    if(miner_page.lb_swarm_best_diff.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_best_diff.obj, "%s", formatNumber(board->status.swarm.best_diff, 4).c_str());
    }
    if(miner_page.lb_swarm_workers.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_workers.obj, "%d", board->status.swarm.total_workers);
    }
    if(miner_page.lb_swarm_total_hashrate.obj != NULL){
      lv_label_set_text_fmt(miner_page.lb_swarm_total_hashrate.obj, "%sH/s", formatNumber(board->status.swarm.total_hr, 2).c_str());
    }
    if(miner_page.lb_utc_time.obj != NULL){
      String utc_time = "";
      utc_time = convert_time_to_local_12h(board->status.time.utc).substring(11, 11 + 5);
      lv_label_set_text_fmt(miner_page.lb_utc_time.obj, "%s", utc_time.c_str());
    }
  }
}

static void ui_ota_page_update(board_sal_t* board){
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  if(!board->status.ota.running)return;

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
}

static void ui_hits_page_update(board_sal_t* board){
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  static lv_obj_t *hits_page = nullptr, *hits_img_obj = nullptr;
  static lv_style_t style_overlay;
  if(board->status.miner.hits == board->status.miner.last_hits) {
    if(hits_img_obj != nullptr){
      lv_obj_del(hits_img_obj);
      hits_img_obj = nullptr;
    }
    if(hits_page != nullptr){
      lv_obj_del(hits_page);
      hits_page = nullptr;
    }
    return;
  }
  //create parent object
  if(hits_page == nullptr){
    // create hits page
    hits_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(hits_page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(hits_page, LV_ALIGN_CENTER, 0, 0);  // center align
    lv_obj_set_style_pad_all(hits_page, 0, 0);
    lv_obj_set_style_border_width(hits_page, 0, 0);
    lv_obj_set_scrollbar_mode(hits_page, LV_SCROLLBAR_MODE_OFF);
    
    // create and configure style
    lv_style_init(&style_overlay);
    lv_style_set_bg_color(&style_overlay, lv_color_black());
    // lv_style_set_bg_opa(&style_overlay, LV_OPA_70);  // 70% opacity black background
    lv_style_set_border_width(&style_overlay, 0);
    lv_style_set_border_opa(&style_overlay, LV_OPA_TRANSP);
    
    // connect style to hits_page
    lv_obj_add_style(hits_page, &style_overlay, LV_PART_MAIN);
    
    // create and configure image object
    hits_img_obj = lv_img_create(hits_page);
    lv_img_set_src(hits_img_obj, &block_hits_page_img_135_240);
    lv_obj_set_size(hits_img_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(hits_img_obj, LV_ALIGN_CENTER, 0, 0);
    
    // set image opacity
    lv_obj_set_style_img_opa(hits_img_obj, LV_OPA_COVER, LV_PART_MAIN);

    // show overlay
    lv_obj_clear_flag(hits_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(hits_page);  // bring to front
  }
  else{
    // uint8_t opa_lsit[] = {LV_OPA_100, LV_OPA_90, LV_OPA_80, LV_OPA_70, LV_OPA_60, LV_OPA_50, LV_OPA_40, LV_OPA_30, LV_OPA_20, LV_OPA_10, LV_OPA_TRANSP};
    // static uint8_t opa_index = 0;
    // //fade out effect
    // lv_obj_set_style_bg_opa(hits_page, opa_lsit[opa_index % sizeof(opa_lsit)/sizeof(opa_lsit[0])], LV_PART_MAIN);
    // // update image opacity
    // lv_obj_set_style_img_opa(hits_img_obj, opa_lsit[opa_index % sizeof(opa_lsit)/sizeof(opa_lsit[0])], LV_PART_MAIN);
    // LOG_D("Hits page opa index: %d, opacity: %d\r\n", opa_index, opa_lsit[opa_index]);
    // opa_index++;  
  }
}

static void ui_dashboard_page_update(board_sal_t* board){
  const float FREQ_MIN          = board->info.spec.ui.dashboard_page.performance.asic_freq_req.min;
  const float FREQ_MAX          = board->info.spec.ui.dashboard_page.performance.asic_freq_req.max;
  const float POWER_MIN         = board->info.spec.ui.dashboard_page.power.power.min;
  const float POWER_MAX         = board->info.spec.ui.dashboard_page.power.power.max;
  const float VCORE_REQ_MIN     = board->info.spec.ui.dashboard_page.performance.vcore_req.min;
  const float VCORE_REQ_MAX     = board->info.spec.ui.dashboard_page.performance.vcore_req.max;
  const float VCORE_MEASURE_MIN = board->info.spec.ui.dashboard_page.performance.vcore_measure.min;
  const float VCORE_MEASURE_MAX = board->info.spec.ui.dashboard_page.performance.vcore_measure.max;
  const float VCORE_TEMP_MIN    = board->info.spec.ui.dashboard_page.heat.vcore.min;
  const float VCORE_TEMP_MAX    = board->info.spec.ui.dashboard_page.heat.vcore.max;
  const float ASIC_TEMP_MIN     = board->info.spec.ui.dashboard_page.heat.asic.min;
  const float ASIC_TEMP_MAX     = board->info.spec.ui.dashboard_page.heat.asic.max;

  if(!board){
    LOG_E("board is null\r\n");
    return;
  }

  static lv_obj_t * arc_power = NULL, *arc_oc = NULL, *arc_vcore_req = NULL, *arc_vcore_measure = NULL, *arc_asci_temp = NULL;
  static lv_obj_t * lb_ds_hr = NULL, * lb_ds_hr_unit = NULL;
  static lv_obj_t * lb_pwr = NULL, * lb_overclock = NULL, * lb_vcore_req = NULL, * lb_vcore_measure = NULL, *lb_asic_temp = NULL;
  static lv_obj_t * lb_pwr_title = NULL, * lb_oc_title = NULL, * lb_vcore_req_title = NULL, * lb_vcore_measure_title = NULL, *lb_asic_temp_title = NULL;

  const lv_font_t *font = &lv_font_montserrat_14;
  lv_color_t font_color = lv_color_hex(0xFFFFFF);

  uint8_t arc_r = 30, arc_line_width = 8;
  uint16_t arc_angle_full = 230;
  if((ui_pages[UI_PAGE_DASHBOARD] != NULL) && (arc_power == NULL)) {
    // Hashrate label
    font = &ds_digib_font_36;
    font_color = lv_color_hex(0x000000);
    lb_ds_hr   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_ds_hr, 80);
    lv_label_set_text( lb_ds_hr, " ");
    lv_obj_set_style_text_font(lb_ds_hr, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ds_hr, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_ds_hr, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_ds_hr, LV_ALIGN_TOP_MID, 55, -4);
    //Hashrate uint
    font = &ds_digib_font_20;
    font_color = lv_color_hex(0x808080);
    lb_ds_hr_unit   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_ds_hr_unit, 50);
    lv_label_set_text( lb_ds_hr_unit, " ");
    lv_obj_set_style_text_font(lb_ds_hr_unit, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ds_hr_unit, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_ds_hr_unit, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_ds_hr_unit, LV_ALIGN_TOP_MID, 100, 8); 



    // Create overclock arc
    arc_oc = lv_arc_create(ui_pages[UI_PAGE_DASHBOARD]);
    lv_obj_set_size(arc_oc, 2*arc_r, 2*arc_r);
    lv_arc_set_bg_angles(arc_oc, 0, arc_angle_full);
    lv_arc_set_angles(arc_oc, 0, 0);
    lv_obj_remove_style(arc_oc, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(arc_oc, 90 + (360 - arc_angle_full) / 2);
    lv_obj_set_style_arc_width(arc_oc, arc_line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_oc, arc_line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_oc, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    // lv_obj_set_style_arc_color(arc_vbus, lv_color_hex(0xC0C0C0), LV_PART_INDICATOR);
    lv_obj_set_pos(arc_oc, 0, 4); 
    // Create power arc
    arc_power = lv_arc_create(ui_pages[UI_PAGE_DASHBOARD]);
    lv_obj_set_size(arc_power, 2*arc_r, 2*arc_r);
    lv_arc_set_bg_angles(arc_power, 0, arc_angle_full); 
    lv_arc_set_angles(arc_power, 0, 0); 
    lv_obj_remove_style(arc_power, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(arc_power, 90 + (360 - arc_angle_full) / 2);
    lv_obj_set_style_arc_width(arc_power, arc_line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_power, arc_line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_power, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    // lv_obj_set_style_arc_color(arc_power, lv_color_hex(0xC0C0C0), LV_PART_INDICATOR);
    lv_obj_set_pos(arc_power, 2*arc_r + 10, 4);
    // Create vcore_req arc
    arc_vcore_req = lv_arc_create(ui_pages[UI_PAGE_DASHBOARD]);
    lv_obj_set_size(arc_vcore_req, 2*arc_r, 2*arc_r);
    lv_arc_set_bg_angles(arc_vcore_req, 0, arc_angle_full);
    lv_arc_set_angles(arc_vcore_req, 0, 0);
    lv_obj_remove_style(arc_vcore_req, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(arc_vcore_req, 90 + (360 - arc_angle_full) / 2);
    lv_obj_set_style_arc_width(arc_vcore_req, arc_line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_vcore_req, arc_line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_vcore_req, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    // lv_obj_set_style_arc_color(arc_vcore_req, lv_color_hex(0xC0C0C0), LV_PART_INDICATOR);
    lv_obj_set_pos(arc_vcore_req, 0, 4 + 2*arc_r + 8);
    // Create arc_vcore_measure arc
    arc_vcore_measure = lv_arc_create(ui_pages[UI_PAGE_DASHBOARD]);
    lv_obj_set_size(arc_vcore_measure, 2*arc_r, 2*arc_r);
    lv_arc_set_bg_angles(arc_vcore_measure, 0, arc_angle_full);
    lv_arc_set_angles(arc_vcore_measure, 0, 0);
    lv_obj_remove_style(arc_vcore_measure, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(arc_vcore_measure, 90 + (360 - arc_angle_full) / 2);
    lv_obj_set_style_arc_width(arc_vcore_measure, arc_line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_vcore_measure, arc_line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_vcore_measure, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    // lv_obj_set_style_arc_color(arc_vcore_measure, lv_color_hex(0xC0C0C0), LV_PART_INDICATOR);
    lv_obj_set_pos(arc_vcore_measure, 2*arc_r + 10, 4 + 2*arc_r + 8);
    // Create arc_asci_temp arc
    arc_asci_temp = lv_arc_create(ui_pages[UI_PAGE_DASHBOARD]);
    lv_obj_set_size(arc_asci_temp, 3*arc_r, 3*arc_r);
    lv_arc_set_bg_angles(arc_asci_temp, 0, arc_angle_full);
    lv_arc_set_angles(arc_asci_temp, 0, 0);
    lv_obj_remove_style(arc_asci_temp, NULL, LV_PART_KNOB);
    lv_arc_set_rotation(arc_asci_temp, 90 + (360 - arc_angle_full) / 2);
    lv_obj_set_style_arc_width(arc_asci_temp, arc_line_width * 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_asci_temp, arc_line_width * 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_asci_temp, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    // lv_obj_set_style_arc_color(arc_asci_temp, lv_color_hex(0xC0C0C0), LV_PART_INDICATOR);
    lv_obj_set_pos(arc_asci_temp, 140, 35);



    // Create overclock label
    const lv_font_t *  font = &lv_font_montserrat_14;
    lv_color_t font_color = lv_color_hex(0xFFFFFF);
    lb_overclock   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_overclock, 80);
    lv_label_set_text( lb_overclock, " ");
    lv_obj_set_style_text_font(lb_overclock, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_overclock, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_overclock, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_overclock, LV_ALIGN_TOP_LEFT, arc_r - 19, arc_r - 2);
    // Create over clock title label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xD3D3D3);
    lb_oc_title   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_oc_title, 80);
    lv_label_set_text( lb_oc_title, "OC");
    lv_obj_set_style_text_font(lb_oc_title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_oc_title, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_oc_title, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_oc_title, LV_ALIGN_TOP_LEFT, arc_r - 12, arc_r + 20);


    // Create power label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xFFFFFF);
    lb_pwr   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_pwr, 80);
    lv_label_set_text( lb_pwr, " ");
    lv_obj_set_style_text_font(lb_pwr, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_pwr, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_pwr, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_pwr, LV_ALIGN_TOP_LEFT, 2 * arc_r + arc_r - 8, arc_r - 2);
    // Create power title label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xD3D3D3);
    lb_pwr_title   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_pwr_title, 80);
    lv_label_set_text( lb_pwr_title, "Power");
    lv_obj_set_style_text_font(lb_pwr_title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_pwr_title, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_pwr_title, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_pwr_title, LV_ALIGN_TOP_LEFT, 3 * arc_r - 12, arc_r + 20);



    // Create vcroe_req label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xFFFFFF);
    lb_vcore_req   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_vcore_req, 80);
    lv_label_set_text( lb_vcore_req, " ");
    lv_obj_set_style_text_font(lb_vcore_req, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_vcore_req, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_vcore_req, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_vcore_req, LV_ALIGN_TOP_LEFT, arc_r - 18 , 3 * arc_r + 3);
    // Create vcroe_req title label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xD3D3D3);
    lb_vcore_req_title   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_vcore_req_title, 80);
    lv_label_set_text( lb_vcore_req_title, "Vc req");
    lv_obj_set_style_text_font(lb_vcore_req_title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_vcore_req_title, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_vcore_req_title, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_vcore_req_title, LV_ALIGN_TOP_LEFT, arc_r - 23 , 3 * arc_r + 27);


    // Create vcroe_measure label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xFFFFFF);
    lb_vcore_measure   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_vcore_measure, 80);
    lv_label_set_text( lb_vcore_measure, " ");
    lv_obj_set_style_text_font(lb_vcore_measure, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_vcore_measure, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_vcore_measure, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_vcore_measure, LV_ALIGN_TOP_LEFT, 2 * arc_r + arc_r - 8, 3 * arc_r + 3);
    // Create vcroe_measure title label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xD3D3D3);
    lb_vcore_measure_title   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_vcore_measure_title, 80);
    lv_label_set_text( lb_vcore_measure_title, "Vc real");
    lv_obj_set_style_text_font(lb_vcore_measure_title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_vcore_measure_title, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_vcore_measure_title, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_vcore_measure_title, LV_ALIGN_TOP_LEFT, 3 * arc_r - 15 , 3 * arc_r + 27);


    // Create lb_asic_temp label
    font = &lv_font_montserrat_20;
    font_color = lv_color_hex(0xFFFFFF);
    lb_asic_temp   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_asic_temp, 80);
    lv_label_set_text( lb_asic_temp, " ");
    lv_obj_set_style_text_font(lb_asic_temp, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_asic_temp, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_asic_temp, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_asic_temp, LV_ALIGN_TOP_LEFT, 165, 65);
    // Create lb_asic_temp_title label
    font = &lv_font_montserrat_14;
    font_color = lv_color_hex(0xD3D3D3);
    lb_asic_temp_title   = lv_label_create( ui_pages[UI_PAGE_DASHBOARD] );
    lv_obj_set_width(lb_asic_temp_title, 80);
    lv_label_set_text( lb_asic_temp_title, "ASIC Temp");
    lv_obj_set_style_text_font(lb_asic_temp_title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_asic_temp_title, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_asic_temp_title, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_asic_temp_title, LV_ALIGN_TOP_LEFT, 146, 105);

  }

  String hr_value = formatNumber(board->status.miner.hashrate._3m, 3);
  String hr_unit  = (board->status.miner.hashrate._3m > 0) ? (String(hr_value.charAt(hr_value.length() - 1)) + "H/s") : "";
  String power    = formatNumber(board->status.power.vbus*board->status.power.ibus/1000.0/1000.0, 3);

  uint16_t oc_angle             = arc_angle_full * (board->info.spec.asic.req_frq - FREQ_MIN) / (FREQ_MAX - FREQ_MIN); 
  uint16_t pwr_angle            = arc_angle_full * (board->status.power.vbus * board->status.power.ibus/1000.0/1000.0 - POWER_MIN) / (POWER_MAX - POWER_MIN);
  uint16_t vcore_req_angle      = arc_angle_full * (board->info.spec.asic.req_vcore/1000.0 - VCORE_REQ_MIN) / (VCORE_REQ_MAX - VCORE_REQ_MIN); 
  uint16_t vcore_measure_angle  = arc_angle_full * (board->status.power.vcore /1000.0 - VCORE_MEASURE_MIN) / (VCORE_MEASURE_MAX - VCORE_MEASURE_MIN);
  uint16_t asic_temp_angle      = arc_angle_full * (board->status.temp.asic - ASIC_TEMP_MIN) / (ASIC_TEMP_MAX - ASIC_TEMP_MIN);

  lv_arc_set_angles(arc_oc,  0, oc_angle);
  lv_arc_set_angles(arc_power, 0, pwr_angle);
  lv_arc_set_angles(arc_vcore_req,  0, vcore_req_angle);
  lv_arc_set_angles(arc_vcore_measure, 0, vcore_measure_angle);
  lv_arc_set_angles(arc_asci_temp, 0, asic_temp_angle);

  //hashrate
  lv_label_set_text_fmt(lb_ds_hr, "%s", hr_value.substring(0, hr_value.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(lb_ds_hr_unit, "%s", hr_unit.c_str());
  //power 
  lv_label_set_text_fmt(lb_pwr, "%sw", power.c_str());
  //vbus
  lv_label_set_text_fmt(lb_overclock, "%sM", String(board->info.spec.asic.req_frq).c_str());
  //vcore_req
  lv_label_set_text_fmt(lb_vcore_req, "%sv", formatNumber(board->info.spec.asic.req_vcore/1000.0, 4).c_str());
  //vcore_measure
  lv_label_set_text_fmt(lb_vcore_measure, "%sv", formatNumber(board->status.power.vcore/1000.0, 4).c_str());
  //asic temp
  lv_label_set_text_fmt(lb_asic_temp, "%s'C",   formatNumber(board->status.temp.asic, 2).c_str());
}

static void ui_hr_healthy_page_update(board_sal_t* board){
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }
  uint16_t SCALE = (board->info.spec.ui.hashrate_dist_page.max_x_hr / board->info.spec.ui.hashrate_dist_page.max_x_bars);

  static lv_obj_t *chart = NULL, *label_scale = NULL, *lb_hr_health_duration = NULL, *lb_hr_health_title = NULL;
  static lv_obj_t * lb_ds_hr = NULL, * lb_ds_hr_unit = NULL;
  static lv_chart_series_t *series;
  static bool first_time = true;

  static double last_hashrate = 0;
  if(last_hashrate == board->status.miner.hashrate._3m) return;
  last_hashrate = board->status.miner.hashrate._3m;

  if(first_time){
    first_time = false;
    // Hashrate label
    const lv_font_t *  font = &ds_digib_font_36;
    lv_color_t font_color = lv_color_hex(0x000000);
    lb_ds_hr   = lv_label_create( ui_pages[UI_PAGE_HR_HEALTH] );
    lv_obj_set_width(lb_ds_hr, 80);
    lv_label_set_text( lb_ds_hr, " ");
    lv_obj_set_style_text_font(lb_ds_hr, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ds_hr, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_ds_hr, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_ds_hr, LV_ALIGN_TOP_MID, 55, -4);
    //Hashrate uint
    font = &ds_digib_font_20;
    font_color = lv_color_hex(0x808080);
    lb_ds_hr_unit   = lv_label_create( ui_pages[UI_PAGE_HR_HEALTH] );
    lv_obj_set_width(lb_ds_hr_unit, 50);
    lv_label_set_text( lb_ds_hr_unit, " ");
    lv_obj_set_style_text_font(lb_ds_hr_unit, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_ds_hr_unit, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_ds_hr_unit, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_ds_hr_unit, LV_ALIGN_TOP_MID, 100, 8); 
    //scale
    font_color = lv_color_hex(0xFFA500);
    font = &lv_font_montserrat_12;
    label_scale = lv_label_create(ui_pages[UI_PAGE_HR_HEALTH]);
    lv_label_set_text(label_scale, ("Scale     : " + String(SCALE) + " GH/s").c_str());
    lv_obj_set_style_text_font(label_scale, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_scale, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(label_scale, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label_scale, LV_ALIGN_TOP_LEFT, 1, 3); 
    //time cost
    font_color = lv_color_hex(0xFFA500);
    font = &lv_font_montserrat_12;
    lb_hr_health_duration   = lv_label_create( ui_pages[UI_PAGE_HR_HEALTH] );
    lv_obj_set_width(lb_hr_health_duration, 120);
    lv_label_set_text( lb_hr_health_duration, " ");
    lv_obj_set_style_text_font(lb_hr_health_duration, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_hr_health_duration, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_hr_health_duration, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align( lb_hr_health_duration, LV_ALIGN_TOP_LEFT, 1, 18);

    // Create a chart
    chart = lv_chart_create(ui_pages[UI_PAGE_HR_HEALTH]);
    lv_obj_set_size(chart, SCREEN_WIDTH - 14, SCREEN_HEIGHT - 48); 
    lv_obj_align(chart, LV_ALIGN_CENTER, 14, 8);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, board->info.spec.ui.hashrate_dist_page.max_x_bars - 1); 
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100); 
    lv_chart_set_div_line_count(chart, 5, 4);

    // Add a series to the chart
    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(chart, board->info.spec.ui.hashrate_dist_page.max_x_bars);
    lv_chart_set_all_value(chart, series, 0);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 1, 1, board->info.spec.ui.hashrate_dist_page.max_x_bars, 1, true, 25);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 1, 2, 5, 1, true, 25);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);

    // set grid style
    static lv_style_t style_grid;
    lv_style_init(&style_grid);
    lv_style_set_line_dash_width(&style_grid, 2); 
    lv_style_set_line_dash_gap(&style_grid, 4); 
    lv_style_set_line_opa(&style_grid, LV_OPA_50); 
    lv_obj_add_style(chart, &style_grid, LV_PART_MAIN | LV_STATE_DEFAULT);
    // set font for ticks
    lv_obj_set_style_text_font(chart, &lv_font_montserrat_10, LV_PART_TICKS); 
  }

  static uint64_t *counts = NULL;
  if (counts == NULL) {
    counts = (uint64_t *)malloc(board->info.spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t));
    memset(counts, 0, board->info.spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t));
  }
  int index = last_hashrate/1000/1000/1000 / SCALE; // Convert to GH/s and scale
  index = (index >= board->info.spec.ui.hashrate_dist_page.max_x_bars) ? board->info.spec.ui.hashrate_dist_page.max_x_bars - 1 : index;
  counts[index]++;
  board->info.spec.ui.hashrate_dist_page.times++;
  for (int i = 0; i < board->info.spec.ui.hashrate_dist_page.max_x_bars; i++) {
    uint8_t y = (uint8_t)(100*(float)counts[i] / (float)board->info.spec.ui.hashrate_dist_page.times);
    lv_chart_set_value_by_id(chart, series, i, y);
    board->info.spec.ui.hashrate_dist_page.dist_map[i] = y;// Update the global distribution map
  }
  // time cost of this feature
  static uint64_t start = millis();
  board->info.spec.ui.hashrate_dist_page.dura = (millis() - start) / 1000;

  String hr = formatNumber(last_hashrate, 3);
  String hr_unit = (last_hashrate > 0) ? (String(hr.charAt(hr.length() - 1)) + "H/s") : "";
  //hashrate
  lv_label_set_text_fmt(lb_ds_hr, "%s", hr.substring(0, hr.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(lb_ds_hr_unit, "%s", hr_unit.c_str());
  //time cost
  lv_label_set_text_fmt(lb_hr_health_duration,"Sample: %s", String(String(board->info.spec.ui.hashrate_dist_page.dura) + "s").c_str());
}

static void ui_big_digit_page_update(board_sal_t* board){
  if(!board){
    LOG_E("board is null\r\n");
    return;
  }



  static bool first_time = true;
  static lv_obj_t * lb_hashrate = NULL, * lb_hashrate_unit = NULL,* lb_price = NULL;
  static lv_obj_t * lb_time = NULL, * lb_date = NULL, * lb_block_hit = NULL, *lb_block_hit_unit = NULL;

  if(first_time){
    first_time = false;

    // Hashrate value
    const lv_font_t *  font = &ds_digib_font_56;
    lv_color_t font_color = lv_color_hex(0xFFFFFF);
    lb_hashrate   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_hashrate, SCREEN_WIDTH/2);
    lv_label_set_text( lb_hashrate, " ");
    lv_obj_set_style_text_font(lb_hashrate, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_hashrate, font_color, LV_PART_MAIN); 
    lv_label_set_long_mode(lb_hashrate, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_hashrate, LV_ALIGN_TOP_LEFT, 0, 0);
    // Hashrate unit
    font = &ds_digib_font_20;
    font_color = lv_color_hex(0x808080);
    lb_hashrate_unit   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_hashrate_unit, 50);
    lv_label_set_text( lb_hashrate_unit, " ");
    lv_obj_set_style_text_font(lb_hashrate_unit, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_hashrate_unit, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_hashrate_unit, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_hashrate_unit, LV_ALIGN_TOP_MID, 0, 26);
    // Time
    font = &ds_digib_font_42;
    font_color = lv_color_hex(0xFFFFFF);
    lb_time   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    String time_text = "00:00:00 AM";
    lv_coord_t width = lv_txt_get_width(time_text.c_str(), strlen(time_text.c_str()), font, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(lb_time, width);
    lv_label_set_text( lb_time, " ");
    lv_obj_set_style_text_font(lb_time, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_time, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_time, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align( lb_time, LV_ALIGN_CENTER, 0, 15);
    // Date
    font = &ds_digib_font_24;
    font_color = lv_color_hex(0x808080);
    lb_date   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_date, SCREEN_WIDTH/2);
    lv_label_set_text( lb_date, " ");
    lv_obj_set_style_text_font(lb_date, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_date, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_date, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_date, LV_ALIGN_BOTTOM_MID, 62, 0);
    // Price
    font = &ds_digib_font_24;
    font_color = lv_color_hex(0x808080);
    lb_price   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_date, SCREEN_WIDTH/2);
    lv_label_set_text( lb_price, " ");
    lv_obj_set_style_text_font(lb_price, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_price, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_price, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_price, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    // Block hit
    font = &ds_digib_font_56;
    font_color = lv_color_hex(0xEE7D30);
    lb_block_hit   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_block_hit, 75);
    lv_label_set_text( lb_block_hit, " ");
    lv_obj_set_style_text_font(lb_block_hit, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_block_hit, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_block_hit, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_block_hit, LV_ALIGN_TOP_RIGHT, -30, 0);
    // Block hit unit
    font = &ds_digib_font_20;
    font_color = lv_color_hex(0x808080);
    lb_block_hit_unit   = lv_label_create( ui_pages[UI_PAGE_BIG_DIGIT] );
    lv_obj_set_width(lb_block_hit_unit, 50);
    lv_label_set_text( lb_block_hit_unit, "hits");
    lv_obj_set_style_text_font(lb_block_hit_unit, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lb_block_hit_unit, font_color, LV_PART_MAIN);
    lv_label_set_long_mode(lb_block_hit_unit, LV_LABEL_LONG_DOT);
    lv_obj_align( lb_block_hit_unit, LV_ALIGN_TOP_RIGHT, 0, 26);
  }


  
  String hr       = formatNumber(board->status.miner.hashrate._3m, 3);
  String datetime = convert_time_to_local(g_board.status.time.utc);
  String hr_unit = (board->status.miner.hashrate._3m > 0) ? (String(hr.charAt(hr.length() - 1)) + "H/s") : "";
  //hashrate
  lv_label_set_text_fmt(lb_hashrate, "%s", hr.substring(0, hr.length() - 1).c_str());
  //hashrate unit
  lv_label_set_text_fmt(lb_hashrate_unit, "%s", hr_unit.c_str());
  //time
  String char11 = datetime.substring(11, 12);//remove '0' 
  uint8_t index = (char11 == "0") ? 12:11;
  lv_label_set_text_fmt(lb_time, "%s", datetime.substring(index, datetime.length()).c_str());
  //date
  lv_label_set_text_fmt(lb_date, "%s", datetime.substring(0, 10).c_str());
  //block hit
  if(board->status.miner.hits < 10) lv_obj_align( lb_block_hit, LV_ALIGN_TOP_RIGHT, -10, 0);
  else lv_obj_align( lb_block_hit, LV_ALIGN_TOP_RIGHT, -30, 0);
  lv_label_set_text_fmt(lb_block_hit, "%s", String(board->status.miner.hits).c_str());

  
  //price value 
  String price_value = (board->market->price > 1.0) ? String(board->market->price, 1) : String(board->market->price, 6);
  lv_color_t font_color;
  static float last_price = board->market->price;
  if(last_price != board->market->price){
    font_color = lv_color_hex(0x00ff00);//green
    last_price = board->market->price;
  }
  else font_color = lv_color_hex(0x808080);//white
  lv_obj_set_style_text_color(lb_price, font_color, LV_PART_MAIN);
  lv_label_set_text_fmt(lb_price, "$%s", price_value.c_str());
}

void ui_switch_next_page_cb(){
  g_board.status.preference.led.sleep         = (g_board.status.preference.led.sleep_last) ? false : g_board.status.preference.led.sleep; //switch led sleep mode
  // g_board.status.preference.screen.brightness = g_board.status.preference.screen.brightness_last;//restore brightness
  if(g_board.status.miner.last_hits!= g_board.status.miner.hits) {
    xSemaphoreGive(g_board.status.brightness_update_xsem); //wake up brightness thread to set brightness
    g_board.status.miner.last_hits = g_board.status.miner.hits;    //save last hits if button pressed
    return;
  } 

  g_board.status.ui.current_page = (g_board.status.ui.current_page == UI_PAGE_BIG_DIGIT) ? UI_PAGE_CONFIG : g_board.status.ui.current_page;
  g_board.status.ui.current_page++;
  lv_obj_scroll_to_view(ui_pages[g_board.status.ui.current_page], LV_ANIM_ON);
  g_board.status.ui.last_page = g_board.status.ui.current_page;
  xSemaphoreGive(g_board.status.ui.page_save_xsem);
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

void ui_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);

  const char* vbus_chk_str[] = {"Vbus check   ","Vbus check.  ","Vbus check.. ","Vbus check..."};
  const char* asci_init_str[] = {"ASIC init  ","ASIC init.  ","ASIC init.. ","ASIC init..."};
  const char* wifi_con_str[] = {"Wifi connect   ","Wifi connect.  ","Wifi connect.. ","Wifi connect..."};
  const char* fan_test_str[] = {"Fan test   ","Fan test.  ","Fan test.. ","Fan test..."};
  const char* market_con_str[] = {"Market connect   ","Market connect.  ","Market connect.. ","Market connect..."};
  const char* ver_chk_str[]  = {"Version check ","Version check.","Version check..","Version check..."};
  const char* pool_con_str[] = {"Pool connect   ","Pool connect.  ","Pool connect.. ","Pool connect..."};
  const char* pool_auth_str[] = {"Pool auth   ","Pool auth.  ","Pool auth.. ","Pool auth..."};
  const char* wait_job_str[] = {"Waiting pool job   ","Waiting pool job.  ","Waiting pool job.. ","Waiting pool job..."};
  const char* config_str[] = {"Config   ","Config.  ","Config.. ","Config..."};

  tft_init();
  lv_init();
  ui_drv_register();

  //lvgl tick task
  String taskName = "(lvgl)";
  xTaskCreatePinnedToCore(lvgl_tick_task, taskName.c_str(), 1024*5, (void*)taskName.c_str(), TASK_PRIORITY_LVGL_DRV, NULL, 1);
  delay(100);

  ui_page_element_init(&g_board);

  //ui layout init
  ui_layout_init(&g_board);

  //set the first page to loading page
  lv_obj_scroll_to_view(ui_pages[UI_PAGE_LOADING], LV_ANIM_ON); 

  //backlight brightness ramp up
  for(int i = 0; i < g_board.status.preference.screen.brightness; i++) {
    tft_bl_ctrl(i);
    delay(10);
  }

  uint16_t cnt = 0;

  //Vbus check 
  ui_loading_str_update(vbus_chk_str[0], 0xFFFFFF, true);
  while (!g_board.power->is_adc_ready()){
    ui_loading_str_update(vbus_chk_str[(cnt++)%4], 0xFFFFFF, false);
    delay(500);
  }
  
  //Vbus type check, DC or USB 
  if(g_board.power->is_dc_pluged()) ui_loading_str_update("DC pluged.", 0x00ff00, true);
  else ui_loading_str_update("USB pluged.", 0x00ff00, true);
  delay(500);

  //Vbus type check and voltage check
  while(g_board.power->get_vbus() < g_board.info.spec.pwr.vbus_min_required){
      static bool blink = false;
      uint32_t color = (blink) ? 0xFF0000 : 0xFFFFFF;
      String vbusString = "Vbus " + String(g_board.power->get_vbus()/1000.0, 1) + "v(at least" + String(g_board.info.spec.pwr.vbus_min_required / 1000.0, 1) + "v)";
      ui_loading_str_update(vbusString, color, false);
      blink = !blink;
      if(!g_board.power->is_dc_pluged()){
        disable_usb_uart();//disable usb uart to fit for typeA port PD , such as Apple divider 3/BC1.2 SDP/CDP/DCP protocol
        delay(500);
      }
      delay(500);
  }
  ui_loading_str_update("Vbus " + String(g_board.power->get_vbus() / 1000.0, 3) + "V.", 0x00FF00, true);
  delay(500);

  /***************************************wait fan self test *******************************************/
  cnt = 0;
  ui_loading_str_update(fan_test_str[0], 0xFFFFFF, true);
  while(!g_board.status.fan.list[0].self_test){
    ui_loading_str_update(String(fan_test_str[cnt++ % 4]) + String(g_board.status.fan.list[0].rpm) + "/ " + String(g_board.info.spec.fans[0].init.self_test_rpm_thr) + "rpm", 0xFFFFFF, false);
    delay(300);
  }
  ui_loading_str_update("Pass! [" + String(g_board.status.fan.list[0].rpm) + "/ " + String(g_board.info.spec.fans[0].init.self_test_rpm_thr) + " rpm]", 0x00FF00, true);
  delay(2000);

  /***************************************wait Vcore self test *****************************************/
  ui_loading_str_update("Vcore check...", 0xFFFFFF, true);
  delay(500);
  //Vcore voltage check
  while(!g_board.power->is_vcore_ready()){
    static bool blink = false;
    uint32_t color = (blink) ? 0xFF0000 : 0xFFFFFF;
    ui_loading_str_update("Vcore error.", color, false);
    blink = !blink;
    delay(500);
  }
  delay(200);//wait for vcore set to target voltage
  ui_loading_str_update(String("Vcore ") + String(g_board.power->get_vcore() / 1000.0, 3) + "v.", 0x00FF00, true);
  delay(500);
  /****************************************wait for asic init********************************************/
  while(g_board.miner == nullptr) delay(10); //wait miner object created
  cnt = 0;
  while(g_board.miner->get_asic_count() == 0){
    ui_loading_str_update(String(asci_init_str[cnt++ % 4]), 0xFFFFFF, false);
    delay(300);
  }
  ui_loading_str_update(String("Found " + String(g_board.miner->get_asic_count())) + (g_board.miner->get_asic_count() > 1 ? " chips" : " chip"), 0x00FF00, true);
  delay(1000);
  /***************************************wait for wifi connected****************************************/
  cnt = 0;
  while(g_board.info.connection.wifi.status_param.status != WL_CONNECTED){
    ui_loading_str_update(wifi_con_str[(cnt++)%4]  + String("[") + g_board.info.connection.wifi.conn_param.ssid +  String("]"), 0xFFFFFF, false);
    if(xSemaphoreTake(g_board.info.connection.wifi.force_cfg_xsem, 100) == pdTRUE){
      ui_loading_str_update(String("Timeout!"), 0xFF0000, false);
      delay(500);
      //config background
      lv_obj_scroll_to_view(ui_pages[UI_PAGE_CONFIG], LV_ANIM_ON);

      //config label
      const lv_font_t *font = &lv_font_montserrat_14;
      lv_color_t font_color = lv_color_hex(0xFFFFFF);
      lv_obj_t *lb_cfg = lv_label_create(ui_pages[UI_PAGE_CONFIG]);
      lv_obj_t *lb_version = lv_label_create(ui_pages[UI_PAGE_CONFIG]);
      String str = g_board.info.connection.wifi.softap_param.ssid + "\r\n"+ g_board.info.connection.wifi.softap_param.ip.toString();

      lv_obj_set_width(lb_cfg, 120);
      lv_label_set_text(lb_cfg, str.c_str());
      lv_obj_set_style_text_font(lb_cfg, font, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_cfg, font_color, LV_PART_MAIN);
      lv_label_set_long_mode(lb_cfg, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_line_space(lb_cfg, 0, LV_PART_MAIN); 
      lv_obj_align(lb_cfg, LV_ALIGN_LEFT_MID, 8, 38);

      lv_obj_set_width(lb_version, 120);
      lv_label_set_text(lb_version, g_board.info.base.fw_version.c_str());
      lv_obj_set_style_text_font(lb_version, font, LV_PART_MAIN);
      lv_obj_set_style_text_color(lb_version, font_color, LV_PART_MAIN);
      lv_label_set_long_mode(lb_version, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_line_space(lb_version, 0, LV_PART_MAIN); 
      lv_obj_align(lb_version, LV_ALIGN_TOP_MID, 105, 1);

      //QR code
      lv_obj_t *qrcode = lv_qrcode_create(ui_pages[UI_PAGE_CONFIG], SCREEN_HEIGHT - 30, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
      String qr_str = "WIFI:T:WPA;S:" + g_board.info.connection.wifi.softap_param.ssid + ";P:" + g_board.info.connection.wifi.softap_param.pwd + ";H:false;";
      lv_qrcode_update(qrcode, (uint8_t*)qr_str.c_str(), qr_str.length());
      lv_obj_align(qrcode, LV_ALIGN_RIGHT_MID, 0, 0);

      while (true){
        static uint8_t cnt = 0;
        String str = (g_board.info.connection.client_connected) ? config_str[cnt++%4] : (String(g_board.info.connection.wifi.status_param.config_timeout) + "s");
        //config timeout label location
        if(g_board.info.connection.client_connected) lv_obj_align( config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, 160, 0);
        else lv_obj_align( config_page.lb_cfg_timeout.obj, LV_ALIGN_BOTTOM_MID, 175, 0);

        lv_label_set_text(config_page.lb_cfg_timeout.obj, str.c_str());
        //update ota page
        if(g_board.status.ota.running){
          ui_ota_page_update(&g_board);
        }
        delay(1000);//wait for configuration and miner will restart after configuration
      }
    }
  }
  ui_loading_str_update("Wifi connected!", 0x00FF00, true);
  delay(500);
#if HAS_VERSION_CHECK_FEATURE
  /***************************************wait for version check**************************************/
  cnt = 0;
  ui_loading_str_update(ver_chk_str[0], 0xFFFFFF, true);
  while(g_board.info.base.fw_latest_release == ""){
    ui_loading_str_update(ver_chk_str[cnt++ % 4], 0xFFFFFF, false);
    delay(500);
  }

  int res = compareVersions(g_board.info.base.fw_version, g_board.info.base.fw_latest_release);
  if(res == -1){
    String str = "Update to: " + g_board.info.base.fw_latest_release;
    ui_loading_str_update(str, 0xFFFFFF, true);
    while (cnt++ <= 15){
      delay(250);
      ui_loading_str_update(str, 0xEE7D30, false);
      delay(250);
      ui_loading_str_update(str, 0xFFFFFF, false);
    }
  }
  else if(res == 0 || res == 1){
    ui_loading_str_update("Up to date!", 0x00FF00, true);
    delay(2000);
  }
  else{
    ui_loading_str_update("Version check failed!", 0xFF0000, true);
    delay(2000);
  }
#endif //HAS_VERSION_CHECK_FEATURE
  /***************************************wait for market connected************************************/
  cnt = 0;
  ui_loading_str_update(market_con_str[0], 0xFFFFFF, true);
  uint32_t start = millis();
  while(0 == g_board.market->lastUpdate){
    ui_loading_str_update(String(market_con_str[cnt++ % 4] + g_board.info.base.coin_price).c_str(), 0xFFFFFF, false);
    if(millis() - start - g_board.market->lastUpdate >= MARKET_TIMEOUT){
      ui_loading_str_update("Market update timeout!", 0xFF0000, false);
      delay(500);
      break;
    }
    delay(300);
  }
  delay(500);
  if(0 != g_board.market->lastUpdate) ui_loading_str_update("Market connected!", 0x00FF00, true);
  delay(1000);
  /***************************************wait for pool connected**************************************/
  cnt = 0;
  while(!g_board.stratum->is_subscribed()){
    if(g_board.stratum->pool->get_last_errormsg().length() > 0){
      uint32_t color = (cnt % 2 == 0) ? 0xFFFFFF : 0xFF0000;
      ui_loading_str_update(g_board.stratum->pool->get_last_errormsg().c_str(), color, false);
    }else{
      String con_type = g_board.info.connection.pool_use.ssl ? "[ssl]" : "[tcp]";
      ui_loading_str_update(String(pool_con_str[(cnt)%4] + con_type), 0xFFFFFF, false);
    }
    cnt++;
    delay(300);
  }
  ui_loading_str_update("Pool connected!", 0x00FF00, true);
  delay(100);
  /******************************************wait pool authorized*************************************/
  cnt = 0;
  while(!g_board.stratum->is_authorized()){
    ui_loading_str_update(pool_auth_str[(cnt++)%4], 0xFFFFFF, false);
    delay(300);
    while (cnt >= 20){
      ui_loading_str_update("Wrong stratum user!", 0xFF0000, false);
      delay(500);
      ui_loading_str_update("Wrong stratum user!", 0xFFFFFF, false);
      delay(500);
      if(g_board.stratum->is_authorized()) break;
    }
  }
  ui_loading_str_update("Pool authorized!", 0x00FF00, true);
  delay(100);
  /******************************************wait first job*******************************************/
  cnt = 0;
  while(g_board.stratum->get_job_counter() == 0){
    ui_loading_str_update(wait_job_str[(cnt++)%4], 0xFFFFFF, false);
    while (cnt >= 3*20){
      ui_loading_str_update("Pool job timeout!", 0xFF0000, false);
      delay(500);
      ui_loading_str_update("Pool job timeout!", 0xFFFFFF, false);
      delay(500);
      if(g_board.stratum->get_job_counter() > 0) break;
    }
  }
  ui_loading_str_update("Miner ready!", 0x00FF00, true);
  delay(500);
  /***************************************scroll to miner page***************************************/
  lv_obj_scroll_to_view(ui_pages[g_board.status.ui.last_page], LV_ANIM_ON); 
  g_board.status.ui.current_page = g_board.status.ui.last_page;
  
  while (true){
    xSemaphoreTake(g_board.status.miner.update_xsem, portMAX_DELAY);
    if(xSemaphoreTake(lvgl_xMutex, 0) == pdTRUE){
      // auto screen scrolling
      static uint32_t last = 0;
      if((millis() - last >= 1000*10) && g_board.status.preference.screen.auto_rolling){
        ui_switch_next_page_cb();
        last = millis();
      }

      //update miner page
      ui_miner_page_update(&g_board);
      //update dashboard page
      ui_dashboard_page_update(&g_board);
      //update hashrate healthy page
      ui_hr_healthy_page_update(&g_board);
      //update big digit page
      ui_big_digit_page_update(&g_board);
      //update ota page
      ui_ota_page_update(&g_board);
      //update hits page
      ui_hits_page_update(&g_board);

      //release mutex
      xSemaphoreGive(lvgl_xMutex); 
    }
  }
}
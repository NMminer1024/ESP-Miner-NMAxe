#include "lvgl.h"
#include "image.h"     // defines all lv_img_dsc_t descriptors + bitmap arrays
#include "images.h"

void images_init(uint16_t w, uint16_t h) {
    // Fixed-size logos (RGB565 + alpha byte)
    logo_worker_nmaxe.header.w = 60;  logo_worker_nmaxe.header.h = 68;
    logo_worker_nmaxe.data_size = 60 * 68 * LV_IMG_PX_SIZE_ALPHA_BYTE;
    logo_worker_nmaxegamma.header.w = 60;  logo_worker_nmaxegamma.header.h = 68;
    logo_worker_nmaxegamma.data_size = 60 * 68 * LV_IMG_PX_SIZE_ALPHA_BYTE;
    logo_worker_nmqaxepp.header.w = 70;  logo_worker_nmqaxepp.header.h = 79;
    logo_worker_nmqaxepp.data_size = 70 * 79 * LV_IMG_PX_SIZE_ALPHA_BYTE;
    logo_miner_nmqaxepp_70_70.header.w = 70;  logo_miner_nmqaxepp_70_70.header.h = 70;
    logo_miner_nmqaxepp_70_70.data_size = 70 * 70 * LV_IMG_PX_SIZE_ALPHA_BYTE;

    const uint32_t bytes = (uint32_t)w * h * LV_COLOR_SIZE / 8;
    auto set = [&](lv_img_dsc_t& d) { d.header.w = w; d.header.h = h; d.data_size = bytes; };

    if (h <= 160) {   // 135x240 set (NMAxe / Gamma)
        set(loading_page_img_135_240);  set(config_page_img_135_240);
        set(mining_page_img_135_240);   set(black_page_img_135_240);
        set(block_hits_page_img_135_240); set(new_achievement_page_img_135_240);
        set(status_page_img_135_240);
    } else {          // 240x320 set (NMQAxe++)
        set(loading_page_img_240_320);  set(config_page_img_240_320);
        set(mining_page_img_240_320);   set(black_page_img_240_320);
        set(block_hits_page_img_240_320); set(new_achievement_page_img_240_320);
        set(status_page_img_240_320);
    }
}

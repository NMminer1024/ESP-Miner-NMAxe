#ifndef IMAGE_H_
#define IMAGE_H_

#include "loading_img.h"
#include "mining_img.h"
#include "config_img.h"
#include "status_img.h"
#include "black_img.h"
#include "blockhit_img.h"
#include "logo_img.h"

/****************************** worker logo****************************/

lv_img_dsc_t logo_worker_nmaxe = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,  //transparent color, alpha channel included
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)nmaxe_worker_logo_img_array,
};

lv_img_dsc_t logo_worker_nmaxegamma = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,  //transparent color, alpha channel included
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)nmaxegamma_worker_logo_img_array,
};

lv_img_dsc_t logo_worker_nmqaxepp = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,  //transparent color, alpha channel included
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)nmqaxepp_worker_logo_img_array,
};

lv_img_dsc_t logo_miner_nmqaxepp_70_70 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,  //transparent color, alpha channel included
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)logo_miner_nmqaxepp_70_70_img_array,
};

/****************************** 135x240 page images****************************/
lv_img_dsc_t loading_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)loading_img_array_135_240, 
};

lv_img_dsc_t config_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)config_img_array_135_240,
};

lv_img_dsc_t mining_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)mining_page_img_array_135_240,
};

lv_img_dsc_t black_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)black_page_img_array_135_240,
};

lv_img_dsc_t status_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)status_page_img_array_135_240,
};

lv_img_dsc_t block_hits_page_img_135_240 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)block_hit_page_img_array_135_240,
};






/****************************** 240x320 page images****************************/
lv_img_dsc_t loading_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)loading_img_array_240_320, 
};



lv_img_dsc_t config_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)config_img_array_240_320,
};


lv_img_dsc_t mining_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)mining_page_img_array_240_320, 
};



lv_img_dsc_t black_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)black_page_img_array_240_320,
};

lv_img_dsc_t status_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)status_page_img_array_240_320,
};

lv_img_dsc_t block_hits_page_img_240_320 = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)block_hit_page_img_array_240_320,
};










#endif
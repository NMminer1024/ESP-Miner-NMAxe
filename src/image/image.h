#ifndef IMAGE_H_
#define IMAGE_H_

#include "loading_img.h"
#include "mining_img.h"
#include "config_img.h"
#include "status_img.h"
#include "black_img.h"
#include "blockhit_img.h"




lv_img_dsc_t black_page_img = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)black_page_img_array,
};

lv_img_dsc_t block_hits_page_img = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)block_hit_page_img_array,
};



lv_img_dsc_t config_page_img_nmaxe = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)config_img_array_nmaxe,
};

lv_img_dsc_t config_page_img_nmaxe_gamma = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)config_img_array_nmaxe,
};

lv_img_dsc_t loading_page_img = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)loading_img_array, 
};


lv_img_dsc_t mining_page_img_nmaxe = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)main_page_img_array_nmaxe,
};

lv_img_dsc_t mining_page_img_nmaxe_gamma = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)main_page_img_array_nmaxe_gamma,
};


lv_img_dsc_t status_page_img = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,  
        .always_zero = 0,
        .reserved = 0,
        .w = 0 ,  
        .h = 0, 
    },
    .data_size = 0 * 0 * LV_COLOR_SIZE / 8,
    .data = (const uint8_t *)status_page_img_array,
};
#endif
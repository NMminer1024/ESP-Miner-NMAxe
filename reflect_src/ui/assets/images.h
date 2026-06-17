#pragma once
#include "lvgl.h"

// ============================================================================
//  Background images & logos (full bitmaps live in the *_img.h arrays; the
//  lv_img_dsc_t descriptors are defined once in images.cpp). Ported verbatim
//  from the legacy src/image set so the UI keeps its exact look.
// ============================================================================

// Worker / miner logos
extern lv_img_dsc_t logo_worker_nmaxe;
extern lv_img_dsc_t logo_worker_nmaxegamma;
extern lv_img_dsc_t logo_worker_nmqaxepp;
extern lv_img_dsc_t logo_miner_nmqaxepp_70_70;

// 135x240 (NMAxe / Gamma) page backgrounds
extern lv_img_dsc_t loading_page_img_135_240;
extern lv_img_dsc_t config_page_img_135_240;
extern lv_img_dsc_t mining_page_img_135_240;
extern lv_img_dsc_t black_page_img_135_240;
extern lv_img_dsc_t block_hits_page_img_135_240;
extern lv_img_dsc_t new_achievement_page_img_135_240;
extern lv_img_dsc_t status_page_img_135_240;

// 240x320 (NMQAxe++) page backgrounds
extern lv_img_dsc_t loading_page_img_240_320;
extern lv_img_dsc_t config_page_img_240_320;
extern lv_img_dsc_t mining_page_img_240_320;
extern lv_img_dsc_t black_page_img_240_320;
extern lv_img_dsc_t block_hits_page_img_240_320;
extern lv_img_dsc_t new_achievement_page_img_240_320;
extern lv_img_dsc_t status_page_img_240_320;

// Set descriptor dimensions for the active resolution (w x h, post-rotation) +
// fixed logo dimensions. Must run once before any image is shown.
void images_init(uint16_t w, uint16_t h);

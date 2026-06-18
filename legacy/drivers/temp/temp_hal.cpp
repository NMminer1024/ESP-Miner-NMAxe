#include "temp_hal.h"

static temp_reader_fn_t s_vcore_fn  = nullptr;
static void            *s_vcore_ctx = nullptr;
static temp_reader_fn_t s_asic_fn   = nullptr;
static void            *s_asic_ctx  = nullptr;

void temp_hal_register_vcore(temp_reader_fn_t fn, void *ctx) {
    s_vcore_fn  = fn;
    s_vcore_ctx = ctx;
}

void temp_hal_register_asic(temp_reader_fn_t fn, void *ctx) {
    s_asic_fn  = fn;
    s_asic_ctx = ctx;
}

float temp_hal_get_vcore(void) {
    return s_vcore_fn ? s_vcore_fn(s_vcore_ctx) : NAN;
}

float temp_hal_get_asic(void) {
    return s_asic_fn ? s_asic_fn(s_asic_ctx) : NAN;
}

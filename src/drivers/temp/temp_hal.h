#pragma once
#include <cmath>

/**
 * Temperature HAL — board-agnostic temperature reader interface.
 *
 * During board init each board registers its own callbacks via
 * temp_hal_register_vcore / temp_hal_register_asic.
 * All upper-layer code then calls temp_hal_get_vcore / temp_hal_get_asic
 * without any knowledge of the underlying sensor.
 *
 * Callback signature:
 *   float reader(void *ctx)  — returns °C, or NAN on failure.
 */

typedef float (*temp_reader_fn_t)(void *ctx);

void  temp_hal_register_vcore(temp_reader_fn_t fn, void *ctx = nullptr);
void  temp_hal_register_asic (temp_reader_fn_t fn, void *ctx = nullptr);
float temp_hal_get_vcore(void);
float temp_hal_get_asic (void);

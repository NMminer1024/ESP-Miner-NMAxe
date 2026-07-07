// What: LVGL display adapter for the project display abstraction.
// Why: LVGL expects flush callbacks and draw-buffer management, while BSPs
// expose a board-owned `drivers::Display` interface.
// Role: Allocates the LVGL draw buffer, registers the LVGL display driver, and
// forwards flush rectangles into the active BSP display implementation.
// Benefit: The UI framework can stay LVGL-native without forcing BSPs to depend
// on LVGL APIs or own LVGL lifecycle details.
#include "ui/port/ports.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace nm::ui::port {

struct LvglDisplayPortState {
    drivers::Display* display = nullptr;
    lv_disp_draw_buf_t draw_buffer{};
    lv_disp_drv_t driver{};
    lv_color_t* buffer = nullptr;
    lv_disp_t* handle = nullptr;
    uint32_t buffer_pixels = 0;
    bool lvgl_ready = false;
    bool display_ready = false;
};

static LvglDisplayPortState g_display_port;

static uint16_t draw_buffer_lines(const drivers::DisplaySize& size) {
    if (size.height < 20) {
        return size.height;
    }
    return 20;
}

static void release_draw_buffer() {
    if (g_display_port.buffer == nullptr) {
        return;
    }

    heap_caps_free(g_display_port.buffer);
    g_display_port.buffer = nullptr;
    g_display_port.buffer_pixels = 0;
}

static bool allocate_draw_buffer(const drivers::DisplaySize& size) {
    release_draw_buffer();

    const uint32_t pixel_count = static_cast<uint32_t>(size.width) * draw_buffer_lines(size);
    const size_t buffer_bytes = pixel_count * sizeof(lv_color_t);

    g_display_port.buffer = static_cast<lv_color_t*>(
        heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_display_port.buffer == nullptr) {
        g_display_port.buffer = static_cast<lv_color_t*>(
            heap_caps_malloc(buffer_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (g_display_port.buffer == nullptr) {
        Serial.printf("[ui.port] draw buffer alloc failed bytes=%u\n", static_cast<unsigned>(buffer_bytes));
        return false;
    }

    g_display_port.buffer_pixels = pixel_count;
    return true;
}

static void flush_display(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* color_map) {
    auto* display = static_cast<drivers::Display*>(driver->user_data);
    const drivers::DisplayRect rect(
        static_cast<uint16_t>(area->x1),
        static_cast<uint16_t>(area->y1),
        static_cast<uint16_t>(area->x2 - area->x1 + 1),
        static_cast<uint16_t>(area->y2 - area->y1 + 1));

    if (display != nullptr) {
        display->write_rect(rect, reinterpret_cast<const uint16_t*>(color_map));
    }
    lv_disp_flush_ready(driver);
}

bool bind_display(drivers::Display& display) {
    const auto size = display.size();
    if (!display.init()) {
        Serial.printf("[ui.port] display init failed=%s\n", display.name());
        return false;
    }

    if (!g_display_port.lvgl_ready) {
        lv_init();
        g_display_port.lvgl_ready = true;
    }

    if (!allocate_draw_buffer(size)) {
        return false;
    }

    g_display_port.display = &display;
    lv_disp_draw_buf_init(
        &g_display_port.draw_buffer,
        g_display_port.buffer,
        nullptr,
        g_display_port.buffer_pixels);

    lv_disp_drv_init(&g_display_port.driver);
    g_display_port.driver.hor_res = size.width;
    g_display_port.driver.ver_res = size.height;
    g_display_port.driver.flush_cb = flush_display;
    g_display_port.driver.draw_buf = &g_display_port.draw_buffer;
    g_display_port.driver.user_data = &display;

    g_display_port.handle = lv_disp_drv_register(&g_display_port.driver);
    if (g_display_port.handle != nullptr) {
        lv_disp_set_default(g_display_port.handle);
    }
    g_display_port.display_ready = g_display_port.handle != nullptr;

    Serial.printf("[ui.port] bind display=%s %ux%u\n",
                  display.name(),
                  size.width,
                  size.height);

    return g_display_port.display_ready;
}

void poll() {
    if (!g_display_port.lvgl_ready || !g_display_port.display_ready) {
        return;
    }

    lv_timer_handler();
}

}  // namespace nm::ui::port

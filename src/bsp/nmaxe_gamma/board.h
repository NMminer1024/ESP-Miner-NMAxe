#pragma once

#include "bsp/board.h"

namespace nm::bsp::nmaxe_gamma {

class DisplayDevice final : public drivers::Display {
public:
    bool init() override;
    const char* name() const override;
    drivers::DisplaySize size() const override;

    static const char* display_id() { return "gamma-st7789"; }
    static uint16_t screen_width() { return panel_config().width; }
    static uint16_t screen_height() { return panel_config().height; }
    static bool color_invert() { return panel_config().color_invert; }
    static bool default_flip() { return panel_config().default_flip; }
    static uint16_t default_rotation() { return default_flip() ? 1u : 3u; }

private:
    struct PanelConfig {
        uint16_t width = 240;
        uint16_t height = 135;
        int8_t power_pin = 18;
        int8_t backlight_pin = 17;
        int8_t backlight_pwm_channel = 0;
        uint32_t backlight_pwm_frequency = 1000 * 100;
        uint8_t backlight_pwm_resolution = 8;
        int8_t dc_pin = 47;
        int8_t reset_pin = 40;
        int8_t spi_cs_pin = 39;
        int8_t spi_mosi_pin = 48;
        int8_t spi_miso_pin = -1;
        int8_t spi_sclk_pin = 38;
        uint32_t spi_frequency_hz = 80000000;
        bool color_invert = true;
        bool default_flip = true;
    };

    struct InitCommand {
        uint8_t command;
        uint8_t data[14];
        uint8_t size;
        uint16_t delay_ms;
    };

    struct RotationConfig {
        uint8_t madctl;
        uint16_t colstart;
        uint16_t rowstart;
    };

    static const uint8_t kBacklightOffDuty = 255;
    static const uint8_t kBacklightOnDuty = 0;
    static const uint8_t kInitSequenceCount = 21;
    static const uint16_t kFillChunkPixels = 128;

    static const PanelConfig& panel_config() {
        static const PanelConfig config;
        return config;
    }

    static const InitCommand (&init_sequence())[kInitSequenceCount] {
        static const InitCommand sequence[kInitSequenceCount] = {
            // 11h SLPOUT:
            // Exit sleep mode. The controller needs a long delay for internal power blocks to stabilize.
            {0x11, {}, 0, 120},

            // 13h NORON:
            // Switch to normal display mode.
            {0x13, {}, 0, 0},

            // 36h MADCTL:
            // Set default memory access direction and BGR color order during init.
            // Final board orientation is applied later in apply_rotation().
            {0x36, {0x08}, 1, 0},

            // B6h Display Function Control:
            // Panel-specific gate/source behavior.
            // 0x0A, 0x82 is the legacy gamma panel setting carried from src_old/TFT_eSPI.
            {0xB6, {0x0A, 0x82}, 2, 0},

            // B0h RAMCTRL:
            // Configure RAM interface and RGB565 packing behavior.
            // 0xE0 enables the RGB565 transfer behavior expected by the old project.
            {0xB0, {0x00, 0xE0}, 2, 0},

            // 3Ah COLMOD:
            // Select 16-bit RGB565 pixel format.
            {0x3A, {0x55}, 1, 10},

            // B2h PORCTRL:
            // Porch timing parameters.
            // front/back porch timing copied from the working legacy gamma configuration.
            {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},

            // B7h GCTRL:
            // Gate control tuning.
            {0xB7, {0x35}, 1, 0},

            // BBh VCOMS:
            // Set common electrode voltage.
            {0xBB, {0x28}, 1, 0},

            // C0h LCMCTRL:
            // LCD module internal control tuning.
            {0xC0, {0x0C}, 1, 0},

            // C2h VDVVRHEN:
            // Enable VRH/VDV related writes.
            {0xC2, {0x01, 0xFF}, 2, 0},

            // C3h VRHS:
            // Set reference voltage level.
            {0xC3, {0x10}, 1, 0},

            // C4h VDVSET:
            // Set voltage delta value.
            {0xC4, {0x20}, 1, 0},

            // C6h FRCTRL2:
            // Frame rate control.
            {0xC6, {0x0F}, 1, 0},

            // D0h PWCTRL1:
            // Power control for source/gate drive.
            {0xD0, {0xA4, 0xA1}, 2, 0},

            // E0h PVGAMCTRL:
            // Positive gamma curve.
            {0xE0, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}, 14, 0},

            // E1h NVGAMCTRL:
            // Negative gamma curve.
            {0xE1, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}, 14, 0},

            // 21h INVON:
            // Enable display inversion. This matches the legacy gamma panel behavior.
            {0x21, {}, 0, 0},

            // 2Ah CASET:
            // Set full controller column address range.
            // controller column range = 0..239.
            {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4, 0},

            // 2Bh RASET:
            // Set full controller row address range.
            // controller row range = 0..319.
            {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4, 0},

            // 29h DISPON:
            // Turn the panel output on after all timing/power/gamma registers are ready.
            {0x29, {}, 0, 120},
        };
        return sequence;
    }

    static const RotationConfig& rotation_config(bool flip) {
        static const RotationConfig configs[] = {
            // flip = false, rotation = 3, MADCTL = MV | MY | BGR
            {0x20 | 0x80 | 0x08, 40, 52},

            // flip = true, rotation = 1, MADCTL = MX | MV | BGR
            {0x40 | 0x20 | 0x08, 40, 53},
        };
        return configs[flip ? 1 : 0];
    }

    void enable_panel_power();
    void setup_backlight_pwm();
    void set_boot_backlight_off();
    void set_backlight_on();
    void init_bus();
    void hardware_reset();
    void begin_transaction();
    void end_transaction();
    void select_panel();
    void release_panel();
    void set_command_mode();
    void set_data_mode();
    void write_command_with_data(uint8_t command, const uint8_t* data, uint8_t size);
    void run_init_sequence();
    void apply_rotation(bool flip);
    void begin_memory_write(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void end_memory_write();
    void fill_screen(uint16_t color);

    bool _initialized = false;
    RotationConfig _rotation{};
};

class NMAxeGammaBoard final : public bsp::Board {
public:
    NMAxeGammaBoard();

    void init() override;
    const char* key() const override;
    const BoardContext& context() const override;

private:
    BoardContext _context{};
};

}  // namespace nm::bsp::nmaxe_gamma

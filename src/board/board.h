#ifndef __NM_BOARD_H_
#define __NM_BOARD_H_
#include <Arduino.h>
#include "drivers/asic/bm_hal.h"
#include "nmaxe.h"
#include "nmaxegamma.h"
#include "nmqaxepp.h"
#include "drivers/power/power_hal.h"
#include <map>
#include "drivers/fan/fan.h"
#include <vector>

/******************default parameter define for NMAxe***********************/
#define BOARD_NMAXE_NAME                                "NMAxe"
#define BOARD_NMAXE_GAMMA_NAME                          "NMAxeGamma"
#define BOARD_NMQAXE_PLUS_PLUS_NAME                     "NMQAxe++"

#define CHIP_NMAXE_NAME                                 "BM1366"
#define CHIP_NMAXE_GAMMA_NAME                           "BM1370"
#define CHIP_NMQAXE_PLUS_PLUS_NAME                      "BM1370"


#define PRIMARY_POOL_URL                               "stratum+tcp://solo.ckpool.org:3333"//btc
#define FALLBACK_POOL_URL                              "stratum+tcp://pool.nmminer.com:3333" //xec

#define PRIMARY_USER                                   "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1"//btc
#define FALLBACK_USER                                  "ecash:qpf6dlpplgltcxuq4rve99jfk67z4tlcjc3sscrrsf"//xec

#define PRIMARY_POOL_PWD                               "x"
#define FALLBACK_POOL_PWD                              "x"

/*********************************Pin define********************************/
#define NM_AXE_MODEL_SELECT_PIN0                       15
#define NM_AXE_MODEL_SELECT_PIN1                       46

typedef enum {
    NMAXE            = 0b11,
    NMAXE_GAMMA      = 0b01,
    NMQAXE_PLUS_PLUS = 0b10,
    BOARD_UNKNOWN    = 0b00
} BoardModelType;

typedef struct{
    float       min;      // high temperature limit
    float       max;      // low temperature limit
}limited_data_f;

typedef struct{
    uint16_t    min;      // high temperature limit
    uint16_t    max;      // low temperature limit
}limited_data_u16;

typedef struct{
    bool        is_auto_speed;
    float       target_temp;  //asic temp
}fan_preference_info_t;

typedef struct{
    bool     flip;
    bool     auto_rolling;// auto rolling screen
    uint16_t brightness;
}screen_preference_info_t;

typedef struct{
    bool   enable;
    bool   sleep;
    bool   sleep_last;
}led_preference_info_t;

typedef struct{
    uint8_t   target_temp;    // target asic temperature in celsius degree
}asic_preference_info_t;

// ---- OC / VC option lists (defined before BoardSpecConfig so it can be used as a field) ----
struct work_option_t {
    String   name;   // display label
    uint16_t value;  // numeric value (MHz for OC, mV for VC)
};
// --------------------------------------------------------------------------------------------

// board config struct
struct BoardSpecConfig {
    String   name;

    struct{
        struct{
            uint32_t                    max_x_hr;       // max x axis in hashrate distribution
            uint32_t                    max_x_bars;     // count of max x axis
            uint32_t                    count;          // count of hashrate samples
            uint32_t                    time;           // duration of hashrate samples, seconds
            std::map<uint16_t, uint8_t> dist_map;       //<x, y> x:scale_x, y:percentage of hashrate in this scale, range from 0 to 100
        }hashrate_dist_page;

        struct{
            struct{
                limited_data_f vbus;
                limited_data_f ibus;
                limited_data_f power;
            }power;
            struct{
                limited_data_f mcu;
                limited_data_f asic;
                limited_data_f vcore;
                limited_data_f fan;
            }heat; 
            struct{
                limited_data_f asic_freq_req;
                limited_data_f vcore_req;
                limited_data_f vcore_measure;
            }performance; 
        }dashboard_page;

        struct{
            std::vector<work_option_t> oc;  // overclock frequency options
            std::vector<work_option_t> vc;  // vcore voltage options
        }setting_page;
    }ui;

    struct {
        uint16_t width;   // in pixels
        uint16_t height;  // in pixels
        int8_t dc_pin;   // data/command pin
        int8_t rst_pin;  // reset pin
        int8_t pwr_pin;  // power pin
        bool   color_invert; // true: invert color, false: normal
        struct {
            int8_t   pin;   // backlight pin
            int8_t   pwm_ch; // backlight pwm channel
            uint32_t pwm_freq; // backlight pwm frequency
            uint8_t  pwm_resolution; // backlight pwm resolution in bits
        }bl;
    }tft;

    struct {
        String   name;            // asic model name
        uint16_t num_req;         // required asic number
        uint32_t job_interval_ms; // ms, time interval between two asic jobs
        uint16_t req_frq;         // MHz, required frequency
        uint16_t default_frq;     // MHz, default frequency
        uint16_t req_vcore;       // mV, required core voltage
        uint16_t default_vcore;   // mV, default core voltage
        uint16_t min_vcore;       // mV, minimum core voltage
        uint16_t max_vcore;       // mV, maximum core voltage
        uint16_t diff_thr_init;   // initial difficulty threshold
        int8_t  rx_pin;           // ESP32 rx pin to asic tx pin
        int8_t  tx_pin;           // ESP32 tx pin to asic rx pin
        int8_t  rst_pin;          // ESP32 rst pin to asic rst pin
        uint32_t com_baud_init;   // initial communication baudrate 
        uint32_t com_baud_work;   // working communication baudrate
        struct {
            float high;      // high temperature limit
            float medium;    // medium temperature limit
            float low;       // low temperature limit
        }temp_limit;
        HardwareSerial *com_port; // HardwareSerial port instance
    }asic;

    struct {
        axe_pwr_enable_pin_t en_pins;             // power enable pins to control asic power rails, such as pll 0.8v, vdd 1.8v and vcore
        axe_pwr_adc_pin_t    adc_pins;            // adc pins to monitor vbus, ibus and vcore
        int8_t               vcore_regulator_pin; // pin to control vcore regulator, for some board with vcore regulator control
        int8_t               pgood_pin;           // pin to detect if Vcore is good
        int8_t               dc_plug_pin;         // pin to detect if DC is plugged in
        uint16_t             vbus_min_required;   // mV, minimum vbus voltage to start mining
        uint16_t             power_low_threshold; // Watt, power low warning threshold
        struct {
            float high;      // high temperature limit
            float medium;    // medium temperature limit
            float low;       // low temperature limit
        }temp_limit;
    }pwr;

    struct{
        int8_t              sda_pin;
        int8_t              scl_pin;
    }iic;

    struct{
        int8_t               miso_pin;
        int8_t               mosi_pin;
        int8_t               sclk_pin;
        int8_t               cs_pin;  // chip select pin
    }spi;

    struct{
        int8_t             user_pin; // user button as recover to factory default
        int8_t             boot_pin; // boot button as UI page switch
    }btn;

    struct{
        int8_t             wifi_pin; // wifi status led
        int8_t             pool_pin; // pool status led
        int8_t             sys_pin;  // system status led
    }led;

    struct{
        screen_preference_info_t screen;
        asic_preference_info_t   asic;
        fan_preference_info_t    fan;
        led_preference_info_t    led;
    }preference;

    std::vector<fan_config_t> fans;  // support multiple fans

    // creator function pointer
    BMxxx*       (*create_asic_instance)(HardwareSerial&, uint32_t, uint8_t, uint8_t, uint8_t);
    // Power HAL instance pointer
    AxePowerHal* (*create_power_instance)(axe_pwr_enable_pin_t, axe_pwr_adc_pin_t, uint8_t, uint8_t, uint8_t);
};

void hardware_pre_init(BoardSpecConfig config);
BoardModelType get_board_model();
BoardSpecConfig get_board_config(BoardModelType model);

#endif
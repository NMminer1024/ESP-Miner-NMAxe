#ifndef __NM_BOARD_H_
#define __NM_BOARD_H_
#include <Arduino.h>
#include "bm_hal.h"
#include "BM1366.h"
#include "BM1370.h"
#include "axe_pwr_hal.h"

/******************default parameter define for NMAxe***********************/
#define BOARD_MODEL                                    "NMAxe"
#define ASIC_MODEL                                     "BM1366"

#define PRIMARY_POOL_URL                               "stratum+tcp://public-pool.io:21496"//btc
#define FALLBACK_POOL_URL                              "stratum+tcp://pool.nmminer.com:3333" //xec

#define PRIMARY_USER                                   "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1"//btc
#define FALLBACK_USER                                  "ecash:qpf6dlpplgltcxuq4rve99jfk67z4tlcjc3sscrrsf"//xec

#define PRIMARY_POOL_PWD                               "x"
#define FALLBACK_POOL_PWD                              "x"

#define SCREEN_WIDTH                                   240
#define SCREEN_HEIGHT                                  135

// #define ASIC_JOB_DIFF_DEFAULT_THR                      512  //Default ASIC diff threshold to set as initial asic diff
#define ESP32_TO_ASIC_INIT_BUAD                        115200
#define ESP32_TO_ASIC_WORK_BUAD                        1000000

/*********************************Pin define********************************/
// #define NM_AXE_BUTTON_BOOT_PIN                         0
// #define NM_AXE_BUTTON_USER_PIN                         12

// #define NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN             14
// #define NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN            13
// #define NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN           10
// #define NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN    16

// #define NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN       21
// #define NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN    11

// #define NM_AXE_POWER_BM13xx_VCORE_ADC_PIN               1
// #define NM_AXE_POWER_BM13xx_VBUS_ADC_PIN                2
// #define NM_AXE_POWER_BM13xx_IBUS_ADC_PIN                3

// #define NM_AXE_ESP32_RX_TO_BM13xx                      44
// #define NM_AXE_ESP32_TX_TO_BM13xx                      43
// #define NM_AXE_ESP32_RST_TO_BM13xx                     45

// #define NM_AXE_FAN_PWM_PIN                             41
// #define NM_AXE_FAN_PWM_RPM_MEASURE_PIN                 42

// #define NM_AXE_IIC_SCL_PIN                             8
// #define NM_AXE_IIC_SDA_PIN                             9

#define NM_AXE_TFT_PWER_PIN                            18
#define NM_AXE_TFT_BL_PIN                              17

#define NM_AXE_LED_WIFI_STA_PIN                        6
#define NM_AXE_LED_POOL_STA_PIN                        4
#define NM_AXE_LED_SYS_STA_PIN                         5

#define NM_AXE_MODEL_SELECT_PIN0                       15
#define NM_AXE_MODEL_SELECT_PIN1                       46

typedef enum {
    NMAXE         = 0b11,
    NMAXE_GAMMA   = 0b01,
    NMQAXE        = 0b10,
    BOARD_UNKNOWN = 0b00
} BoardModelType;

// board config struct
struct BoardSpecConfig {
    String   name;
    struct{
        uint32_t hr_dist_max_x_hr;
        uint32_t hr_dist_max_x_bars;
    }ui_spec;

    struct {
        String   name;            // asic model name
        uint32_t job_interval_ms; // ms, time interval between two asic jobs
        uint16_t req_frq;         // MHz, required frequency
        uint16_t default_frq;     // MHz, default frequency
        uint16_t req_vcore;       // mV, required core voltage
        uint16_t default_vcore;   // mV, default core voltage
        uint16_t min_vcore;       // mV, minimum core voltage
        uint16_t max_vcore;       // mV, maximum core voltage
        uint16_t diff_thr_init;   // initial difficulty threshold
        uint8_t rx_pin;           // ESP32 rx pin to asic tx pin
        uint8_t tx_pin;           // ESP32 tx pin to asic rx pin
        uint8_t rst_pin;          // ESP32 rst pin to asic rst pin
    } asic_spec;

    struct {
        axe_pwr_enable_pin_t enable_pins;
        axe_pwr_adc_pin_t    adc_pins;
        uint8_t              vcore_regulator_pin;    
        uint8_t              pgood_pin;
        uint8_t              dc_plug_pin;
    } pwr_pins;

    struct{
        uint8_t              sda_pin;
        uint8_t              scl_pin;
    }iic_pins;

    struct{
        uint8_t              torch_pin;
        uint8_t              pwm_pin;
        uint16_t             self_test_rpm_thr; // RPM, minimum RPM when fan is at full speed in self-test
    }fan_spec;

    struct{
        uint8_t             user_pin; // user button as recover to factory default
        uint8_t             boot_pin; // boot button as UI page switch
    }btn_spec;

    // creator function pointer
    BMxxx* (*create_asic_instance)(HardwareSerial&, uint32_t, uint8_t, uint8_t, uint8_t);
    // Power HAL instance pointer

};




BoardModelType get_board_model();
BoardSpecConfig get_board_config(BoardModelType model);
#endif
#ifndef __NM_BOARD_H_
#define __NM_BOARD_H_
#include <Arduino.h>
#include "bm_hal.h"
#include "nmaxe.h"
#include "nmaxegamma.h"
#include "nmqaxepp.h"
#include "power_hal.h"
#include <map>
#include "fan.h"

/******************default parameter define for NMAxe***********************/
#define PRIMARY_POOL_URL                               "stratum+tcp://solo.ckpool.org:3333"//btc
#define FALLBACK_POOL_URL                              "stratum+tcp://pool.nmminer.com:3333" //xec

#define PRIMARY_USER                                   "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1"//btc
#define FALLBACK_USER                                  "ecash:qpf6dlpplgltcxuq4rve99jfk67z4tlcjc3sscrrsf"//xec

#define PRIMARY_POOL_PWD                               "x"
#define FALLBACK_POOL_PWD                              "x"

#define SCREEN_WIDTH                                   240
#define SCREEN_HEIGHT                                  135

// // #define ASIC_JOB_DIFF_DEFAULT_THR                      512  //Default ASIC diff threshold to set as initial asic diff
// #define ESP32_TO_ASIC_INIT_BUAD                        115200
// #define ESP32_TO_ASIC_WORK_BUAD                        1000000

/*********************************Pin define********************************/
#define NM_AXE_TFT_PWER_PIN                            18
#define NM_AXE_TFT_BL_PIN                              17

#define NM_AXE_MODEL_SELECT_PIN0                       15
#define NM_AXE_MODEL_SELECT_PIN1                       46

typedef enum {
    NMAXE            = 0b11,
    NMAXE_GAMMA      = 0b01,
    NMQAXE_PLUS_PLUS = 0b10,
    BOARD_UNKNOWN    = 0b00
} BoardModelType;

// board config struct
struct BoardSpecConfig {
    String   name;

    struct{
        struct{
            uint32_t                    max_x_hr;       // max x axis in hashrate distribution
            uint32_t                    max_x_bars;     // count of max x axis
            uint32_t                    times;          // count of hashrate samples
            uint32_t                    dura;           // duration of hashrate samples, seconds
            std::map<uint16_t, uint8_t> dist_map;//<x, y> x:scale_x, y:percentage of hashrate in this scale, range from 0 to 100
        }hr_dist_page;
        uint8_t             last_page;//last ui page index, restored on next boot
    }ui;

    struct{
        uint8_t   pwr_pin;
        uint8_t   bl_pin;
        uint32_t  width;
        uint32_t  height;
    }tft;

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
        uint8_t  rx_pin;           // ESP32 rx pin to asic tx pin
        uint8_t  tx_pin;           // ESP32 tx pin to asic rx pin
        uint8_t  rst_pin;          // ESP32 rst pin to asic rst pin
        uint32_t com_baud_init;   // initial communication baudrate 
        uint32_t com_baud_work;   // working communication baudrate
        HardwareSerial *com_port; // HardwareSerial port instance
    }asic;

    struct {
        axe_pwr_enable_pin_t en_pins;
        axe_pwr_adc_pin_t    adc_pins;
        uint8_t              vcore_regulator_pin;    
        uint8_t              pgood_pin;
        uint8_t              dc_plug_pin;
        uint16_t             vbus_min_required; // mV, minimum vbus voltage to start mining
    }pwr;

    struct{
        uint8_t              sda_pin;
        uint8_t              scl_pin;
    }iic;

    struct{
        uint8_t             user_pin; // user button as recover to factory default
        uint8_t             boot_pin; // boot button as UI page switch
    }btn;

    struct{
        uint8_t             wifi_pin; // wifi status led
        uint8_t             pool_pin; // pool status led
        uint8_t             sys_pin;  // system status led
    }led;

    struct{
        uint8_t              torch_pin;
        uint8_t              pwm_pin;
        uint8_t              pwm_channel;
        uint32_t             pwm_freq;       // Hz
        uint8_t              pwm_resolution; // bits
        uint16_t             p_cnt_h_limt;   // PCNT high limit value
        uint16_t             self_test_rpm_thr; // RPM, minimum RPM when fan is at full speed in self-test
        fan_pid_t            pid;
    }fan;

    // creator function pointer
    BMxxx*       (*create_asic_instance)(HardwareSerial&, uint32_t, uint8_t, uint8_t, uint8_t);
    // Power HAL instance pointer
    AxePowerHal* (*create_power_instance)(axe_pwr_enable_pin_t, axe_pwr_adc_pin_t, uint8_t, uint8_t, uint8_t);
};

void hardware_pre_init(void);
BoardModelType get_board_model();
BoardSpecConfig get_board_config(BoardModelType model);
#endif
#ifndef _NM_AXE_H_
#define _NM_AXE_H_



/***************************default parameter define************************/
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

#define ASIC_DEFAULT_FREQ                              550  //MHz
#define ASIC_VCORE_DEFAULT                             1200 //mV
#define ASIC_VCORE_MIN                                 1100 //mV
#define ASIC_VCORE_MAX                                 1300 //mV
#define ASIC_JOB_INTERVAL_MS                           2000 //ms
#define ASIC_JOB_DIFF_DEFAULT_THR                      512  //Default ASIC diff threshold to set as initial asic diff
#define ESP32_TO_ASIC_INIT_BUAD                        115200
#define ESP32_TO_ASIC_WORK_BUAD                        1000000


#define HR_DIST_HEALTH_X_AXIS_MAX_HR                   1000 //GH/s
#define HR_DIST_HEALTH_X_AXIS_BARS                     20   //how many samples for x axis


#define FAN_FULL_RPM_MIN                               (4200)
#define BOARD_LOW_POWER                                (10.0f)   //Watt
#define ASIC_TEMP_DANGER                               (75.0f)
#define ASIC_TEMP_NORMAL                               (50.0f)
#define VCORE_TEMP_DANGER                              (90.0f)
#define VCORE_TEMP_LOW                                 (50.0f)
#define BOARD_MCU_DANGER                               (60.0f)
#define HISTORY_DEEPTH                                 (1000*3600*24) // history depth, how long to keep the history, in seconds
#define HISTORY_SAMPLE_INTERVAL                        (2) // history sample interval, in seconds


/*********************************Pin define********************************/
#define NM_AXE_BUTTON_BOOT_PIN                         0
#define NM_AXE_BUTTON_USER_PIN                         12

#define NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN             14
#define NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN            13
#define NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN           10
#define NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN    16

#define NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN       21
#define NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN    11

#define NM_AXE_POWER_BM13xx_VCORE_ADC_PIN               1
#define NM_AXE_POWER_BM13xx_VBUS_ADC_PIN                2
#define NM_AXE_POWER_BM13xx_IBUS_ADC_PIN                3

#define NM_AXE_LED1_PIN                                4
#define NM_AXE_LED2_PIN                                5
#define NM_AXE_LED3_PIN                                6

#define NM_AXE_ESP32_RX_TO_BM13xx                      44
#define NM_AXE_ESP32_TX_TO_BM13xx                      43
#define NM_AXE_ESP32_RST_TO_BM13xx                     45

#define NM_AXE_FAN_PWM_PIN                             41
#define NM_AXE_FAN_PWM_RPM_MEASURE_PIN                 42

#define NM_AXE_IIC_SCL_PIN                             8
#define NM_AXE_IIC_SDA_PIN                             9

#define NM_AXE_TFT_PWER_PIN                            18
#define NM_AXE_TFT_BL_PIN                              17

#define NM_AXE_LED_WIFI_STA_PIN                        6
#define NM_AXE_LED_POOL_STA_PIN                        4
#define NM_AXE_LED_SYS_STA_PIN                         5

#define NM_AXE_MODEL_SELECT_PIN0                       15
#define NM_AXE_MODEL_SELECT_PIN1                       46


#endif
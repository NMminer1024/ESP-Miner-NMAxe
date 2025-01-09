#ifndef NM_AXE_1370_H
#define NM_AXE_1370_H


#define BOARD_MODEL                                   "NMAxe-Gamma"
#define ASIC_MODEL                                    "BM1370"


#define NM_AXE_BUTTON_BOOT_PIN                         0
#define NM_AXE_BUTTON_USER_PIN                         12

#define NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN             14
#define NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN            13
#define NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN           10
#define NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN    16

#define NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN       21
#define NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN    11

#define NM_AXE_POWER_BM13xx_VCORE_ADC_PIN      1
#define NM_AXE_POWER_BM13xx_VBUS_ADC_PIN       2
#define NM_AXE_POWER_BM13xx_IBUS_ADC_PIN       3

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

#define LED_WIFI_STA_PIN                                6
#define LED_POOL_STA_PIN                                4
#define LED_SYS_STA_PIN                                 5

#endif
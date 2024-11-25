#include "nm_pwr.h"
#include "logger.h"
#include "global.h"

#define VCORE_REGULATOR_PWM_CHANNEL     1
#define VCORE_REGULATOR_PWM_RESOLUTION  8
#define VCORE_REGULATOR_PWM_FREQ        (1000*100)

#define REG_IBUS_SAMPLE                 (0.01f)
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)
#define GAIN_VCORE_SAMPLE               (2.0f)


NMAxePowerClass::~NMAxePowerClass(){

}


bool NMAxePowerClass::init(void){
    this->_adc_ready = AxePowerHal::init();
    pinMode(this->_vcore_regulator_pwm_pin, OUTPUT);
    pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);
    pinMode(this->_dc_plug_pin, INPUT_PULLUP);

    ledcSetup(VCORE_REGULATOR_PWM_CHANNEL, VCORE_REGULATOR_PWM_FREQ, VCORE_REGULATOR_PWM_RESOLUTION);
    ledcAttachPin(this->_vcore_regulator_pwm_pin, VCORE_REGULATOR_PWM_CHANNEL);
    ledcWrite(VCORE_REGULATOR_PWM_CHANNEL, 0);
    this->good_xsem = xSemaphoreCreateCounting(1, 0);
    return this->_adc_ready;
}

bool NMAxePowerClass::is_adc_ready(void){
    return this->_adc_ready;
}


bool NMAxePowerClass::is_vcore_good(void){
    if(digitalRead(this->_vcore_pgood_pin) == HIGH){
        delay(1);
        return (digitalRead(this->_vcore_pgood_pin) == HIGH);
    }
    return false;
}

bool NMAxePowerClass::is_dc_pluged(void){
    //if not, that might be usb pd plug
    return (digitalRead(this->_dc_plug_pin) == HIGH);
}

void NMAxePowerClass::set_vdd_1v8(power_state_t state){
    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_enable_pins.pwr_1v8, LOW);
    } else {
        digitalWrite(this->_asic_pwr_enable_pins.pwr_1v8, HIGH);
    }
}

void NMAxePowerClass::set_pll_0v8(power_state_t state){
    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_enable_pins.pwr_0v8, LOW);
    } else {
        digitalWrite(this->_asic_pwr_enable_pins.pwr_0v8, HIGH);
    }
}

void NMAxePowerClass::set_vcore(power_state_t state){
    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_enable_pins.pwr_vcore, HIGH);
    } else {
        digitalWrite(this->_asic_pwr_enable_pins.pwr_vcore, LOW);
    }
}

void NMAxePowerClass::set_vcore_voltage(uint16_t req_mv){
    uint16_t pwm = 0;
    //pwm = 0.14*req_mv - 140
    if (req_mv >= 1100 && req_mv <= 1300) {
        pwm = 0.14 * (req_mv) - 140; //bias 10mv
    } else {
        pwm = 0; //default
    }
    ledcWrite(VCORE_REGULATOR_PWM_CHANNEL, pwm);
}





uint32_t NMAxePowerClass::get_vbus(void){
    uint32_t vadc = this->get_vbus_adc();
    // LOG_W("Vbus %dmv", (uint32_t)(vadc));
    return (uint32_t)(vadc * GAIN_VBUS_SAMPLE);
}

uint32_t NMAxePowerClass::get_ibus(void){
    uint32_t vadc = this->get_ibus_adc();
    // LOG_W("ibus %dmv", vadc);
    float real = (float)vadc / GAIN_IBUS_SAMPLE;
    uint32_t current = (uint32_t)(real / REG_IBUS_SAMPLE); //0.01 ohm sampling resistor
    return current;
}

uint32_t NMAxePowerClass::get_vcore(void){
    uint32_t vadc = this->get_vcore_adc();
    // LOG_W("vcore %dmv", vadc);
    return (vadc * GAIN_VCORE_SAMPLE);
}








void power_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    //detect power plug or pd plug
    if(g_nmaxe.power.is_dc_pluged()) LOG_I("DC plug detected...");
    else LOG_I("USB plug detected...");

    //detect vbus voltage, if lower than VBUS_MIN_VOLTAGE , wait for power settle or throw error
    g_nmaxe.power.init();
    while (g_nmaxe.power.get_vbus() < VBUS_MIN_VOLTAGE){
        LOG_W("Vbus is %.2fV , at least %.2fV required, waiting for power setup...", g_nmaxe.power.get_vbus()/1000.0, VBUS_MIN_VOLTAGE/1000.0);
        delay(1000);
    }
    while (!g_nmaxe.fan.self_test){
        LOG_W("Waiting for fan self test %d/%d...", g_nmaxe.fan.rpm, FAN_FULL_RPM_MIN);
        delay(1000);
    }
    
    //set vdd_1v8 and pll_0v8 power
    g_nmaxe.power.set_pll_0v8(PWR_ON);
    g_nmaxe.power.set_vdd_1v8(PWR_ON);
    //set vcore to default 1.0V, then wait for vcore power settle, and set vcore voltage to required voltage
    g_nmaxe.power.set_vcore_voltage(1000);
    delay(50);
    g_nmaxe.power.set_vcore(PWR_ON);
    while (!g_nmaxe.power.is_vcore_good()){
        LOG_W("Waiting for vcore power setup...");
        delay(100);
    }
    //set vcore voltage to required voltage
    g_nmaxe.power.set_vcore_voltage(g_nmaxe.asic.vcore_req);
    xSemaphoreGive(g_nmaxe.power.good_xsem);
    //exit
    vTaskDelete(NULL);
}




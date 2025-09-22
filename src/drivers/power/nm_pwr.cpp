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
    if (req_mv >= this->_vcore_min_mv && req_mv <= this->_vcore_max_mv) {
        pwm = 0.14 * (req_mv) - 140; // bias 140, linear 0.14
    } else {
        pwm = 0; //for safety
        LOG_E("Vcore request %dmV out of range %d-%d mV", req_mv, this->_vcore_min_mv, this->_vcore_max_mv);
    }
    ledcWrite(VCORE_REGULATOR_PWM_CHANNEL, pwm);
}

void NMAxePowerClass::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
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

    // set vcore range according to board model
    if(g_nmaxe.board.hw_model == BOARD_NMAxe){
        g_nmaxe.power->set_vcore_range(1100, 1300);
        LOG_I("Board model '%s', set vcore range to 1100-1300mV", g_nmaxe.board.hw_model.c_str());
    }
    else if(g_nmaxe.board.hw_model == BOARD_NMAxeGamma){
        g_nmaxe.power->set_vcore_range(1000, 1250);
        LOG_I("Board model '%s', set vcore range to 1000-1250mV", g_nmaxe.board.hw_model.c_str());
    }
    else{
        g_nmaxe.power->set_vcore_range(1000, 1250);
        LOG_W("Unknown board model '%s', set vcore range to 1000-1250mV", g_nmaxe.board.hw_model.c_str());
    }
    
    //detect power plug or pd plug
    if(g_nmaxe.power->is_dc_pluged()) LOG_I("DC plug detected...");
    else LOG_I("USB plug detected...");

    //detect vbus voltage, if lower than VBUS_MIN_VOLTAGE , wait for power settle or throw error
    g_nmaxe.power->init();
    delay(500);
    while (g_nmaxe.power->get_vbus() < VBUS_MIN_VOLTAGE){
        LOG_W("Vbus is %.2fV , at least %.2fV required, waiting for power setup...", g_nmaxe.power->get_vbus()/1000.0, VBUS_MIN_VOLTAGE/1000.0);
        delay(1000);
    }
    while (!g_nmaxe.preference.fan.self_test){
        delay(1000);
        LOG_W("Fan self test %d/%d...", g_nmaxe.preference.fan.rpm, FAN_FULL_RPM_MIN);
    }
    
    //set vdd_1v8 and pll_0v8 power
    g_nmaxe.power->set_pll_0v8(PWR_ON);
    g_nmaxe.power->set_vdd_1v8(PWR_ON);
    delay(50);
    //set vcore voltage to required voltage
    g_nmaxe.power->set_vcore_voltage(g_nmaxe.asic.vcore_req);
    delay(50);
    g_nmaxe.power->set_vcore(PWR_ON);
    while (!g_nmaxe.power->is_vcore_good()){
        LOG_W("Waiting for vcore power setup...");
        delay(100);
    }
    xSemaphoreGive(g_nmaxe.power->good_xsem);
    LOG_I("Power is ready.");
    while(true){
        uint32_t vcore_measure = g_nmaxe.power->get_vcore();
        int32_t err = vcore_measure - g_nmaxe.asic.vcore_req;
        if(abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, power ready", vcore_measure, g_nmaxe.asic.vcore_req, err);
            delay(200);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore voltage for error correction %d mV", vcore_measure, g_nmaxe.asic.vcore_req, err, err/5);
        static uint32_t vcore_set = g_nmaxe.asic.vcore_req;
        vcore_set -= err/5;//half error correction
        vcore_set = (vcore_set < g_nmaxe.power->get_vcore_min()) ? g_nmaxe.power->get_vcore_min() : vcore_set;
        vcore_set = (vcore_set > g_nmaxe.power->get_vcore_max()) ? g_nmaxe.power->get_vcore_max() : vcore_set;
        g_nmaxe.power->set_vcore_voltage(vcore_set);//half error correction
        delay(200);
    }
    //exit
    vTaskDelete(NULL);
}




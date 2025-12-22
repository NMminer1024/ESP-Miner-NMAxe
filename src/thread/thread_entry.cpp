#include "global.h"
#include "logger.h"



void power_thread_entry(void *args){
    board_sal_t *baord = (board_sal_t*)args;
    String taskName = "(power)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing power...");

    baord->power->set_vcore_range(baord->status.asic.vcore_min, baord->status.asic.vcore_max);
    LOG_I("Set vcore range to %d-%d mV", baord->power->get_vcore_min(), baord->power->get_vcore_max());

    //detect power plug or pd plug
    if(baord->power->is_dc_pluged()) LOG_I("DC plug detected...");
    else LOG_I("USB plug detected...");

    //detect vbus voltage, if lower than VBUS_MIN_VOLTAGE , wait for power settle or throw error
    baord->power->init();
    delay(500);
    while (baord->power->get_vbus() < baord->info.spec.pwr.vbus_min_required){
        LOG_W("Vbus is %.2fV , at least %.2fV required, waiting for power setup...", baord->power->get_vbus()/1000.0, baord->info.spec.pwr.vbus_min_required/1000.0);
        delay(1000);
    }
    while (!baord->info.preference.fan.self_test){
        delay(1000);
        LOG_W("Fan self test %d/%d...", baord->info.preference.fan.rpm, baord->info.spec.fan.self_test_rpm_thr);
    }
    
    //set vdd_1v8 and pll_0v8 power
    baord->power->set_pll_0v8(PWR_ON);
    baord->power->set_vdd_1v8(PWR_ON);
    delay(50);
    //set vcore voltage to required voltage
    baord->power->set_vcore_voltage(baord->status.asic.vcore_req);
    delay(50);
    baord->power->set_vcore_status(PWR_ON);
    while (!baord->power->is_vcore_good()){
        LOG_W("Waiting for vcore power setup...");
        delay(100);
    }
    xSemaphoreGive(baord->power->good_xsem);
    LOG_I("Power is ready.");
    while(true){
        uint32_t vcore_measure = baord->power->get_vcore();
        int32_t err = vcore_measure - baord->status.asic.vcore_req;
        if(abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, power ready", vcore_measure, baord->status.asic.vcore_req, err);
            delay(200);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore voltage for error correction %d mV", vcore_measure, baord->status.asic.vcore_req, err, err/5);
        static uint32_t vcore_set = baord->status.asic.vcore_req;
        vcore_set -= err/5;//half error correction
        vcore_set = (vcore_set < baord->power->get_vcore_min()) ? baord->power->get_vcore_min() : vcore_set;
        vcore_set = (vcore_set > baord->power->get_vcore_max()) ? baord->power->get_vcore_max() : vcore_set;
        baord->power->set_vcore_voltage(vcore_set);//half error correction
        delay(200);
    }
    //exit
    vTaskDelete(NULL);
}
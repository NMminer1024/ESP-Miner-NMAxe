
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/NMminer1024/ESP-Miner-NMAxe/total)
![GitHub commit activity](https://img.shields.io/github/commit-activity/t/NMminer1024/ESP-Miner-NMAxe)
![GitHub contributors](https://img.shields.io/github/contributors/NMminer1024/ESP-Miner-NMAxe)


<div align="center">
  <h1>Make it better</h1>
</div>

## NMAxe
***
- NMAxe and NMAxeGamma solo miner based on BM1366 and BM1370 which fork from [bitaxe](https://github.com/skot/bitaxe), hashrate around 450-550GH/s and 1.2-2.0TH/s.
- NMAxe [aliexpress](https://www.aliexpress.com/item/1005008053561633.html)

<div align="center">
  <table width="100%">
    <tr>
      <td width="50%" align="center"><img src="image/nmaxe-1.jpg" alt="nmaxe-1"></td>
      <td width="50%" align="center"><img src="image/nmaxe-2.jpg" alt="nmaxe-2"></td>
    </tr>
  </table>
</div>
<div align="center">
  <table width="100%">
    <tr>
      <td width="50%" align="center"><img src="image/5.jpg" alt="Gamma-1"></td>
      <td width="50%" align="center"><img src="image/6.jpg" alt="Gamma-2"></td>
    </tr>
  </table>
</div>

## Tutorial
***
- [NMTech YouTube Channel](https://www.youtube.com/@NMTech-official)


## Build requirements
***
| Platform           | framework  | PCB     | Firmware                     |
| :---------------   | :---------:|:-------:|:----------------------------:|
|espressif32@6.6.0   |    Arduino |JLC EDA  | VS code with platformio      |


## Features
***
- **SHA256d algorithm**
- **ESP32S3R8 wifi connection**
- **Stratum with ssl connection optional**
- **USB PD power supply support**
- **Raspberry Pi5 structure compatible**
- **User-friendly UI with a 1.14-inch TFT LCD driven by LVGL**
- **Low cost Vcore regulator solution**
- **Vcore adjustment without restart**
- **A more accurate way for calculating hashrate**
- **Dynamically adjust ASIC difficulty threshold**
- **[One click deployment](https://flash.nmminer.com/)**
- **[NMController_client](https://github.com/NMminer1024/NMController_client) monitor**.
- **[NMController_web](https://github.com/NMminer1024/NMController_web) monitor**.


## Buttons
***
| Buttons           | Action             | Description             |
| :---------------  | :-----------------:|:-----------------:      |
|boot               |    Double click    |  Back to previous page    |
|boot               |    Single click    |  Switch to next page    |
|boot               |    Long press      |  Force configuration    |
|user               |    Long press      |  Restore to factory settings  |

## Power 
### NMAxe
- DC adapter 8-12v/25W at least.
- USB PD charger 25W at least. 
### NMAxeGamma
- DC adapter 8-12v/40W at least.
- USB PD charger 40W at least. 

## Configuration
***

### Solo
| Url               | Type               | Port               | Description       | Minimum required difficulty|
| :---------------  | :-----------------:| :-----------------:|:-----------------:| :----------------------:   |
|solobtc.nmminer.com| tcp                |    3333           |         BTC        |  Minimum 0.001  |
|au.solobtc.nmminer.com| tcp             |    3333           |         BTC        |  Minimum 0.001  |
|solo.ckpool.org    | tcp                |    3333            |        BTC        | 10K initial, dynamically adjusted based on hashrate|
|pool.nmminer.com   | tcp                |    3333            |        XEC        | 1K initial, dynamically adjusted based on hashrate |
|eu.molepool.com    | tcp                |    7450            |        DGB        | Minimum 32.768K constantly         |
|eu.molepool.com    | tcp                |    5566            |        BCH        | Minimum 32.768K constantly         |
|eu.molepool.com    | tcp                |    2566            |        SPACE      | Minimum 32.768K constantly         |
|eu2.molepool.com   | tcp                |    1801            |        FB         | Minimum 32.768K constantly         |

### Pool(v2.2.xx at least)
| Url               | Type               | Port               | Description       | Region|
| :---------------  | :-----------------:| :-----------------:|:-----------------:| :----------------------:   |
|btc.f2pool.com     | tcp                |    1314            |        BTC        | common  |
|btc-asia.f2pool.com| tcp                |    1314            |        BTC        | Asia    |
|btc-na.f2pool.com  | tcp                |    1314            |        BTC        | North America|
|btc-euro.f2pool.com| tcp                |    1314            |        BTC        | Europe|
|btc-africa.f2pool.com| tcp              |    1314            |        BTC        | Africa|
|btc-latin.f2pool.com| tcp               |    1314            |        BTC        | Latin|

## How to monitor

- *Swarm*
<div align="center">
  <img src="image/swarm.jpg" alt="swarm">
</div>

- *Lucky statistics*
<div align="center">
  <img src="image/Lucky.png" alt="png">
</div>


## Firmware update
- *Via AxeOS ota*

Check the latest **firmware.bin** and **spiffs.bin**.

<div align="center">
  <img src="image/firmware-check.jpg" alt="firmware-check">
</div>

Batch firmware update on swarm page.
<div align="center">
  <img src="image/ota-batch.jpg" alt="ota-batch">
</div>



- *Via [NMMiner flash tool](https://flash.nmminer.com/)*

***Notice***：Long press and hold **boot** button，then click **reset** button to let the NMAxe force into bootloader mode, So that the computer will recognize a COMx device plug in.

<div align="center">
  <img src="image/nm-flash-tool.jpg" alt="nm-flash-tool">
</div>

## Recover A Bricked Device or Corrupt Firmware

If your device is inaccessible, use the [NMMiner flash tool](https://flash.nmminer.com/) to fully recover it.

**How to enter forced flash mode:**
1. Hold the **boot** button
2. Plug in the USB cable
3. Release the **boot** button

The browser will detect the device and you can flash the latest firmware directly.




## API Reference
***

Full API documentation: [docs/API.md](./docs/API.md)

> After flashing, an interactive version is also available on-device at `http://{device-ip}/api-doc`.

## Contact
- Anything do not work as your expectation, just let us know.

| Email                   |  Telegram                       | Home page           |
| :-----------------:     |  :-----------------:            |:-----------------:  |
|nmminer1024@gmail.com    |  https://t.me/NMAxe1024         |[NMTech](https://www.nmminer.com/) |


## Release Log
***

### (2026.04.01) - v3.0.10
- `Add`:
  - Screensaver: animated GIF screensaver support, with built-in GIF presets (240x135 / 320x240).
  - Screensaver: custom GIF upload via AxeOS, no reboot required.
  - AxeOS: Achievement page with fade-in animation on new personal best difficulty.
  - AxeOS: Interactive API reference page accessible at `http://{ip}/api-doc`.
  - AxeOS: Vcore fan control support for NMQAxe++.
  - Device Display: Screen auto-roll toggle on settings page for NMQAxe++.
  - Device Display: Swarm page added for NMAxe and NMAxeGamma.
  - API: `/alive` endpoint for swarm LAN discovery.
  - API: `/probe` now includes screen resolution fields `sw` / `sh`.
  - API: `/api/swarm/find` — locate a specific device in the swarm by blinking its screen.
  - OTA: RGB LED progress indicator during firmware flash(Only NMQAxe++ and ARGB required).
  - NTP: additional NTP server options.
- `Fixed`:
  - TFT_eSPI library compatibility issue.
  - Multiple task stack overflow issues; power task stack reduced from 7 KB to 3 KB.
  - Screen randomly going blank.
  - Recovery mode logic when SPIFFS partition is missing or corrupt.
  - Swarm cross-device OTA upgrade failure.
  - Boot button logic.
  - Swipe gesture and settings page UI issues.
  - lwIP / HTTP stability issues.
- `Improved`:
  - Overall task stack and memory usage optimization.
  - SPI bus speed increased to 80 MHz.
  - AxeOS Update page UI.
  - Loading page log display.
  - Swarm total power consumption display.
- `Modify`:
  - None.
- `Remove`:
  - MCU temperature field removed from system info.

### (2026.03.04) - v2.9.31
- `Add`:
  - Feature: Time and Date format settings in AxeOS.
- `Fixed`:
  - Issues on hashrate distribute page in AxeOS.
  - The pool class has issues that lead to random restarts.
- `Improved`:
  - None.
- `Modify`:
  - None.
- `Remove`:
  - None.

### (2026.02.25) - v2.9.30
- `Add`:
  - Firmware : 'All in one' for NMAxe, NMAxeGamma, NMQAxe++(Hardware Coming soon).
  - Firmware : Support multi-fan on NMQAxe++.
  - API:  http://{ip}/api/system/gauge/limits
  - API:  http://{ip}/api/system/settings/mining
- `Fixed`:
  - Init queen issue, Optimized boot process.
  - Low Power startup issue full Fixed – Upgrade Recommended if your NMAxeGamma ever fails.
  - Screen randomly goes blank.
- `Improved`:
  - Memory usage.
- `Modify`:
  - http://{ip}/api/system/info
- `Remove`:
  - None.

### (2025.10.24) - v2.9.21
- `Add`:
  - Miner will resumes on the last page after a shutdown or crash.
- `Fixed`:
  - RPM of fan logic.
- `Improved`:
  - Memory usage.
- `Modify`:
  - AxeOS : Change label from `Mining coin` to `Price Display`.
  - Default setting : Change primary pool `public-pool.io:21496` to `solo.ckpool.org:3333`.
  - Default setting ：Change Overclock from `550`MHz to `575`MHz and Vcore from `1200`mV to `1250`mV for NMAxe.
- `Remove`:
  - None.

### (2025.10.02) - v2.9.20
- `Add`:
  - AxeOS: Lucky statistics chart in dashboard page.
  - AxeOS: `clear Block hit` button.
- `Fixed`:
  - The issue of random restart.
- `Improved`:
  - Memory usage.
  - Algorithm of fan control, reduced fan noise.
  - Algorithm of Vcore control.
  - Fan polarity auto detect.
- `Modify`:
  - None.
- `Remove`:
  - AxeOS: fan polarity option.

### (2025.08.31) - v2.9.10
- `Add`:
  - None.
- `Fixed`:
  - The issue of PSRAM usage causes the miner to restart every 17 hours.
- `Improved`:
  - None.
- `Modify`:
  - None.
- `Remove`:
  - None.

### (2025.08.24) - v2.9.03
- `Add`:
  - PID controller for auto fan.
  - Target ASIC temperature setting on AxeOS(The target temperature cannot be locked when the ambient temperature is too high)
  - The default stratum user will have the hostname appended as a suffix.
- `Fixed`:
  - None.
- `Improved`:
  - Hashrate for 3m, 30m, 60m, reduce the computation time from 10ms to 70us.
  - Memory usage, resulting in smoother screen switching.
- `Modify`:
  - None.
- `Remove`:
  - None.

### (2025.08.08) - v2.9.02
- `Add`:
  - API: Hashrate distribution,`IP`/api/system/hr/dist
  - API: History status, `IP`/api/system/status/history
  - API: History realtime, `IP`/api/system/status/realtime
  - UI: Up to 24 hours of historical data storage chart on AxeOS.
  - UI: Hashrate distribution chart on AxeOS.
  - UI: Version timeline in `update` menu bar, and release note will sync from github repository.
- `Fixed`:
  - Swarm table can't drag on v2.9.01.
- `Improved`:
  - ANSI log console support, a colored log console.
- `Modify`:
  - When upgrading from v2.9.02 to a newer one, it's no longer necessary to clear the browser cache.
- `Remove`:
  - None.

### (2025.07.31) - v2.9.01
- `Add`:
  - None.
- `Fixed`:
  - User cannot save network config when configuring the miner for the first time.
- `Improved`:
  - A clean, modern AxeOS UI.
  - Swarm table, filter for high and low hashrate miners.
- `Modify`:
  - None.
- `Remove`:
  - None.

### (2025.07.22) - v2.8.02
- `Add`:
  - None.
- `Fixed`:
  - Primary pool switch back logic.
- `Improved`:
  - None.
- `Modify`:
  - NMAxe default OC 550MHz and 1200mV for summer ambient temperature.
- `Remove`:
  - None.

### (2025.07.18) - v2.8.01
- `Add`:
  - When the primary mining pool active again, miner will switch back to the primary pool within 10s.
  - Screen brightness will blink from 10%~100% when miner hit a block.
- `Fixed`:
  - BM1370 init queeze.
- `Improved`:
  - Memory usage.
- `Modify`:
  - None.
- `Remove`:
  - None.

### (2025.07.10) - v2.7.05
- `Add`:
  - None
- `Fixed`:
  - ssl connection issue.
- `Improved`:
  - realtime log on AxeOS.
- `Modify`:
  - The hostname can be edited and modified by the user.
- `Remove`:
  - firmware update reminder.

### (2025.06.02) - v2.7.04
- `Add`:
  - Auto TimeZone fetch base on public IP.
- `Fixed`:
  - Monitor thread issue.
- `Improved`:
  - AxeOS UI.
- `Modify`:
  - None.
- `Remove`:
  - Timezone box in AxeOS.

### (2025.05.23) - v2.7.03
- `Add`:
  - Auto screen scrolling option on AxeOS.
  - TimeZone and ntp service.
  - Big digital display page on NMAxe and Gamma.
- `Fixed`:
  - Low power issue.
- `Improved`:
  - Default OverClock settings(600/1125).
- `Modify`:
  - OverClock display on dashboard page.
- `Remove`:
  - Vbus display from dashboard page.

### (2025.05.21) - v2.7.02
- `Add`:
  - None.
- `Fixed`:
  - Low power issue.
  - Block hit counter issue.
- `Improved`:
  - Random delay for wifi connection when miner startup.
  - Vcore order.
- `Modify`:
  - NMaxe-Gamma default Setting 600Mhz/1125mV.
- `Remove`:
  - None.

### (2025.05.17) - v2.6.01
- `Add`:
  - None.
- `Fixed`:
  - Auto fan logic.
- `Improved`:
  - None.
- `Modify`:
  - NMaxe-Gamma default Setting 595Mhz/1125mV.
- `Remove`:
  - softAP password.

### (2025.05.11) - v2.5.51
- `Add`:
  - None.
- `Fixed`:
  - None.
- `Improved`:
  - None.
- `Modify`:
  - Default Setting 600Mhz/1125mV.
  - Clear Extra2 every job receive.
  - Default difficulty for BM1366 set to 256, BM1370 to 512.

### (2025.04.03) - v2.5.50
- Add:
  - None.
- Fixed:
  - Restart button inactive on AxeOS when QR code appear.
- Improved:
  - None.
- Modify:
  - ASIC danger temp.
  - boot button logic: single click -> page switch,  double click -> screen and led sleep.

### (2025.03.30) - v2.5.41
- Add:
  - None.
- Fixed:
  - Fan self test bug.
  - Restart button inactive on AxeOS when miner loading.
- Improved:
  - None.
- Modify:
  - None.

### (2025.03.26) - v2.5.31
- Add:
  - None.
- Fixed:
  - Fallback pool issues.
- Improved:
  - Real time log.
- Modify:
  - None.

### (2025.03.22) - v2.5.30
- Add:
  - Benchmark support.
- Fixed:
  - Default settings with BM1366 and BM1730.
- Improved:
  - Real time log.
- Modify:
  - Default wallet address pad with worker name.

### (2025.03.05) - v2.5.20
- Add:
  - Batch firmware upgrade on swarm page.
  - BM1370 class.
- Fixed:
  - AxeOS compatible NMAxe and NMAxe-Gamma.
- Improved:
  - Swarm.
- Modify:
  - None.

### (2025.02.13) - v2.5.10
- Add:
  - BSV,FB,SPACE price.
- Fixed:
  - NMAxe will reboot when refresh the AxeOS frequently.
- Improved:
  - Swarm.
- Modify:
  - Stratum password to text type.
  - NMAxe and NMAxe-Gamma board select pin.

### (2025.01.30) - v2.5.02
- Add:
  - Stratum user for primary and fallback.
- Fixed:
  - None.
- Improved:
  - Swarm.
- Modify:
  - None.


### (2025.01.25) - v2.5.01
- Add:
  - Fallback pool.
- Fixed:
  - Swarm issues.
- Improved:
  - AxeOS follow 'skot' version, theme preference option for user. (clear your browser cache after firmware update)
  - The primary pool will back to 'public-pool.io' after you update this version, just change it as you want. 
  - Display the IP of the NMAxe on the loading page. Even if the NMAxe is stuck on the loading page, you can log in to the device through the IP.
- Modify:
  - In order to compatible noctua fan,Fan self test min rpm from 4800 to 4200.

### (2025.01.22) - v2.4.13
- Add:
  - Market price support BTC, BCH, XEC, DGB.
- Fixed:
  - Pool connection timeout.
  - Stratum waiting response issue.
- Improved:
  - System stable.
  - Swarm.
- Modify:
  - Disable IP scroll on NMAxe.

### (2025.01.19) - v2.4.12
- Add:
  - Column sort feature for swarm.
  - Summary of swarm list.
- Fixed:
  - None
- Improved:
  - BM1366 class.
- Modify:
  - Input voltage danger threshold to 5v. 

### (2025.01.09) - v2.4.11
- Add:
  - None
- Fixed:
  - None
- Improved:
  - Swarm.
- Modify:
  - ssl default in first flash for test in BTC banned region, tcp connection once long press user button.

### (2025.01.03) - v2.4.10
- Add:
  - Latest firmware check on AxeOS.
- Fixed:
  - Restart issue.
- Improved:
  - None.
- Modify:
  - None.

### (2024.12.31) - v2.3.20
- Add:
  - None.
- Fixed:
  - Hashrate on pool drop to zero.
- Improved:
  - Stability.
  - Pretty log in AxeOS.
- Modify:
  - Order of session uptime and total uptime. 
  - Swarm support 60 devices.

### (2024.12.18) - v2.3.10
- Add:
  - Share latency.
  - Swarm list.
  - Hashrate real time page on NMAxe.
- Fixed:
  - CPU1 wtd restart issue.
  - Auto fan issue.
  - Low power and hashrate expect issue.
- Improved:
  - Increase share accept rate.
- Modify:
  - Hashrate on 3m, 5m, 60m.

### (2024.12.18) - v2.2.23
- Add:
  - None.
- Fixed:
  - Stratum user issues.
- Improved:
  - Increase share accept rate.
- Modify:
  - None.

### (2024.12.14) - v2.2.22
- Add:
  - Hashrate real time page.
  - Screen brightness adjustment.
- Fixed:
  - Restart issues.
- Improved:
  - None.
- Modify:
  - None.

### (2024.12.08) - v2.2.11
- Add:
  - Pool mining support.
  - Local dashboard.
  - Local hashrate healthy page.
- Fixed:
  - None.
- Improved:
  - None.
- Modify:
  - None.

### (2024.11.30) - v2.1.15
- Add:
  - None.
- Fixed:
  - Order of vcore settle.
  - DNS issue.
- Improved:
  - None.
- Modify:
  - Danger temp.

### (2024.11.25) - v2.1.14
- Add:
  - None.
- Fixed:
  - Soft AP name issue.
- Improved:
  - None.
- Modify:
  - None.

### (2024.11.13) - v2.1.13
- Add:
  - None.
- Fixed:
  - ASIC not found reboot issue.
- Improved:
  - None.
- Modify:
  - Led indicator enable default.
  - Overclock 575Mhz and Vcore 1300mV default.
  - Fan self-test threshold changed to 4800 rpm.

### (2024.11.13) - v2.1.12
- Add:
  - None.
- Fixed:
  - None.
- Improved:
  - None.
- Modify:
  - Vcore bias set to 10mV.

### (2024.11.13) - v2.1.11
- First push.
- Checkout NMAxe branch.


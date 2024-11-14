
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/NMminer1024/ESP-Miner-NMAxe/total)
![GitHub commit activity](https://img.shields.io/github/commit-activity/t/NMminer1024/ESP-Miner-NMAxe)
![GitHub contributors](https://img.shields.io/github/contributors/NMminer1024/ESP-Miner-NMAxe)

# NMAxe
***
- NMAxe solo miner based on BM1366 which fork from [bitaxe](https://github.com/skot/bitaxe).
- NMAxe [aliexpress](https://www.aliexpress.com/item/1005008053561633.html)
<div align="center">
  <table width="100%">
    <tr>
      <td width="50%" align="center"><img src="image/nmaxe-1.jpg" alt="nmaxe-1"></td>
      <td width="50%" align="center"><img src="image/nmaxe-2.jpg" alt="nmaxe-2"></td>
    </tr>
  </table>
</div>

## Build requirements
***
| Platform           | framework  | PCB     | Firmware                     |
| :---------------   | :---------:|:-------:|:----------------------------:|
|espressif32@6.6.0   |    Arduino |JLC EDA  | VS code with platformio      |


## Features
***
- **ESP32S3R8 wifi connection**
- **Stratum with ssl connection**
- **USB PD power supply support**
- **Raspberry Pi5 structure compatible**
- **User-friendly UI with a 1.14-inch TFT LCD driven by LVGL**
- **Low cost Vcore regulator solution**
- [NMController_client](https://github.com/NMminer1024/NMController_client) monitor.
- [NMController_web](https://github.com/NMminer1024/NMController_web) monitor.


### Buttons
***
| Buttons           | Action             | Description             |
| :---------------  | :-----------------:|:-----------------:      |
|boot               |    Unused          |        --               |
|user               |    Long press      |  Restore to factory settings  |

### Power 
- 8-12v/25W dc adapter least.
- USB PD charger 25W at least. 



### Configuration
***
## Pool
| Url               | Type               | Port               | Description       | Minimum required difficulty|
| :---------------  | :-----------------:| :-----------------:|:-----------------:| :----------------------:   |
|public-pool.io     | tcp                |    21496           |        BTC        | 1M initial, Minimum 0.001  |
|solo.ckpool.org    | tcp                |    3333            |        BTC        | 10K initial, dynamically adjusted based on hashrate|
|pool.nmminer.com   | tcp                |    3333            |        XEC        | 1K initial, dynamically adjusted based on hashrate |
|eu.molepool.com    | tcp                |    7450            |        DGB        | Minimum 32.768K constantly         |
|eu.molepool.com    | tcp                |    5566            |        BCH        | Minimum 32.768K constantly         |
|eu.molepool.com    | tcp                |    2566            |        SPACE      | Minimum 32.768K constantly         |
|eu.molepool.com    | tcp                |    1801            |        FB         | Minimum 32.768K constantly         |


## How to monitor
- In fact, both ***NMController_client*** and ***NMController_web*** have the same feature, ***NMController_client*** for Windows, ***NMController_web*** for Windows and MACOS, We make an example by ***NMController_client*** here.

- ***NMController_client***, scan the machine in your LAN, just as below.

<div align="center">
  <img src="image/nmcontroller-home.jpg" alt="nmcontroller-home">
</div>

- Redirect to the web monitor for more details. 

<div align="center">
  <img src="image/nmcontroller-details.jpg" alt="nmcontroller-details">
</div>

## Firmware update
***
- *Via AxeOS ota*
<div align="center">
  <img src="image/ota.jpg" alt="ota">
</div>


- *Via [NMMiner flash tool](https://flash.nmminer.com/)*
<div align="center">
  <img src="image/nm-flash-tool.jpg" alt="nm-flash-tool">
</div>

## TODO
- According to different mining coin, the corresponding market prices are displayed.


## Contact
- Anything do not work as your expectation, just let us know.

| Email                   |  Telegram                       |
| :-----------------:     |  :-----------------:            |
|nmminer1024@gmail.com    |  https://t.me/NMAxe1024         |



## Release Log
***
### (2024.11.13) - v2.1.11
- First push.
- Checkout NMAxe branch.


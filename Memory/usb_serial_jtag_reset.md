# USB cable insertion / port open resets the ESP32-S3 (rst:0x15)

## ⚠️ Correction (same day): the capture tool caused most of these resets

The first version of this note concluded the resets were plain hardware behaviour
triggered by RTS. That was incomplete, and it produced a **false lead**: the
"plugging the cable reboots the board" symptom was in large part caused by this
repo's own closed-loop serial capture (`serial_log.ps1`), not by the cable and
not by any firmware regression. Evidence — every `# ---- serial COMx opened ----`
marker in the captures is immediately followed by a USB reset:

| capture log | marker | reset line right after it |
|---|---|---|
| serial_COM19_124.13 | 22:16:30 | `rst:0x15 (USB_UART_CHIP_RESET)` |
| serial_COM19_124.13 | 22:19:09 | `rst:0x15` |
| serial_COM17_124.18 | 22:18:05 | `rst:0x15` |
| serial_COM17_124.18 | 22:21:20 | `rst:0x15` |
| serial_COM17_124.18 | 22:22:19 | `rst:0x15` |
| serial_COM4_124.10 | 22:18:29 | `rst:0x15` |

6 of 6. The very first open (COM4 at 21:21:44) did **not** reset, which is why the
behaviour looked intermittent and version-dependent.

Why the script does it: `serial_log.ps1` runs an infinite reconnect loop, so it
re-opens the port every time the handle dies — and every open re-applies the
modem lines. Setting `DtrEnable/RtsEnable = false` **before** `Open()` is not
reliable, and re-asserting them **after** `Open()` (the hardening added below) is
too late: the SET_CONTROL_LINE_STATE that resets the chip has already been sent.
Each reboot therefore also *creates* the next reopen, i.e. the tool manufactures
the very reboot it is supposed to be observing.

Consequence for the reboot-history data, on the boards measured that evening:

- 192.168.124.10 `idx=5 reset=unknown class=external ran=3026s` — mine.
- 192.168.124.18 `idx=33/34/36 reset=unknown class=external ran=99/17/5s` — mine.
- 192.168.124.18 `idx=31 detail=usb_uart ran=36s` — mine (deliberate RTS test).

These are **not** crashes and must not be read as such. The genuine `async_tcp`
panics stay distinguishable: `class=crash` + `detail=panic` + a coredump present.

The A/B that originally motivated this note ("3.2.01 does not reset, 3.2.02x
does") is therefore contaminated: whether a background capture was running
differed between the two runs, not the firmware.

**Rule: never attach a serial capture to a board whose uptime or reboot history
is the evidence under test.** Use the WS log stream for live boards.

## Symptom (as originally reported)

Plugging USB into a board, or opening its serial port, reboots the miner. The
uptime is lost and `/api/reboot/last` used to report

```json
{"reset":"unknown","intent":"none","class":"external","detail":"unknown"}
```

which looks exactly like an unexplained crash and sends anyone debugging in the
wrong direction.

## Root cause of the reset itself (hardware)

`rst:0x15` is `RESET_REASON_CORE_USB_UART` (`soc/reset_reasons.h`:
"USB UART resets the digital core"). The ESP32-S3 **USB-Serial-JTAG** peripheral
resets the chip in response to the host's modem-control state. Measured with a
DTR/RTS matrix against COM17 / board 192.168.124.18:

| DTR | RTS | result |
|---|---|---|
| 0 | 0 | no reset |
| 1 | 0 | no reset |
| 0 | **1** | **reset** |
| 1 | **1** | **reset** |

**RTS alone decides it; DTR is irrelevant.** Windows asserts the modem lines when
it enumerates the CDC device (cable insertion) and some terminal programs assert
RTS when they open the port, so either action resets the board once.

This is NOT a recent code change:

- `platformio.ini` `ARDUINO_USB_MODE=1` / `ARDUINO_USB_CDC_ON_BOOT=1` are
  unchanged since the 2026-06-22 baseline (`0796f782`).
- `Serial.begin(115200)` in `src/app/application.cpp` is unchanged since
  2026-05-15 (`62db0e52`).

It simply became *visible* recently: the reboot history now records every boot,
and closed-loop debugging means cables are being plugged in and ports opened.

## Why it was reported as "unknown"

`esp_reset_reason()` maps the SoC cause to `esp_reset_reason_t`, but this IDF
version's enum has **no USB member**, so `0x15`/`0x16` collapse to
`ESP_RST_UNKNOWN`, which `classify()` sends to `REBOOT_CLASS_EXTERNAL` with the
detail word `"unknown"`.

## Fix (implemented + verified on hardware)

`src/utils/reboot_log/reboot_log.cpp`: new `soc_reset_cause_detail()` reads
`rtc_get_reset_reason(0)` when the coarse reason is `ESP_RST_UNKNOWN` and names
the cause:

```cpp
case 0x15: return "usb_uart";   // RESET_REASON_CORE_USB_UART
case 0x16: return "usb_jtag";   // RESET_REASON_CORE_USB_JTAG
```

The detail overrides the generic word, so the boot log and the API now read
`reset=unknown class=external detail="usb_uart"`.

Verified on 192.168.124.18 (fw v3.2.02a, sha `b800cbbacf21ab55`) by asserting RTS
on purpose:

```json
{"idx":31,"uptime":36,"reset":"unknown","class":"external","detail":"usb_uart"}
```

The record right before the fix, for contrast, still says `"detail":"unknown"`.

Also hardened: `.github/skills/board-closed-loop-regression/scripts/serial_log.ps1`
now re-asserts `DtrEnable=false` / `RtsEnable=false` immediately **after**
`Open()`. Setting those properties only before `Open()` is not reliable — the
driver can apply the default (high) state at open time, which is what reset a
board whose 21 h uptime was the evidence being collected.

## Consequences for debugging

- Losing uptime to a cable is indistinguishable from losing it to a crash unless
  you read `detail`. Always check `detail` before concluding "it crashed".
- **Never attach serial to a unit whose long uptime is the evidence under test.**
  On 192.168.124.13 that destroyed a 76 122 s (21.1 h) run.
- The reset itself is harmless: no coredump is written (`coredump.present` stays
  false) and mining resumes normally after reboot.

## Avoidance

- Prefer the WS log stream (`ws_log.ps1`) while a board is live-mining: no USB,
  no reset.
- If USB is required, open the port with DTR/RTS low (the hardened script does
  this) and expect one reset at first enumeration.
- A board that must keep uptime should not have a CDC port opened at all.

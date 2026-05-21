export interface HelpSection {
  heading?: string;
  body: string;
}

export interface HelpEntry {
  title: string;
  icon: string;
  sections: HelpSection[];
  callout?: string;
}

export const HELP_CONTENT: Record<string, HelpEntry> = {

  // ── Benchmark ─────────────────────────────────────────────────────────────
  benchmark: {
    title: 'Benchmark',
    icon: 'pi pi-chart-bar',
    sections: [
      {
        heading: '🔬 What is it?',
        body: 'Benchmark automatically sweeps a grid of Frequency × Vcore combinations, measuring real hashrate, power draw, and chip temperature at each operating point. The result table lets you identify the most efficient (lowest J/TH) or highest-performance setting for your specific miner chip.'
      },
      {
        heading: '⚙ How it works',
        body: 'The sweep starts at Freq Min / Vcore Min and increments by the configured Step values until reaching Max. At each point the miner first enters a Warmup phase — waiting for temperature to stabilise — then a Sampling phase where hashrate is averaged over the configured time. All results are saved to NVS flash and survive device reboots.'
      },
      {
        heading: '📐 Parameters',
        body: '• Freq Min / Max / Step — Frequency sweep range in MHz (e.g. 400 → 600, step 25)\n• Vcore Min / Max / Step — Core voltage range in mV (e.g. 1100 → 1250, step 25)\n• Warmup Time — Seconds to wait for temperature to stabilise before sampling begins\n• Benchmark Time — Seconds to record hashrate at each operating point\n• Sample Interval — How often (seconds) hashrate is polled during Benchmark Time'
      },
      {
        heading: '📊 Reading results',
        body: '• Avg HR — Measured average hashrate (GH/s)\n• Exp HR — Expected hashrate at that frequency (theoretical max)\n• Eff (J/TH) — Energy efficiency — lower is better\n• Avg Pwr — Average power draw at that operating point (W)\n• Best Efficiency row is highlighted in green; Best Hashrate in blue'
      },
      {
        heading: '⚠ Notes',
        body: '• A full sweep can take hours. Use Stop to pause and Resume to continue from where you left off.\n• Do NOT change Settings while a benchmark is running — it may corrupt sweep results.\n• Reset erases all NVS benchmark data and restores factory sweep parameters.\n• Results survive a factory reset (10-second long-press) but are erased by Reset Benchmark.\n• Temperature readings are VRM (voltage regulator) and ASIC (chip) respectively.'
      }
    ]
  },

  // ── Home / Dashboard ───────────────────────────────────────────────────────
  home: {
    title: 'Home — Dashboard',
    icon: 'pi pi-home',
    sections: [
      {
        heading: '⚡ Hash Rate',
        body: 'The current measured hashrate output of your miner in GH/s. The smaller "Expected" value below it is the theoretical maximum calculated from chip frequency × number of cores. A large gap between actual and expected usually means some ASIC cores are failing or the pool rejects shares.'
      },
      {
        heading: '📊 Shares',
        body: '• Accepted — Valid shares acknowledged by the pool/node. In solo mining, shares do not generate direct income — they are proof-of-work submissions. A share that meets the full network difficulty would constitute a block solve.\n• Rejected — Shares the pool refused (stale, invalid difficulty, etc.). A high reject rate may indicate pool latency or incorrect settings.\n• Difficulty — The share difficulty your pool/node has assigned; higher = fewer but harder shares per unit time.'
      },
      {
        heading: '🍀 Luck',
        body: 'Luck compares how often you find a share vs. the statistical expectation. 100% = exactly on target, > 100% = lucky (finding shares faster than expected), < 100% = unlucky. Luck fluctuates naturally and averages out over time.'
      },
      {
        heading: '🌡 Temperature',
        body: 'Shows ASIC chip temperature and VRM (voltage regulator) temperature. Sustained ASIC temperature above ~85 °C may reduce chip lifespan. If temperatures are high, increase fan speed or reduce frequency/voltage in Settings.'
      },
      {
        heading: '💰 Best Difficulty',
        body: 'The highest single-share difficulty ever found by this miner. A share at Bitcoin network difficulty (currently ~90T) would be a block solve. This value is saved to flash and survives reboots.'
      }
    ]
  },

  // ── Settings — ASIC ───────────────────────────────────────────────────────
  'settings-asic': {
    title: 'Settings — ASIC',
    icon: 'pi pi-sliders-h',
    sections: [
      {
        heading: '📐 Frequency',
        body: 'Clock frequency of the ASIC chip in MHz. Higher frequency = higher hashrate but also higher power draw and temperature. The relationship is roughly linear for hashrate but cubic for power — doubling frequency roughly doubles hashrate but uses ~4× the power.'
      },
      {
        heading: '⚡ Core Voltage (Vcore)',
        body: 'Supply voltage to the ASIC cores in mV. Lower voltage = less power and heat; too low and the chip becomes unstable and produces invalid hashes. Use Benchmark to find the minimum stable voltage for your frequency.'
      },
      {
        heading: '🌀 Fan Speed',
        body: 'Fan duty cycle (0–100%). Higher % = more airflow = lower temperatures. "Auto" mode adjusts speed based on target temperature. Manual mode holds a fixed speed.'
      },
      {
        heading: '⚠ Notes',
        body: '• Do not run Settings changes while Benchmark is active.\n• Overvoltage can permanently damage the chip — stay within manufacturer-rated limits.\n• After changing frequency or voltage, hashrate takes ~30 seconds to stabilise.'
      }
    ]
  },

  // ── Settings — Pool ───────────────────────────────────────────────────────
  'settings-pool': {
    title: 'Settings — Mining Pool',
    icon: 'pi pi-server',
    sections: [
      {
        heading: '🔗 Stratum URL',
        body: 'The pool connection address in the format:\n  stratum+tcp://pool.example.com:3333\nMost pools provide this on their "Connect" or "Getting Started" page. The port number (e.g. 3333, 3334, 443) may vary by pool and difficulty tier.'
      },
      {
        heading: '👛 Wallet / Username',
        body: 'Your Bitcoin wallet address or pool account username. For solo/p2pool-style setups this is your wallet address directly. For custodial pools (NiceHash, Antpool, etc.) it is usually your registered username.'
      },
      {
        heading: '🔑 Worker Name (Password)',
        body: 'An optional identifier appended to your username (username.worker1) to distinguish multiple miners on the same account. Many pools use the Password field to pass worker name; enter "x" or leave blank if unsure.'
      },
      {
        heading: '⚠ Notes',
        body: '• Changes take effect after saving and rebooting.\n• The device tries the primary pool first; if it cannot connect within ~30 seconds it falls back to the backup pool.\n• Latency shown in the top bar is the RTT to the active Stratum server.'
      }
    ]
  },

  // ── Network ───────────────────────────────────────────────────────────────
  network: {
    title: 'Network Settings',
    icon: 'pi pi-wifi',
    sections: [
      {
        heading: '📶 WiFi',
        body: 'Enter your 2.4 GHz WiFi SSID and password. The ESP32 only supports 2.4 GHz — 5 GHz networks will not appear. After saving, the device reboots and connects to the new network.'
      },
      {
        heading: '🖥 Static IP',
        body: 'By default the device gets an IP from your router via DHCP. If you want a fixed IP (useful for the Benchmark IP display and port-forwarding), enable Static IP and fill in IP / Gateway / Subnet Mask / DNS fields.'
      },
      {
        heading: '⚠ Notes',
        body: '• If you enter wrong WiFi credentials, the device enters AP mode (hotspot "NMAxe-Setup") so you can reconfigure it.\n• A static IP outside your router\'s subnet will make the device unreachable — double-check Gateway and Subnet Mask.'
      }
    ]
  },

  // ── Monitor ───────────────────────────────────────────────────────────────
  monitor: {
    title: 'Monitor — Charts',
    icon: 'pi pi-chart-line',
    sections: [
      {
        heading: '📈 Hashrate chart',
        body: 'Shows rolling 10-minute averaged hashrate (GH/s) over time. The dashed line is the expected hashrate at the current frequency. Dips indicate rejected shares, pool reconnections, or thermal throttling.'
      },
      {
        heading: '🌡 Temperature chart',
        body: 'ASIC and VRM temperatures over time. Sustained spikes may indicate inadequate cooling or a chip running near its thermal limit.'
      },
      {
        heading: '🔍 Troubleshooting',
        body: '• Hashrate well below expected — Check ASIC frequency in Settings, or run Benchmark to find a stable operating point.\n• Frequent drops to zero — Pool connectivity issue; check pool URL and WiFi signal (dBm in top bar).\n• Temperature spikes — Increase fan speed or reduce frequency/voltage.'
      }
    ]
  },

  // ── Swarm ─────────────────────────────────────────────────────────────────
  swarm: {
    title: 'Swarm — Multi-device Management',
    icon: 'pi pi-sitemap',
    sections: [
      {
        heading: '🌐 What is Swarm?',
        body: 'Swarm lets you discover and manage multiple NMAxe miners on your local network from a single interface. All devices must be on the same subnet.'
      },
      {
        heading: '⚡ Apply All',
        body: '"Apply All" pushes the same Settings (frequency, voltage, pool, etc.) to every discovered device simultaneously. Use with caution — it overwrites each device\'s individual configuration.'
      },
      {
        heading: '⚠ Notes',
        body: '• Device discovery uses mDNS — make sure your router allows mDNS/Bonjour traffic.\n• Apply All reboots all devices; wait ~60 seconds before checking results.\n• Benchmark data is device-specific and is NOT synced via Swarm.'
      }
    ]
  },

  // ── Update ────────────────────────────────────────────────────────────────
  update: {
    title: 'Firmware Update (OTA)',
    icon: 'pi pi-cloud-upload',
    sections: [
      {
        heading: '🚀 Over-the-Air update',
        body: 'Upload a new firmware binary (.bin) or SPIFFS filesystem image (.bin) to update the device without a USB cable. The device verifies the image before flashing and rolls back automatically if booting fails.'
      },
      {
        heading: '📋 Update order',
        body: '1. Flash SPIFFS first (web interface files)\n2. Flash firmware second (application code)\n3. The device reboots after each upload — wait for the web interface to come back before proceeding.'
      },
      {
        heading: '⚠ Notes',
        body: '• Do not power off the device during an update — this can corrupt flash and require USB recovery.\n• If the web interface does not load after an update, connect via USB and re-flash SPIFFS.\n• Always download a backup of your Benchmark results before updating firmware.'
      }
    ]
  },

  // ── Benchmark Results ─────────────────────────────────────────────────────
  'benchmark-results': {
    title: 'Benchmark Results — How to Read',
    icon: 'pi pi-table',
    sections: [
      {
        heading: '📋 What each column means',
        body: '• Time — When this data point was recorded (date + hour:minute)\n• Freq (MHz) — The ASIC clock frequency that was tested at this row\n• Vcore (mV) — The core voltage applied during this test point\n• Exp HR (GH/s) — Expected (theoretical) hashrate at this frequency based on chip spec\n• Avg HR (GH/s) — The actual measured average hashrate over the entire Benchmark Time window\n• ASIC Temp (°C) — Average ASIC chip temperature during the sampling window\n• VRM Temp (°C) — Average VRM (voltage regulator) temperature during the sampling window\n• Eff (J/TH) — Energy efficiency in Joules per Terahash — lower = better\n• Avg Pwr (W) — Average power consumption of the whole miner at this operating point\n• Action — Apply this row\'s Freq + Vcore to the miner as its permanent settings'
      },
      {
        heading: '🏆 Row highlighting',
        body: '• Green row — Best efficiency point (lowest J/TH). This is usually the sweet spot for running 24/7 with minimum electricity cost.\n• Blue row — Highest hashrate point (highest Avg HR). Choose this if you want maximum output regardless of power draw.\n• Gold row — The same operating point wins both best efficiency AND best hashrate simultaneously (rare).'
      },
      {
        heading: '⚡ Efficiency (J/TH) explained',
        body: 'J/TH = Watts ÷ (Hashrate in TH/s). It tells you how many Joules of energy are spent to compute one Terahash.\n\nExample: 8 W ÷ 0.8 TH/s = 10 J/TH.\n\nLower J/TH means the chip does more hashing per watt — better for profitability. Typically, lowering Vcore at the same frequency improves efficiency until the chip becomes unstable and starts producing invalid shares.'
      },
      {
        heading: '📊 Avg HR vs Exp HR',
        body: 'The gap between Avg HR and Exp HR shows how well the chip performs at a given operating point.\n\n• Small gap (< 5%) — Chip is stable; all cores are hashing correctly.\n• Large gap (> 10%) — Some ASIC cores may be failing or rejecting shares at this Freq/Vcore combination. Try raising Vcore slightly or lowering Freq.\n• Avg HR > Exp HR — Rare; can happen due to variance in pool-side share counting.'
      },
      {
        heading: '🎯 How to pick your operating point',
        body: 'This miner operates in solo mode — there is no guaranteed daily income. Each block found is a luck event. The goal is to maximise your hashrate or minimise power consumption, not to predict earnings.\n\n1. Green row (best efficiency) — Lowest J/TH. Run here to minimise electricity costs while keeping hashrate reasonable. Good for long-term 24/7 operation.\n2. Blue row (best hashrate) — Highest Avg HR. More hash attempts per second = statistically higher chance of finding the next block.\n3. If temperature is a concern, find a row with acceptable ASIC Temp that still gives good hashrate.\n4. Once decided, click "Apply" on that row — it saves Freq and Vcore to your Settings and reboots the miner.'
      },
      {
        heading: '💾 Data management',
        body: '• Results are stored in device flash (NVS) and survive power cycles.\n• "Download JSON" exports all results as a text file for offline analysis or backup.\n• "Clear Results" permanently deletes all stored data — download first!\n• Results survive a factory reset (10-second long-press) but NOT a "Reset Benchmark" in the Benchmark card.'
      }
    ]
  },

  // ── Settings — Time ───────────────────────────────────────────────────────
  'settings-time': {
    title: 'Settings — Time',
    icon: 'pi pi-clock',
    sections: [
      {
        heading: '🕐 Timezone Offset',
        body: 'Enter your UTC offset as a plain integer.\n\nExamples:\n• UTC+8 (China, Singapore, Perth) → enter 8\n• UTC+0 (UK, Portugal) → enter 0\n• UTC-5 (US Eastern) → enter -5\n• UTC+5:30 (India) → enter 5 (half-hours are not supported)\n\nThe device normally detects timezone automatically from your network. This manual offset is only used as a fallback when automatic detection fails — for example if your router blocks NTP or geolocation requests.'
      },
      {
        heading: '🕑 Time Format',
        body: '• 12-hour — Displays time as 01:30 PM / 11:45 AM (AM/PM style)\n• 24-hour — Displays time as 13:30 / 23:45 (military / European style)\n\nThis affects the clock shown on the miner\'s physical display and the timestamp column in Benchmark results.'
      },
      {
        heading: '📅 Date Format',
        body: 'Controls how dates are shown on-screen:\n\n• MM/DD/YYYY — US style (e.g. 05/21/2026)\n• DD/MM/YYYY — European style (e.g. 21/05/2026)\n• YYYY-MM-DD — ISO 8601 international standard (e.g. 2026-05-21)\n\nChoose whichever matches your regional convention.'
      },
      {
        heading: '⚠ Notes',
        body: '• You must click Save and then restart the device for time changes to take effect.\n• The device syncs time from the internet via NTP (Network Time Protocol) automatically on boot — if the time shown is wrong, check your network connection and timezone offset.\n• Time is not battery-backed; the clock resets to epoch on power loss and re-syncs after WiFi connects.'
      }
    ]
  },

  // ── Settings — Network ────────────────────────────────────────────────────
  'settings-network': {
    title: 'Settings — Network',
    icon: 'pi pi-wifi',
    sections: [
      {
        heading: '🏷 HostName',
        body: 'This is the device\'s network name. The default name is auto-generated from the device model and a short unique code (e.g. NMAxe_AB123).\n\nYou can change it to any name you like (up to 20 characters). The main use of this name is to identify the device in the Swarm list when you manage multiple miners — a descriptive name like "Desk-01" or "Axe-Left" makes it much easier to tell them apart.\n\nNote: this name is also used as the hotspot (AP) name if the device cannot connect to your WiFi — see below.'
      },
      {
        heading: '📶 WiFi SSID & Password',
        body: 'Enter the name (SSID) and password of your 2.4 GHz WiFi network.\n\nThe ESP32 chip only supports 2.4 GHz bands — it cannot connect to 5 GHz networks. If your router broadcasts both bands under the same name, the device will automatically pick the 2.4 GHz band.\n\nAfter saving, the device must be restarted for the new WiFi settings to take effect.'
      },
      {
        heading: '📡 Hotspot fallback (AP mode)',
        body: 'If the device cannot connect to the configured WiFi network after about 15 seconds, it automatically switches to AP (hotspot) mode so you can reconfigure it:\n\n• Hotspot name: same as the HostName set above\n• Hotspot password: 12345678\n• Connect your phone or laptop to this hotspot, then open http://192.168.4.1 in a browser to access the settings page.'
      },
      {
        heading: '📌 Static / Fixed IP',
        body: 'The device always uses DHCP to get its IP address — there is no option to configure a static IP directly on the device.\n\nIf you need a fixed IP address (recommended for multi-miner setups or remote access), configure MAC address binding (also called "IP reservation" or "DHCP static lease") on your router. Look up the device MAC address in your router\'s connected-devices list, then bind it to a fixed IP there.'
      },
      {
        heading: '⚠ Notes',
        body: '• Changes require a restart to take effect — click Save, then restart the device.\n• The HostName cannot contain spaces or special characters; stick to letters, numbers, and hyphens.'
      }
    ]
  },

  // ── Settings — Mining ─────────────────────────────────────────────────────
  'settings-mining': {
    title: 'Settings — Mining',
    icon: 'pi pi-cog',
    sections: [
      {
        heading: '🥇 Primary Pool',
        body: 'The pool the miner connects to on startup. Three fields:\n\n• Stratum USER — Your wallet address (for solo/p2pool) or pool account username. This is where any block reward would be sent.\n• Stratum URL — Connection address in the format: stratum+tcp://pool.host.com:3333\n• Stratum PWD — Pool password. Enter "x" if unsure; some pools use this field to pass a worker name.'
      },
      {
        heading: '🥈 Backup Pool',
        body: 'Same three fields as Primary Pool. If the device cannot reach the primary pool after ~30 seconds, it automatically switches to the backup pool and continues mining there.'
      },
      {
        heading: '⚡ OverClock (MHz)',
        body: 'The ASIC chip clock frequency. Higher frequency = higher hashrate, but also higher chip temperature and higher power draw. Select from the available presets in the dropdown.\n\nUse the Benchmark page to find the optimal frequency for your specific chip — results vary between individual chips even of the same model.'
      },
      {
        heading: '🔋 Vcore (mV)',
        body: 'Core voltage supplied to the ASIC. Lower voltage = less heat and power consumption; too low and the chip becomes unstable and produces invalid hashes.\n\nSelect from the available presets. Use the Benchmark page to find the lowest stable voltage at your chosen frequency.'
      },
      {
        heading: '⚠ Notes',
        body: '• All changes require Save and a device restart to take effect.\n• Do NOT change these settings while a Benchmark is running — it may corrupt sweep results.\n• After changing frequency or voltage, allow ~60 seconds for hashrate to stabilise before judging the result.'
      }
    ]
  },

  // ── Settings — Market ─────────────────────────────────────────────────────
  'settings-market': {
    title: 'Settings — Market',
    icon: 'pi pi-chart-line',
    sections: [
      {
        heading: '💰 Main Price',
        body: 'The cryptocurrency whose live price is shown on the main display screen of your miner. Only one coin can be selected as the main price.\n\nThe price is fetched from the internet and refreshed periodically. If the device has no internet connection, the display will show the last known price.'
      },
      {
        heading: '👀 Watchlist',
        body: 'A list of coins whose prices continuously cycle on the price wall / ticker screen of the display. You can select up to 50 trading pairs.\n\nThe price wall scrolls through all selected pairs in order, updating live. This is useful for tracking multiple assets simultaneously on the physical display.'
      },
      {
        heading: '🔍 Finding coins',
        body: 'Both dropdowns have a search/filter field — type the coin symbol (e.g. BTC, ETH, SOL) or name to quickly filter the list. The list is sourced from major exchange data.'
      },
      {
        heading: '⚠ Notes',
        body: '• Price data requires an active internet connection.\n• Prices are for display purposes only and have no effect on mining operation.\n• Click Save after making changes.'
      }
    ]
  },

  // ── Settings — Preference ─────────────────────────────────────────────────
  'settings-preference': {
    title: 'Settings — Preference',
    icon: 'pi pi-sliders-v',
    sections: [
      {
        heading: '� Screen Brightness',
        body: 'Controls the backlight intensity of the physical display, from 1% to 100%. Lower brightness reduces heat output from the display and is easier on the eyes in dark environments.'
      },
      {
        heading: '🌀 ASIC Fan Control',
        body: '• Auto ASIC Fan (checked) — The firmware automatically adjusts fan speed to maintain a Target ASIC temperature. Set the target temperature using the slider that appears. Recommended for daily operation.\n• Auto ASIC Fan (unchecked) — Manual mode. Set a fixed ASIC fan duty cycle (0–100%) using the slider. 100% = maximum airflow.'
      },
      {
        heading: '🌀 Vcore Fan Control',
        body: 'Only visible on NMQAxe++ (dual-fan models). Works the same as ASIC Fan Control but for the VRM (voltage regulator) cooling fan:\n• Checked — Auto mode, maintains a target Vcore temperature.\n• Unchecked — Manual mode, fixed fan duty cycle.'
      },
      {
        heading: '🌙 Screen Saver',
        body: 'Sets how long the display stays idle before the screen saver activates. Select 0 to disable (always on).\n\nWhen a non-zero timeout is selected, a second dropdown appears to choose the screen saver style/mode.'
      },
      {
        heading: '☑ Other Options',
        body: '• Flip Screen — Rotates the display 180°. Useful if the device is mounted upside-down.\n• LED Indicator — Enables or disables the status LED (not available on NMQAxe++ models).\n• Auto Scroll — The display automatically cycles through different info screens instead of staying on one page.'
      },
      {
        heading: '⚠ Notes',
        body: '• All preference changes require Save and a device restart to take effect.'
      }
    ]
  },

  // ── Settings — Theme ─────────────────────────────────────────────────────
  'settings-theme': {
    title: 'Settings — Theme',
    icon: 'pi pi-palette',
    sections: [
      {
        heading: '☀️ Color Scheme',
        body: 'Switch between Dark and Light mode for the AxeOS web interface. Select the radio button for your preferred mode — the change takes effect immediately in the browser.'
      },
      {
        heading: '🎨 Theme Colors',
        body: 'A set of named color presets (e.g. Red, Blue, Green…). Each preset changes the primary accent color used for buttons, highlights, sliders, and other interactive elements across the entire interface.\n\nClick a color dot to apply it immediately — no save button needed. The selection is stored in your browser and takes effect right away.'
      },
      {
        heading: '⚠ Notes',
        body: '• Theme settings are saved in your browser (localStorage) and are not synced across different browsers or devices.\n• Switching to a private/incognito window will reset to the default theme.\n• If the interface looks broken after switching, do a hard refresh (Ctrl+Shift+R / Cmd+Shift+R) to clear cached styles.'
      }
    ]
  },

  // ── Lucky Statistics ───────────────────────────────────────────────────────
  'lucky-stats': {
    title: 'Lucky Statistics',
    icon: 'pi pi-chart-line',
    sections: [
      {
        heading: '🎰 Why this chart exists',
        body: 'Solo mining is a lonely journey. Unlike pool mining there is no guaranteed daily income — your miner competes with the entire Bitcoin network to find a block entirely on its own. Weeks or even months can pass without a single block, and that\'s statistically normal.\n\nThis chart exists to make that wait a little more alive. It records every share difficulty your miner has achieved since it powered on — think of each bar as a lottery ticket. The higher the bar, the luckier that moment was.'
      },
      {
        heading: '📊 How to read the chart',
        body: '• Each data point represents one share submission and its difficulty value.\n• The Y-axis uses a logarithmic scale because share difficulties span a huge range.\n• The horizontal Network Difficulty line is the threshold your share must clear to solve a block. It moves with the Bitcoin network\'s global difficulty.\n• The top 3 highest-difficulty shares are specially marked and always retained in the chart, even as older records rotate out.'
      },
      {
        heading: '⚡ Flashing bar — possible block!',
        body: 'When a share\'s difficulty exceeds the Network Difficulty line, that bar flashes on screen to alert you that this share may have solved a block.\n\nIMPORTANT: this is only a possibility. A block solve must be accepted and confirmed by the rest of the decentralised network — other nodes must validate and propagate it. Until confirmation arrives, treat it as a candidate, not a guaranteed win.'
      },
      {
        heading: '📌 Record samples',
        body: 'The "N record samples" badge shows how many share events are stored in the device\'s flash memory. Records are saved in NVS and survive power cycles. When storage fills up, the oldest records rotate out automatically — but the top 3 all-time highs are always kept.'
      },
      {
        heading: '🔔 The one rule that matters',
        body: 'No matter how impressive a bar looks — no matter how high the share difficulty climbs — if it does not cross the Network Difficulty line, it is not a block solve. There is no partial credit in Bitcoin mining. One threshold, one rule.'
      }
    ],
    callout: 'Be a Friend of Time'
  }

};

export interface HelpSection {
  heading?: string;
  body: string;
}

export interface HelpEntry {
  title: string;
  icon: string;
  sections: HelpSection[];
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
        body: '• Accepted — Valid shares acknowledged by the pool (count toward your earnings)\n• Rejected — Shares the pool refused (stale, invalid difficulty, etc.)\n• Difficulty — The share difficulty your pool has assigned; higher = fewer but larger shares'
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
        body: '1. For 24/7 mining, the Green (best efficiency) row gives the lowest electricity cost per coin earned.\n2. For max earnings when electricity is cheap or free, pick the Blue (best hashrate) row.\n3. If temperature is a concern, look for a row with acceptable ASIC Temp while still having good Avg HR.\n4. Once decided, click "Apply" on that row — it saves Freq and Vcore to your Settings and reboots the miner.'
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
        heading: '⛏ ASIC Frequency (MHz)',
        body: 'The clock speed of the mining chip. Higher frequency = more hashrate, but also:\n• Higher power consumption (roughly linear)\n• Higher chip temperature\n• More voltage required for stability\n\nStart with the stock frequency and use Benchmark to find the optimal point for your chip. Typical range for BM1370-series: 400–600 MHz.'
      },
      {
        heading: '⚡ Core Voltage (Vcore, mV)',
        body: 'Voltage supplied to the ASIC cores. Lower voltage = less heat and power, but too low causes the chip to fail and produce invalid hashes (0% efficiency, low actual hashrate vs. expected).\n\nRule of thumb: lower frequency → lower minimum stable voltage. Use Benchmark to find the lowest stable voltage for your target frequency. Typical range: 1050–1250 mV.'
      },
      {
        heading: '🌀 Fan Speed (%)',
        body: '• Manual — Sets a fixed fan duty cycle (0–100%). Use 100% for maximum cooling during Benchmark or in hot environments.\n• Auto — The firmware adjusts fan speed to maintain a target ASIC temperature. Recommended for daily use.\n\nSilence-vs-cooling is a trade-off: lower fan speed is quieter but may cause thermal throttling at high frequencies.'
      },
      {
        heading: '🔗 Pool Settings (Primary & Backup)',
        body: 'Each pool entry requires:\n• Stratum URL — format: stratum+tcp://pool.host.com:3333\n• Wallet / Username — your wallet address or pool account name\n• Password / Worker — enter "x" if unsure; some pools use this field for worker naming (e.g. "worker1")\n\nThe device connects to the Primary pool first. If it cannot reach it within ~30 seconds it automatically fails over to the Backup pool.'
      },
      {
        heading: '🏷 Device Name',
        body: 'A human-readable label for this miner. Shown on the physical display and in the Swarm view when managing multiple devices. Use a short, unique name if you have several miners (e.g. "Axe-01", "Axe-02").'
      },
      {
        heading: '⚠ Notes',
        body: '• All mining settings require a Save + Reboot to take effect.\n• Do NOT change settings while a Benchmark is running — it can corrupt sweep results.\n• ASIC Boost is enabled by default and improves efficiency ~20% — only disable if your pool explicitly rejects boosted shares.\n• After a frequency or voltage change, wait ~60 seconds for hashrate to stabilise before judging performance.'
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
        body: '• Price data requires an active internet connection.\n• Prices are for informational display only and do not affect mining operation.\n• Selecting many watchlist coins with fast cycle speed may make individual prices hard to read — 5–15 coins is a practical maximum for readability.\n• Click Save after making changes.'
      }
    ]
  },

  // ── Settings — Preference ─────────────────────────────────────────────────
  'settings-preference': {
    title: 'Settings — Preference',
    icon: 'pi pi-sliders-v',
    sections: [
      {
        heading: '🖥 Display Settings',
        body: 'Controls what is shown on the miner\'s physical screen:\n• Screen timeout — How many minutes of inactivity before the screen dims or turns off (0 = always on)\n• Brightness — Backlight intensity (0–100%). Lower brightness saves a small amount of power and extends display lifespan.\n• Screen saver — Enables a moving animation when the screen has been idle for the timeout period'
      },
      {
        heading: '🔔 Notifications',
        body: 'Visual or audio alerts shown on the physical display:\n• Block found alert — Flashes a special animation when a block hit is detected\n• Share accepted pulse — Brief visual confirmation when the pool accepts a share\n\nThese are purely cosmetic and do not affect mining performance.'
      },
      {
        heading: '📊 Display Layout',
        body: 'Choose which statistics are shown on the home screen of the physical display and in what order. Hiding unused metrics reduces visual clutter.\n\nCommon options: Hashrate, Temperature, Pool Latency, Best Difficulty, Uptime, Power.'
      },
      {
        heading: '⚠ Notes',
        body: '• Preference changes take effect immediately without a reboot.\n• Very low brightness on some display models may cause colour banding — if the display looks odd, increase brightness to at least 20%.'
      }
    ]
  },

  // ── Settings — Theme ─────────────────────────────────────────────────────
  'settings-theme': {
    title: 'Settings — Theme',
    icon: 'pi pi-palette',
    sections: [
      {
        heading: '🎨 Color Theme',
        body: 'Changes the color scheme of the AxeOS web interface. The selected theme is stored in your browser (localStorage) — switching browsers or using a private/incognito window will reset to the default.\n\nAll themes are dark-mode by design to reduce eye strain during overnight monitoring sessions.'
      },
      {
        heading: '🖌 Accent Color',
        body: 'Some themes support a custom accent color for buttons, highlights, and interactive elements. Click a color swatch to preview it before saving.'
      },
      {
        heading: '🔡 Font Size',
        body: 'Adjust the base font size of the web interface. "Default" is optimised for desktop monitors. "Large" is useful on tablets or if you prefer bigger text. Changes apply instantly without a page reload.'
      },
      {
        heading: '⚠ Notes',
        body: '• Theme settings are per-browser and are NOT synced across devices.\n• If the page looks broken after a theme change, try a hard refresh (Ctrl+Shift+R / Cmd+Shift+R) to clear cached CSS.'
      }
    ]
  },

  // ── Lucky Statistics ───────────────────────────────────────────────────────
  'lucky-stats': {
    title: 'Lucky Statistics — How to Read',
    icon: 'pi pi-chart-line',
    sections: [
      {
        heading: '🍀 What is "Luck"?',
        body: 'Luck measures how often your miner finds valid shares compared to the statistical expectation based on its hashrate.\n\n• 100% luck — You\'re finding shares at exactly the rate math predicts.\n• > 100% — You\'re finding shares faster than expected (running hot 🔥).\n• < 100% — You\'re finding shares slower than expected (cold streak ❄️).\n\nLuck is random by nature — like rolling dice. Short-term swings are completely normal. It converges toward 100% over thousands of shares.'
      },
      {
        heading: '📊 The chart explained',
        body: 'The chart plots two lines on a logarithmic scale over time:\n\n• Share Difficulty (your line) — The difficulty value of each share your miner submitted to the pool. Higher = harder share found.\n• Network Difficulty (reference line) — The current Bitcoin network mining difficulty. A share that reaches this level would be a valid block solve (extremely rare).\n\nThe Y-axis uses a log scale because difficulties span many orders of magnitude — this lets you see both small pool-difficulty shares and large near-block events on the same chart.'
      },
      {
        heading: '📅 Record samples',
        body: 'The "N record samples" badge in the top-right shows how many data points are stored in the device\'s flash memory for this chart. Each record is one share submission event.\n\n• More records = longer history visible in the chart.\n• Records are stored in NVS (non-volatile storage) and survive reboots and power loss.\n• Very old records are automatically rotated out when storage is full.'
      },
      {
        heading: '📉 Gaps in the chart',
        body: 'Gaps in the share difficulty line indicate periods when no shares were submitted — usually because:\n• The miner was offline or rebooting\n• The pool connection was interrupted\n• No shares happened to be found in that interval\n\nGaps are filled visually to maintain line continuity, but no actual data is fabricated.'
      },
      {
        heading: '🔍 Practical tips',
        body: '• A consistently low share difficulty with luck < 50% over many hours may indicate the pool has assigned you a difficulty that\'s too high for your hashrate — contact your pool to lower it.\n• Occasional very high share difficulty spikes are normal — these are "high-luck" shares.\n• If the share difficulty line flatlines at zero for extended periods, check your pool connection and miner uptime in the Home dashboard.'
      }
    ]
  }

};

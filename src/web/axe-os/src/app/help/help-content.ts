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
  }

};

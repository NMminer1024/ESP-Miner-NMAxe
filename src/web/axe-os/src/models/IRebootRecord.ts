// Backend record format produced by /api/reboot/list and /api/reboot/last.
// Field names match http_server.cpp::reboot_record_to_json().
export interface IRebootRecord {
  idx: number;            // monotonic boot index (of the boot that just ended)
  ts: number;             // unix epoch in seconds, 0 if NTP wasn't synced yet
  uptime: number;         // how long the previous run lasted (seconds)
  // heapMin is the total minimum free heap. Because the 8 MB PSRAM joins the
  // ESP32 heap, this reads as a PSRAM-scale number and says nothing about the
  // scarce on-chip SRAM. Prefer heapMinPsram / heapMinInternal when present.
  heapMin: number;
  heapMinInternal?: number;  // min free on-chip SRAM (bytes); 0/absent = not recorded
  heapMinPsram?: number;     // min free PSRAM (bytes); 0/absent = not recorded
  reset: string;          // esp_reset_reason as short word (e.g. "panic", "sw")
  intent: string;         // RebootIntent as short word (e.g. "user_web_reboot")
  class: string;          // "cold" | "planned" | "unknown_sw" | "crash" | "power" | "external"
  fw: string;             // firmware version of the previous run
  detail: string;         // human-readable detail string
}

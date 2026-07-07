// Backend record format produced by /api/reboot/list and /api/reboot/last.
// Field names match http_server.cpp::reboot_record_to_json().
export interface IRebootRecord {
  idx: number;            // monotonic boot index (of the boot that just ended)
  ts: number;             // unix epoch in seconds, 0 if NTP wasn't synced yet
  uptime: number;         // how long the previous run lasted (seconds)
  heapMin: number;        // minimum free heap during the previous run (bytes)
  reset: string;          // esp_reset_reason as short word (e.g. "panic", "sw")
  intent: string;         // RebootIntent as short word (e.g. "user_web_reboot")
  class: string;          // "cold" | "planned" | "unknown_sw" | "crash" | "power" | "external"
  fw: string;             // firmware version of the previous run
  detail: string;         // human-readable detail string
}

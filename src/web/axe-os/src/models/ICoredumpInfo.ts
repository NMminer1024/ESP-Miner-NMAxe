// Coredump info JSON returned by GET /api/coredump/info.
//
// The response is split into two groups that must never be mixed up:
//   crash* / task / pc / bt  -> the image that CRASHED (from the dump itself)
//   running*                 -> the image running right now (context only)
// When `present` is false, every other field is undefined and the UI should
// render an empty / "no crash dump" state.
export interface ICoredumpInfo {
  present:      boolean;
  size?:        number;          // bytes in flash (header + ELF)
  crcOk?:       boolean;         // false = dump may be truncated (e.g. TWDT mid-write)

  // ── Crash time (authoritative) ───────────────────────────────────────────
  task?:        string;          // crashing task name (≤15 chars)
  pc?:          number;          // program counter at fault
  bt?:          number[];        // backtrace PCs (truncated to 16)
  btCorrupted?: boolean;         // hardware reported BT may be partial
  appSha256?:   string;          // full 64-hex SHA of the crashing image (from the dump)

  crashAppSha256?:        string;  // 16-hex SHA of the crashing image, comparable to runningAppSha256
  crashFwVersion?:        string;  // version of the crashing image ('' = not recoverable)
  crashUptimeSec?:        number;  // how long the crashing run lasted
  crashMinFreeHeap?:      number;  // min free internal heap during that run (bytes)
  crashEpoch?:            number;  // unix time the crash was detected
  crashIdentityKnown?:    boolean; // false = only the SHA is known, version is not

  // ── Running image (context only) ─────────────────────────────────────────
  runningFwVersion?:      string;
  runningAppSha256?:      string;
  belongsToRunningImage?: boolean; // false = dump left over from a different image (e.g. before an OTA)
}

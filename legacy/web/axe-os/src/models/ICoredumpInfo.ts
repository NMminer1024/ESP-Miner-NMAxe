// Coredump info JSON returned by GET /api/coredump/info.
//
// When `present` is false, every other field is undefined and the UI should
// render an empty / "no crash dump" state.
export interface ICoredumpInfo {
  present:      boolean;
  size?:        number;          // bytes in flash (header + ELF)
  crcOk?:       boolean;         // false = dump may be truncated (e.g. TWDT mid-write)
  task?:        string;          // crashing task name (≤15 chars)
  pc?:          number;          // program counter at fault
  bt?:          number[];        // backtrace PCs (truncated to 16)
  btCorrupted?: boolean;         // hardware reported BT may be partial
  appSha256?:   string;          // SHA256 of crashing fw image (hex string)
}

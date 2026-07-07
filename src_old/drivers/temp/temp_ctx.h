#pragma once

// ============================================================================
//  TempState — shared latest temperature samples (°C)
//
//  Single-writer (the app tick thread samples temp_hal periodically) and
//  multiple readers (fan control, monitor safety, display). Replaces the
//  legacy board.status.temp.{asic,vcore} fields.
// ============================================================================
struct TempState {
    volatile float asic  = 0.0f;   // last ASIC temp sample
    volatile float vcore = 0.0f;   // last VRM/Vcore temp sample
};

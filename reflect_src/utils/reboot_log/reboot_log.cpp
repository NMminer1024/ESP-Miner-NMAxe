#include "reboot_log.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "version.h"
#include "../logger/logger.h"

// ── NVS layout (namespace "reboot") ──────────────────────────────────────────
//   "head"    u8     index of next slot to write (0..RING-1)
//   "count"   u8     valid record count (0..RING)
//   "bootidx" u32    monotonic boot index, written before every new record
//   "r0".."r9" blob  RebootRecord (128 B)
static const char* NS_REBOOT     = "reboot";
static const char* K_HEAD        = "head";
static const char* K_COUNT       = "count";
static const char* K_BOOTIDX     = "bootidx";
static const char  K_REC_PREFIX  = 'r';

// ── RTC NOINIT state (survives SW resets / panics, lost on power-down) ───────
#define RTC_STATE_MAGIC 0x49435452u  // 'RTCI'

struct __attribute__((packed)) RtcState {
    uint32_t magic;
    uint32_t boot_count;          // increments every boot once init() runs
    uint32_t last_uptime_sec;     // mirrored ~1 Hz by reboot_log_tick()
    uint32_t last_min_heap;       // mirrored ~1 Hz by reboot_log_tick()
    uint8_t  intent;              // RebootIntent currently armed
    uint8_t  intent_valid;        // 0 = no intent for this boot
    uint16_t pad;
    char     detail[REBOOT_LOG_DETAIL_LEN];
    uint32_t reserved[2];
    uint32_t crc32;
};

RTC_NOINIT_ATTR static RtcState s_rtc;

// Updated by reboot_log_init() — true once init has run and RTC is trustworthy.
static bool s_rtc_ready = false;

// ── CRC32 (IEEE 802.3, simple bytewise) ─────────────────────────────────────
static uint32_t crc32_calc(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t rtc_crc(const RtcState* s) {
    return crc32_calc((const uint8_t*)s, offsetof(RtcState, crc32));
}

static bool rtc_valid(const RtcState* s) {
    return s->magic == RTC_STATE_MAGIC && s->crc32 == rtc_crc(s);
}

static void rtc_seal() {
    s_rtc.magic = RTC_STATE_MAGIC;
    s_rtc.crc32 = rtc_crc(&s_rtc);
}

static void rtc_reset_zero() {
    memset(&s_rtc, 0, sizeof(s_rtc));
    rtc_seal();
}

// ── String tables ────────────────────────────────────────────────────────────
const char* reboot_intent_str(uint8_t v) {
    switch (v) {
        case REBOOT_INTENT_NONE:                return "none";
        case REBOOT_INTENT_USER_WEB_REBOOT:     return "user_web_reboot";
        case REBOOT_INTENT_OTA_FINISHED:        return "ota_finished";
        case REBOOT_INTENT_FACTORY_RESET:       return "factory_reset";
        case REBOOT_INTENT_WIFI_CONFIG_TIMEOUT: return "wifi_config_timeout";
        case REBOOT_INTENT_WIFI_RECONNECT_FAIL: return "wifi_reconnect_fail";
        case REBOOT_INTENT_LOW_HASHRATE:        return "low_hashrate";
        case REBOOT_INTENT_POOL_TIMEOUT:        return "pool_timeout";
        case REBOOT_INTENT_ASIC_FROZEN:         return "asic_frozen";
        case REBOOT_INTENT_CONFIG_CHANGED:      return "config_changed";
        case REBOOT_INTENT_SELFTEST_TRIGGERED:  return "selftest_triggered";
        case REBOOT_INTENT_DAEMON_GENERIC:      return "daemon_generic";
        case REBOOT_INTENT_OVERHEAT_VCORE:      return "overheat_vcore";
        case REBOOT_INTENT_OVERHEAT_ASIC:       return "overheat_asic";
        case REBOOT_INTENT_FAN_STALL:           return "fan_stall";
        case REBOOT_INTENT_POWER_LOW:           return "power_low";
        case REBOOT_INTENT_OTA_STALL:           return "ota_stall";
        case REBOOT_INTENT_LCD_USER_RESTART:    return "lcd_user_restart";
        case REBOOT_INTENT_LCD_WIFI_SAVED:      return "lcd_wifi_saved";
        case REBOOT_INTENT_FORCE_CONFIG:        return "force_config";
        case REBOOT_INTENT_OVERCURRENT_FAULT:   return "overcurrent_fault";
        default:                                return "unknown";
    }
}

const char* reboot_reset_reason_str(uint8_t v) {
    switch (v) {
        case ESP_RST_UNKNOWN:    return "unknown";
        case ESP_RST_POWERON:    return "poweron";
        case ESP_RST_EXT:        return "ext_pin";
        case ESP_RST_SW:         return "sw";
        case ESP_RST_PANIC:      return "panic";
        case ESP_RST_INT_WDT:    return "int_wdt";
        case ESP_RST_TASK_WDT:   return "task_wdt";
        case ESP_RST_WDT:        return "wdt";
        case ESP_RST_DEEPSLEEP:  return "deepsleep";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_SDIO:       return "sdio";
        default:                 return "other";
    }
}

const char* reboot_class_str(uint8_t v) {
    switch (v) {
        case REBOOT_CLASS_COLD:       return "cold";
        case REBOOT_CLASS_PLANNED:    return "planned";
        case REBOOT_CLASS_UNKNOWN_SW: return "unknown_sw";
        case REBOOT_CLASS_CRASH:      return "crash";
        case REBOOT_CLASS_POWER:      return "power";
        case REBOOT_CLASS_EXTERNAL:   return "external";
        default:                      return "other";
    }
}

// ── Classification ──────────────────────────────────────────────────────────
static RebootClass classify(esp_reset_reason_t r, RebootIntent intent, bool intent_valid) {
    switch (r) {
        case ESP_RST_POWERON:   return REBOOT_CLASS_COLD;
        case ESP_RST_BROWNOUT:  return REBOOT_CLASS_POWER;
        case ESP_RST_EXT:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_SDIO:
        case ESP_RST_UNKNOWN:   return REBOOT_CLASS_EXTERNAL;
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return REBOOT_CLASS_CRASH;
        case ESP_RST_SW:
            return (intent_valid && intent != REBOOT_INTENT_NONE)
                ? REBOOT_CLASS_PLANNED
                : REBOOT_CLASS_UNKNOWN_SW;
        default:                return REBOOT_CLASS_EXTERNAL;
    }
}

// ── NVS helpers ─────────────────────────────────────────────────────────────
static void rec_seal(RebootRecord* r) {
    r->magic = REBOOT_RECORD_MAGIC;
    r->crc32 = crc32_calc((const uint8_t*)r, offsetof(RebootRecord, crc32));
}

static bool rec_valid(const RebootRecord* r) {
    return r->magic == REBOOT_RECORD_MAGIC &&
           r->crc32 == crc32_calc((const uint8_t*)r, offsetof(RebootRecord, crc32));
}

static void rec_key(uint8_t slot, char out[8]) {
    snprintf(out, 8, "%c%u", K_REC_PREFIX, (unsigned)slot);
}

// Push a freshly built record into the ring. Returns true on success.
static bool nvs_push_record(const RebootRecord* rec) {
    nvs_handle_t h;
    if (nvs_open(NS_REBOOT, NVS_READWRITE, &h) != ESP_OK) return false;

    uint8_t  head  = 0;
    uint8_t  count = 0;
    uint32_t bidx  = 0;
    nvs_get_u8 (h, K_HEAD,    &head);
    nvs_get_u8 (h, K_COUNT,   &count);
    nvs_get_u32(h, K_BOOTIDX, &bidx);

    if (head >= REBOOT_LOG_RING_SIZE) head = 0;

    char k[8];
    rec_key(head, k);
    esp_err_t err = nvs_set_blob(h, k, rec, sizeof(*rec));
    if (err == ESP_OK) {
        head = (head + 1) % REBOOT_LOG_RING_SIZE;
        if (count < REBOOT_LOG_RING_SIZE) count++;
        nvs_set_u8 (h, K_HEAD,    head);
        nvs_set_u8 (h, K_COUNT,   count);
        nvs_set_u32(h, K_BOOTIDX, rec->boot_index);
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

// ── Init ────────────────────────────────────────────────────────────────────
void reboot_log_init() {
    // 1. Validate / initialise RTC state.
    bool first_boot = !rtc_valid(&s_rtc);
    if (first_boot) rtc_reset_zero();

    esp_reset_reason_t reason = esp_reset_reason();
    RebootIntent  intent      = (RebootIntent)s_rtc.intent;
    bool          intent_v    = !first_boot && s_rtc.intent_valid != 0;

    // Intent only meaningful on a software-initiated restart. Anything else
    // (panic / WDT / brownout / cold) overrides whatever was stamped before.
    if (reason != ESP_RST_SW) {
        intent   = REBOOT_INTENT_NONE;
        intent_v = false;
    }

    // 2. Build the record describing the run that JUST ENDED.
    RebootRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.boot_index      = s_rtc.boot_count;        // index of the previous boot
    rec.uptime_last_sec = first_boot ? 0 : s_rtc.last_uptime_sec;
    rec.free_heap_min   = first_boot ? 0 : s_rtc.last_min_heap;
    rec.reset_reason    = (uint8_t)reason;
    rec.intent          = (uint8_t)intent;
    rec.cls             = (uint8_t)classify(reason, intent, intent_v);

    // current epoch (likely 0 unless RTC kept time across SW reset; better than
    // nothing, the web layer can patch this later if desired)
    time_t now = time(nullptr);
    rec.epoch_ts = (now > 1700000000) ? (uint32_t)now : 0;

    strncpy(rec.fw_version, BOARD_CURRENT_FW_VERSION, sizeof(rec.fw_version) - 1);

    // detail: prefer the planner's text; otherwise fall back to a reset_reason word
    if (intent_v) {
        strncpy(rec.detail, s_rtc.detail, sizeof(rec.detail) - 1);
    } else {
        strncpy(rec.detail, reboot_reset_reason_str((uint8_t)reason), sizeof(rec.detail) - 1);
    }

    rec_seal(&rec);

    // 3. Persist (best-effort; failing write does not break boot).
    if (!first_boot || reason != ESP_RST_POWERON || s_rtc.boot_count > 0) {
        // Skip writing a zero-information "first ever boot" record on truly
        // fresh hardware (boot_count == 0 and POWERON). This avoids cluttering
        // the history with an entry that has uptime=0 and no fw context.
        if (!(first_boot && reason == ESP_RST_POWERON && s_rtc.boot_count == 0)) {
            nvs_push_record(&rec);
        }
    } else {
        nvs_push_record(&rec);
    }

    // 4. Prepare RTC for the upcoming run.
    s_rtc.boot_count       = rec.boot_index + 1;
    s_rtc.intent           = REBOOT_INTENT_NONE;
    s_rtc.intent_valid     = 0;
    s_rtc.detail[0]        = '\0';
    s_rtc.last_uptime_sec  = 0;
    s_rtc.last_min_heap    = 0;
    rtc_seal();

    s_rtc_ready = true;

    LOG_W("[reboot_log] prev boot #%u: reset=%s intent=%s class=%s ran=%us minHeap=%uB detail=\"%s\"",
          (unsigned)rec.boot_index,
          reboot_reset_reason_str(rec.reset_reason),
          reboot_intent_str(rec.intent),
          reboot_class_str(rec.cls),
          (unsigned)rec.uptime_last_sec,
          (unsigned)rec.free_heap_min,
          rec.detail);
}

// ── Intent setter ───────────────────────────────────────────────────────────
static void intent_set_internal(RebootIntent intent, const char* fmt, va_list ap) {
    if (!s_rtc_ready) return;  // avoid clobbering RTC before init validated it
    s_rtc.intent       = (uint8_t)intent;
    s_rtc.intent_valid = 1;
    if (fmt && fmt[0]) {
        vsnprintf(s_rtc.detail, sizeof(s_rtc.detail), fmt, ap);
    } else {
        s_rtc.detail[0] = '\0';
    }
    rtc_seal();
    LOG_I("[reboot_log] intent=%s detail=\"%s\"",
          reboot_intent_str((uint8_t)intent), s_rtc.detail);
}

void reboot_intent_set(RebootIntent intent, const char* detail_fmt, ...) {
    va_list ap; va_start(ap, detail_fmt);
    intent_set_internal(intent, detail_fmt, ap);
    va_end(ap);
}

void reboot_intent_set_if_unset(RebootIntent intent, const char* detail_fmt, ...) {
    if (!s_rtc_ready) return;
    if (s_rtc.intent_valid) return;  // someone already stamped a real reason
    va_list ap; va_start(ap, detail_fmt);
    intent_set_internal(intent, detail_fmt, ap);
    va_end(ap);
}

// ── 1 Hz mirror ─────────────────────────────────────────────────────────────
void reboot_log_tick() {
    if (!s_rtc_ready) return;
    s_rtc.last_uptime_sec = (uint32_t)(millis() / 1000UL);
    s_rtc.last_min_heap   = (uint32_t)esp_get_minimum_free_heap_size();
    rtc_seal();
}

// ── Readers ─────────────────────────────────────────────────────────────────
size_t reboot_log_get_recent(RebootRecord* out, size_t max_n) {
    if (!out || max_n == 0) return 0;

    nvs_handle_t h;
    if (nvs_open(NS_REBOOT, NVS_READONLY, &h) != ESP_OK) return 0;

    uint8_t head = 0, count = 0;
    nvs_get_u8(h, K_HEAD,  &head);
    nvs_get_u8(h, K_COUNT, &count);
    if (count > REBOOT_LOG_RING_SIZE) count = REBOOT_LOG_RING_SIZE;
    if (head  >= REBOOT_LOG_RING_SIZE) head  = 0;

    size_t n_read = 0;
    size_t want   = (max_n < count) ? max_n : count;
    // newest is at slot (head - 1) mod RING
    int slot = (int)head - 1;
    for (size_t i = 0; i < want; ++i) {
        if (slot < 0) slot += REBOOT_LOG_RING_SIZE;
        char k[8]; rec_key((uint8_t)slot, k);
        size_t sz = sizeof(RebootRecord);
        if (nvs_get_blob(h, k, &out[n_read], &sz) == ESP_OK &&
            sz == sizeof(RebootRecord) &&
            rec_valid(&out[n_read])) {
            n_read++;
        }
        slot--;
    }
    nvs_close(h);
    return n_read;
}

bool reboot_log_get_last(RebootRecord* out) {
    return reboot_log_get_recent(out, 1) == 1;
}

void reboot_log_clear() {
    nvs_handle_t h;
    if (nvs_open(NS_REBOOT, NVS_READWRITE, &h) != ESP_OK) return;
    // Wipe every record key + counters; leave bootidx alone (monotonic).
    for (uint8_t i = 0; i < REBOOT_LOG_RING_SIZE; ++i) {
        char k[8]; rec_key(i, k);
        nvs_erase_key(h, k);
    }
    nvs_set_u8(h, K_HEAD,  0);
    nvs_set_u8(h, K_COUNT, 0);
    nvs_commit(h);
    nvs_close(h);
}

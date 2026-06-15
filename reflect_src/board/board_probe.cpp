#include "board_probe.h"

namespace {
constexpr int NM_MODEL_SELECT_PIN0 = 15;
constexpr int NM_MODEL_SELECT_PIN1 = 46;
constexpr int NM_MODEL_SELECT_PIN2 = 39;

uint8_t read_raw_board_bits() {
    return static_cast<uint8_t>(
        (digitalRead(NM_MODEL_SELECT_PIN0) << 2) |
        (digitalRead(NM_MODEL_SELECT_PIN1) << 1) |
        (digitalRead(NM_MODEL_SELECT_PIN2)));
}

bool is_valid_raw(uint8_t raw) {
    return raw == 0b110 || raw == 0b010 || raw == 0b101 || raw == 0b111;
}
}  // namespace

ReflectBoardModel detect_reflect_board_model() {
    // Discharge potential external capacitance before sampling selection pins.
    pinMode(NM_MODEL_SELECT_PIN0, OUTPUT);
    digitalWrite(NM_MODEL_SELECT_PIN0, LOW);
    pinMode(NM_MODEL_SELECT_PIN1, OUTPUT);
    digitalWrite(NM_MODEL_SELECT_PIN1, LOW);
    pinMode(NM_MODEL_SELECT_PIN2, OUTPUT);
    digitalWrite(NM_MODEL_SELECT_PIN2, LOW);
    delay(20);

    pinMode(NM_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_MODEL_SELECT_PIN1, INPUT_PULLUP);
    pinMode(NM_MODEL_SELECT_PIN2, INPUT_PULLDOWN);

    constexpr uint32_t SAMPLE_INTERVAL_MS = 10;
    constexpr uint32_t STABLE_MS = 300;
    constexpr uint32_t STABLE_SAMPLES = STABLE_MS / SAMPLE_INTERVAL_MS;
    constexpr uint32_t TIMEOUT_SAMPLES = 3000 / SAMPLE_INTERVAL_MS;

    uint8_t last_raw = 0xFF;
    uint32_t stable_cnt = 0;
    uint32_t timeout_cnt = 0;

    while (true) {
        const uint8_t raw = read_raw_board_bits();
        if (raw == last_raw) {
            stable_cnt++;
        } else {
            last_raw = raw;
            stable_cnt = 1;
        }

        if (stable_cnt >= STABLE_SAMPLES && is_valid_raw(raw)) {
            break;
        }
        if (++timeout_cnt >= TIMEOUT_SAMPLES) {
            break;
        }
        delay(SAMPLE_INTERVAL_MS);
    }

    switch (last_raw) {
        case 0b110:
            return ReflectBoardModel::NMAxe;
        case 0b010:
            return ReflectBoardModel::NMAxeGamma;
        case 0b101:
            return ReflectBoardModel::NMQAxePP;
        case 0b111:
            return ReflectBoardModel::NMQAxePPRev61;
        default:
            return ReflectBoardModel::Unknown;
    }
}

const char* reflect_board_model_name(ReflectBoardModel model) {
    switch (model) {
        case ReflectBoardModel::NMAxe:
            return "NMAxe";
        case ReflectBoardModel::NMAxeGamma:
            return "NMAxeGamma";
        case ReflectBoardModel::NMQAxePP:
            return "NMQAxe++";
        case ReflectBoardModel::NMQAxePPRev61:
            return "NMQAxe++Rev6.1";
        default:
            return "Unknown";
    }
}

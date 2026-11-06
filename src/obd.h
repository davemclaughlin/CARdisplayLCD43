// OBD-II over CAN (ISO 15765-4) using the ESP32-S3 TWAI controller.
#pragma once
#include <stdint.h>

enum Sig {
    SIG_OIL_PSI,     // Mode 0x22 (manufacturer specific), psi
    SIG_COOLANT_C,   // PID 0x05
    SIG_OIL_TEMP_C,  // PID 0x5C, or a Mode 0x22 DID if the ECU lacks 0x5C
    SIG_RPM,         // PID 0x0C (not displayed; used for the oil-pressure alarm)
    SIG_FUEL_PSI,    // PID 0x0A, fuel pressure (gauge), psi
    SIG_SPEED,       // PID 0x0D, vehicle speed km/h
    SIG_STFT1,       // PID 0x06, bank 1 short term fuel trim %
    SIG_LTFT1,       // PID 0x07, bank 1 long term fuel trim %
    SIG_STFT2,       // PID 0x08, bank 2 short term fuel trim %
    SIG_LTFT2,       // PID 0x09, bank 2 long term fuel trim %
    SIG_VBATT,       // PID 0x42, control module voltage
    SIG_COUNT
};

struct SigVal {
    float v;
    uint32_t t;      // millis() of last update, 0 = never
    bool supported;
};

enum LinkState { LINK_INIT, LINK_WAITING_ECU, LINK_OK, LINK_BUS_ERROR, LINK_DEMO };

struct ObdSnapshot {
    SigVal s[SIG_COUNT];
    LinkState link;
};

// Starts the CAN driver and the polling task (pinned to core 0).
void obd_begin();

// Thread-safe copy of the latest values.
void obd_snapshot(ObdSnapshot &out);

static inline bool sig_fresh(const SigVal &s, uint32_t now, uint32_t stale_ms) {
    return s.supported && s.t != 0 && (now - s.t) < stale_ms;
}

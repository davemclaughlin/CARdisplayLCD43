// User configuration for the OBD-II gauge cluster.
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// General
// ---------------------------------------------------------------------------

// 1 = generate fake data (bench-test the UI without a car), 0 = read OBD-II.
#define DEMO_MODE 0

// Display rotation passed to Arduino_GFX: 0 = landscape, 2 = upside down.
// Touch coordinates are rotated to match.
#define DISPLAY_ROTATION 0

// A reading older than this is shown as "--".
#define STALE_MS 2000

// ---------------------------------------------------------------------------
// CAN / OBD-II  (ISO 15765-4, 11-bit IDs)
// ---------------------------------------------------------------------------

#define CAN_TX_PIN 15  // TJA1051 TXD
#define CAN_RX_PIN 16  // TJA1051 RXD
#define CAN_BITRATE_KBPS 500  // 500 for almost all cars since 2008; some use 250

// 0x7DF = functional broadcast to all emission ECUs.
// 0x7E0 = engine ECU only (less bus traffic if your car has several ECUs).
#define OBD_REQUEST_ID 0x7DF

#define OBD_RESPONSE_TIMEOUT_MS 60
#define OBD_CYCLE_DELAY_MS 10  // pause between polling cycles

// ---------------------------------------------------------------------------
// Oil pressure source
// ---------------------------------------------------------------------------
// Oil pressure is NOT part of the standard OBD-II (SAE J1979) PID set, so it
// must come from a manufacturer-specific "Mode 0x22" DID. Look up the DID and
// scaling for your vehicle (e.g. in Torque/OBDLink/ForScan PID lists) and fill
// them in below. With OIL_SRC_NONE the oil gauge shows "N/A".

#define OIL_SRC_NONE   0
#define OIL_SRC_MODE22 1

#define OIL_SOURCE OIL_SRC_MODE22

#define OIL_M22_TX_ID 0x7E0  // physical request ID of the ECU that knows oil pressure
#define OIL_M22_RX_ID 0x7E8  // its response ID
#define OIL_M22_DID   0x115C // GM LS V8 (Holden VE Commodore L98/L76) engine oil pressure

// Convert the DID's data bytes (after the echoed DID) to psi.
// GM 0x115C returns one byte A, scaled A * 4 kPa (~A * 0.578 psi).
// Forum-reported alternatives: 0.6 * (A - 27) and A * 0.65 - 17.5 psi.
// Verify against a mechanical or dash gauge and adjust if needed.
static inline float oil_m22_decode_psi(const uint8_t *d, int n) {
    if (n < 1) return -1.0f;
    return (d[0] * 0.65f) - 17.5;
}

// ---------------------------------------------------------------------------
// Engine oil temperature source
// ---------------------------------------------------------------------------
// Standard PID 0x5C is used if the ECU reports it as supported. Many older
// ECUs (likely including the VE Commodore's E38) don't, so a Mode 0x22 DID is
// tried instead. GM 0x1154 is the commonly listed GM oil temperature DID,
// scaled A - 40 C. It is unverified on the VE; if the ECM rejects it the tile
// shows "N/A".

#define OIL_TEMP_M22_ENABLE 1
#define OIL_TEMP_M22_TX_ID  0x7E0
#define OIL_TEMP_M22_RX_ID  0x7E8
#define OIL_TEMP_M22_DID    0x1154

static inline float oil_temp_m22_decode_c(const uint8_t *d, int n) {
    if (n < 1) return -999.0f;
    return d[0] - 40.0f;
}

// ---------------------------------------------------------------------------
// Gauge zones and warnings
// ---------------------------------------------------------------------------

#define OIL_RED_BELOW_PSI     10
#define OIL_AMBER_BELOW_PSI   20
#define OIL_WARN_PSI          10   // flash the gauge below this (engine running)

#define WATER_COLD_BELOW_C    60
#define WATER_AMBER_ABOVE_C  105
#define WATER_RED_ABOVE_C    112
#define WATER_WARN_C         110   // flash the gauge above this

#define OIL_TEMP_COLD_BELOW_C   60
#define OIL_TEMP_AMBER_ABOVE_C 120
#define OIL_TEMP_RED_ABOVE_C   135
#define OIL_TEMP_WARN_C        130   // flash the gauge above this

#define VBATT_LOW_WARN 12.0f      // value turns amber below this
#define VBATT_HIGH_WARN 15.0f     // ...or above this

#define FUEL_TRIM_WARN_PCT 15.0f  // value turns amber when |ST + LT| exceeds this

// Engine is considered running above this RPM (used for oil min / oil alarm).
#define RUNNING_RPM 400

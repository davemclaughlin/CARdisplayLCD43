#include <Arduino.h>
#include <driver/twai.h>
#include "obd.h"
#include "config.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ObdSnapshot s_data;

static void set_sig(Sig s, float v) {
    uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    s_data.s[s].v = v;
    s_data.s[s].t = now ? now : 1;
    portEXIT_CRITICAL(&s_mux);
}

static void set_supported(Sig s, bool supported) {
    portENTER_CRITICAL(&s_mux);
    s_data.s[s].supported = supported;
    portEXIT_CRITICAL(&s_mux);
}

static void set_link(LinkState l) {
    portENTER_CRITICAL(&s_mux);
    s_data.link = l;
    portEXIT_CRITICAL(&s_mux);
}

void obd_snapshot(ObdSnapshot &out) {
    portENTER_CRITICAL(&s_mux);
    out = s_data;
    portEXIT_CRITICAL(&s_mux);
}

#if DEMO_MODE
// ---------------------------------------------------------------------------
// Demo data generator
// ---------------------------------------------------------------------------
static void demo_task(void *) {
    for (int i = 0; i < SIG_COUNT; i++) set_supported((Sig)i, true);
    set_link(LINK_DEMO);
    for (;;) {
        float t = millis() / 1000.0f;
        float rpm = 2500 + 1700 * sinf(t * 0.35f);
        set_sig(SIG_RPM, rpm);
        set_sig(SIG_OIL_PSI, 15 + rpm / 75.0f + 3 * sinf(t * 2.1f));
        set_sig(SIG_COOLANT_C, 88 + 26 * sinf(t * 0.05f));
        set_sig(SIG_OIL_TEMP_C, 100 + 15 * sinf(t * 0.04f));
        set_sig(SIG_VBATT, 13.9f + 0.3f * sinf(t * 0.7f));
        set_sig(SIG_FUEL_PSI, 58 + 1.5f * sinf(t * 0.6f));
        set_sig(SIG_SPEED, 54 + 50 * sinf(t * 0.35f));
        set_sig(SIG_STFT1, 4 * sinf(t * 1.3f));
        set_sig(SIG_LTFT1, -2.3f);
        set_sig(SIG_STFT2, 5 * sinf(t * 1.1f + 1.0f));
        set_sig(SIG_LTFT2, 1.6f);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void obd_begin() {
    xTaskCreatePinnedToCore(demo_task, "demo", 4096, nullptr, 5, nullptr, 0);
}

#else
// ---------------------------------------------------------------------------
// CAN transport (ISO-TP single frames only, which covers every PID used here)
// ---------------------------------------------------------------------------
static uint32_t s_pid_map[4];  // supported-PID bitmaps for 0x01-0x20 .. 0x61-0x80

static bool pid_supported(uint8_t pid) {
    if (pid == 0) return true;
    uint8_t idx = (pid - 1) / 32, bit = (pid - 1) % 32;
    return idx < 4 && (s_pid_map[idx] & (1UL << (31 - bit)));
}

static bool can_begin() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    g.rx_queue_len = 32;
    g.tx_queue_len = 8;
#if CAN_BITRATE_KBPS == 250
    twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
#else
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
#endif
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    return twai_start() == ESP_OK;
}

static void can_check_bus() {
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) return;
    if (st.state == TWAI_STATE_BUS_OFF) {
        set_link(LINK_BUS_ERROR);
        twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
        twai_start();  // recovery finished
    }
}

// Sends a single-frame request and waits for the matching positive response.
// `req` is [service, params...]. Returns the number of data bytes after the
// echoed service+params, or -1 on timeout, -2 on a negative response.
static int obd_request(uint32_t tx_id, uint32_t rx_lo, uint32_t rx_hi,
                       const uint8_t *req, uint8_t req_len,
                       uint8_t *out, int out_max, uint32_t timeout_ms) {
    twai_clear_receive_queue();

    twai_message_t m = {};
    m.identifier = tx_id;
    m.data_length_code = 8;
    m.data[0] = req_len;
    for (int i = 0; i < 7; i++) m.data[1 + i] = i < req_len ? req[i] : 0x55;
    if (twai_transmit(&m, pdMS_TO_TICKS(10)) != ESP_OK) return -1;

    uint32_t start = millis();
    for (;;) {
        uint32_t elapsed = millis() - start;
        if (elapsed >= timeout_ms) return -1;

        twai_message_t r;
        if (twai_receive(&r, pdMS_TO_TICKS(timeout_ms - elapsed)) != ESP_OK) return -1;
        if (r.extd || r.rtr || r.identifier < rx_lo || r.identifier > rx_hi) continue;
        if (r.data_length_code < 2 || (r.data[0] & 0xF0) != 0) continue;  // single frames only

        uint8_t len = r.data[0] & 0x0F;
        if (len > 7) continue;
        const uint8_t *p = &r.data[1];

        if (len >= 3 && p[0] == 0x7F && p[1] == req[0]) {
            if (p[2] == 0x78) continue;  // "response pending", keep waiting
            return -2;
        }
        if (len < req_len || p[0] != (uint8_t)(req[0] + 0x40)) continue;
        if (memcmp(&p[1], &req[1], req_len - 1) != 0) continue;  // wrong PID/DID

        int n = len - req_len;
        if (n > out_max) n = out_max;
        memcpy(out, &p[req_len], n);
        return n;
    }
}

static int mode01(uint8_t pid, uint8_t *out, uint32_t timeout_ms = OBD_RESPONSE_TIMEOUT_MS) {
    const uint8_t req[2] = {0x01, pid};
    return obd_request(OBD_REQUEST_ID, 0x7E8, 0x7EF, req, 2, out, 4, timeout_ms);
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------
struct PollItem {
    Sig sig;
    uint8_t pid;
    bool fast;
};

static PollItem s_items[] = {
    {SIG_RPM,       0x0C, true},
    {SIG_SPEED,     0x0D, true},
    {SIG_STFT1,     0x06, true},
    {SIG_STFT2,     0x08, true},
    {SIG_FUEL_PSI,  0x0A, false},
    {SIG_COOLANT_C, 0x05, false},
    {SIG_OIL_TEMP_C, 0x5C, false},
    {SIG_VBATT,     0x42, false},
    {SIG_LTFT1,     0x07, false},
    {SIG_LTFT2,     0x09, false},
};

static float decode(uint8_t pid, const uint8_t *d, int n) {
    uint16_t ab = n >= 2 ? (d[0] << 8) | d[1] : 0;
    switch (pid) {
        case 0x05: case 0x5C: return d[0] - 40.0f;
        case 0x06: case 0x07: case 0x08: case 0x09: return d[0] * (100.0f / 128.0f) - 100.0f;
        case 0x0D: return d[0];  // km/h
        case 0x0A: return d[0] * 3.0f * 0.145038f;  // 3 kPa per bit -> psi
        case 0x0C: return ab / 4.0f;
        case 0x42: return ab / 1000.0f;
    }
    return 0;
}

static int min_bytes(uint8_t pid) {
    switch (pid) {
        case 0x0C: case 0x42: return 2;
    }
    return 1;
}

static bool poll_item(const PollItem &it) {
    if (!it.pid || !pid_supported(it.pid)) return false;
    uint8_t d[4];
    int n = mode01(it.pid, d);
    if (n < min_bytes(it.pid)) return false;
    set_sig(it.sig, decode(it.pid, d, n));
    return true;
}

// Manufacturer-specific Mode 0x22 readings.
struct M22Item {
    Sig sig;
    uint32_t tx_id, rx_id;
    uint16_t did;
    float (*decode)(const uint8_t *d, int n);
    float min_valid;
    const char *name;
    bool enabled;    // configured, and needed
    uint8_t nrc;     // negative responses received
};

static M22Item s_oil_press = {SIG_OIL_PSI, OIL_M22_TX_ID, OIL_M22_RX_ID, OIL_M22_DID,
                              oil_m22_decode_psi, 0.0f, "oil pressure",
                              OIL_SOURCE == OIL_SRC_MODE22, 0};
static M22Item s_oil_temp = {SIG_OIL_TEMP_C, OIL_TEMP_M22_TX_ID, OIL_TEMP_M22_RX_ID, OIL_TEMP_M22_DID,
                             oil_temp_m22_decode_c, -60.0f, "oil temperature",
                             OIL_TEMP_M22_ENABLE != 0, 0};

static bool m22_active(const M22Item &m) { return m.enabled && m.nrc < 3; }

static bool poll_m22(M22Item &m) {
    if (!m22_active(m)) return false;
    const uint8_t req[3] = {0x22, (uint8_t)(m.did >> 8), (uint8_t)(m.did & 0xFF)};
    uint8_t d[4];
    int n = obd_request(m.tx_id, m.rx_id, m.rx_id, req, 3, d, 4, OBD_RESPONSE_TIMEOUT_MS);
    if (n == -2 && ++m.nrc >= 3) {
        Serial.printf("[obd] %s DID 0x%04X rejected by ECU, disabling\n", m.name, m.did);
        set_supported(m.sig, false);
    }
    if (n <= 0) return false;
    float v = m.decode(d, n);
    if (v < m.min_valid) return false;
    set_sig(m.sig, v);
    return true;
}

// Waits for the ECU and reads the supported-PID bitmaps.
static bool discover() {
    memset(s_pid_map, 0, sizeof(s_pid_map));
    uint8_t d[4];
    for (uint8_t base = 0x00; base <= 0x60; base += 0x20) {
        if (base && !pid_supported(base)) break;
        if (mode01(base, d, 250) < 4) {
            if (base == 0) return false;
            break;
        }
        s_pid_map[base / 0x20] = ((uint32_t)d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
    }
    Serial.printf("[obd] supported PIDs: %08lX %08lX %08lX %08lX\n",
                  (unsigned long)s_pid_map[0], (unsigned long)s_pid_map[1],
                  (unsigned long)s_pid_map[2], (unsigned long)s_pid_map[3]);

    for (const PollItem &it : s_items) set_supported(it.sig, it.pid && pid_supported(it.pid));

    // Use the oil temperature DID only when standard PID 0x5C is missing.
    s_oil_temp.enabled = OIL_TEMP_M22_ENABLE && !pid_supported(0x5C);
    set_supported(SIG_OIL_PSI, m22_active(s_oil_press));
    if (m22_active(s_oil_temp)) set_supported(SIG_OIL_TEMP_C, true);
    return true;
}

static void obd_task(void *) {
    if (!can_begin()) {
        Serial.println("[obd] TWAI driver start failed");
        set_link(LINK_BUS_ERROR);
        vTaskDelete(nullptr);
    }
    Serial.printf("[obd] CAN started at %d kbit/s\n", CAN_BITRATE_KBPS);

    const size_t n_items = sizeof(s_items) / sizeof(s_items[0]);
    size_t slow_idx = 0;
    uint32_t cycle = 0;

    for (;;) {
        set_link(LINK_WAITING_ECU);
        can_check_bus();
        if (!discover()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        set_link(LINK_OK);
        Serial.println("[obd] ECU connected");

        uint32_t last_ok = millis();
        while (millis() - last_ok < 3000) {
            can_check_bus();

            if (poll_m22(s_oil_press)) last_ok = millis();
            if (cycle++ % 20 == 0 && poll_m22(s_oil_temp)) last_ok = millis();
            for (size_t i = 0; i < n_items; i++)
                if (s_items[i].fast && poll_item(s_items[i])) last_ok = millis();

            // One slow item per cycle.
            for (size_t tries = 0; tries < n_items; tries++) {
                const PollItem &it = s_items[slow_idx++ % n_items];
                if (it.fast || !it.pid || !pid_supported(it.pid)) continue;
                if (poll_item(it)) last_ok = millis();
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(OBD_CYCLE_DELAY_MS));
        }
        Serial.println("[obd] ECU stopped responding");
    }
}

void obd_begin() {
    s_data.link = LINK_INIT;
    xTaskCreatePinnedToCore(obd_task, "obd", 4096, nullptr, 5, nullptr, 0);
}
#endif

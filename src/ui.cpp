// 800x480 dashboard:
//   top    : three round gauges (oil pressure, coolant temp, oil temp)
//   bottom : battery voltage, fuel pressure, vehicle speed, bank 1 / bank 2 fuel trims
// Long-press anywhere to reset the min / max markers.
#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"
#include "config.h"

#define COL_BG      0x000000
#define COL_PANEL   0x111418
#define COL_BORDER  0x2A2F36
#define COL_TEXT    0xF2F2F2
#define COL_DIM     0x8A9099
#define COL_STALE   0x4A4F57
#define COL_NEEDLE  0xFF6A00
#define COL_GREEN   0x2ECC71
#define COL_AMBER   0xFFB300
#define COL_RED     0xFF3030
#define COL_BLUE    0x3D8BFF

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

// Only touch LVGL when something changed, so unchanged areas are not redrawn.
static void set_text(lv_obj_t *label, const char *txt) {
    if (strcmp(lv_label_get_text(label), txt) != 0) lv_label_set_text(label, txt);
}

static void set_text_color(lv_obj_t *obj, uint32_t c) {
    uintptr_t tag = (uintptr_t)c + 1;
    if ((uintptr_t)lv_obj_get_user_data(obj) == tag) return;
    lv_obj_set_user_data(obj, (void *)tag);
    lv_obj_set_style_text_color(obj, hex(c), 0);
}

static void set_border_color(lv_obj_t *obj, uint32_t c) {
    uintptr_t tag = (uintptr_t)c + 1;
    if ((uintptr_t)lv_obj_get_user_data(obj) == tag) return;
    lv_obj_set_user_data(obj, (void *)tag);
    lv_obj_set_style_border_color(obj, hex(c), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_align_t align, int32_t x, int32_t y, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    set_text_color(l, color);
    lv_label_set_text(l, txt);
    lv_obj_align(l, align, x, y);
    return l;
}

static void make_static(lv_obj_t *obj) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// ---------------------------------------------------------------------------
// Round gauge
// ---------------------------------------------------------------------------
#define GAUGE_SIZE   252
#define SCALE_SIZE   234
#define NEEDLE_LEN   100
#define SCALE_MULT   10        // scale runs in tenths for a smoother needle

struct Zone { int32_t from, to; uint32_t color; };

struct GaugeScale {
    int32_t min, max;
    int32_t total_ticks, major_every;
    const char **labels;
};

struct Gauge {
    lv_obj_t *bezel, *scale, *needle, *value, *peak;
    int32_t min, max;
    float shown;
    int32_t needle_pos;
    float peak_val;
    bool peak_valid;
};

static const char *k_labels_0_120[] = {"0", "20", "40", "60", "80", "100", "120", nullptr};
static const char *k_labels_0_150[] = {"0", "25", "50", "75", "100", "125", "150", nullptr};

static const GaugeScale k_scale_0_120 = {0, 120, 25, 4, k_labels_0_120};  // tick every 5
static const GaugeScale k_scale_0_150 = {0, 150, 31, 5, k_labels_0_150};  // tick every 5

static lv_style_t s_zone_styles[12];
static int s_zone_style_count = 0;

static void gauge_create(Gauge &g, lv_obj_t *parent, int32_t x, int32_t y,
                         const char *title, const char *unit, const GaugeScale &sc,
                         const Zone *zones, int n_zones) {
    g.min = sc.min;
    g.max = sc.max;
    g.shown = sc.min;
    g.needle_pos = -1;
    g.peak_valid = false;

    g.bezel = lv_obj_create(parent);
    make_static(g.bezel);
    lv_obj_set_size(g.bezel, GAUGE_SIZE, GAUGE_SIZE);
    lv_obj_set_pos(g.bezel, x, y);
    lv_obj_set_style_radius(g.bezel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g.bezel, hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(g.bezel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g.bezel, 3, 0);
    set_border_color(g.bezel, COL_BORDER);
    lv_obj_set_style_pad_all(g.bezel, 0, 0);

    // Text first so the needle draws over it.
    make_label(g.bezel, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_CENTER, 0, -50, title);
    g.peak  = make_label(g.bezel, &lv_font_montserrat_14, COL_DIM, LV_ALIGN_CENTER, 0, -24, "");
    g.value = make_label(g.bezel, &lv_font_montserrat_40, COL_STALE, LV_ALIGN_CENTER, 0, 48, "--");
    make_label(g.bezel, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_CENTER, 0, 90, unit);

    lv_obj_t *s = lv_scale_create(g.bezel);
    g.scale = s;
    make_static(s);
    lv_obj_set_size(s, SCALE_SIZE, SCALE_SIZE);
    lv_obj_center(s);
    lv_scale_set_mode(s, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s, sc.min * SCALE_MULT, sc.max * SCALE_MULT);
    lv_scale_set_total_tick_count(s, sc.total_ticks);
    lv_scale_set_major_tick_every(s, sc.major_every);
    lv_scale_set_label_show(s, true);
    lv_scale_set_text_src(s, sc.labels);
    lv_scale_set_angle_range(s, 270);
    lv_scale_set_rotation(s, 135);

    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_arc_width(s, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s, hex(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_length(s, 14, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s, hex(COL_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s, hex(COL_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_16, LV_PART_INDICATOR);
    lv_obj_set_style_length(s, 7, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s, hex(COL_DIM), LV_PART_ITEMS);

    for (int i = 0; i < n_zones && s_zone_style_count < 12; i++) {
        lv_style_t *st = &s_zone_styles[s_zone_style_count++];
        lv_style_init(st);
        lv_style_set_arc_color(st, hex(zones[i].color));
        lv_style_set_arc_width(st, 6);
        lv_style_set_line_color(st, hex(zones[i].color));
        lv_scale_section_t *sec = lv_scale_add_section(s);
        lv_scale_section_set_range(sec, zones[i].from * SCALE_MULT, zones[i].to * SCALE_MULT);
        lv_scale_section_set_style(sec, LV_PART_MAIN, st);
        lv_scale_section_set_style(sec, LV_PART_INDICATOR, st);
        lv_scale_section_set_style(sec, LV_PART_ITEMS, st);
    }

    g.needle = lv_line_create(s);
    make_static(g.needle);
    lv_obj_set_style_line_width(g.needle, 5, 0);
    lv_obj_set_style_line_color(g.needle, hex(COL_NEEDLE), 0);
    lv_obj_set_style_line_rounded(g.needle, true, 0);
    lv_scale_set_line_needle_value(s, g.needle, NEEDLE_LEN, sc.min * SCALE_MULT);

    lv_obj_t *hub = lv_obj_create(g.bezel);
    make_static(hub);
    lv_obj_set_size(hub, 18, 18);
    lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, hex(COL_NEEDLE), 0);
    lv_obj_set_style_border_width(hub, 0, 0);
}

static void gauge_set_needle(Gauge &g, float v) {
    int32_t pos = (int32_t)lroundf(constrain(v, (float)g.min, (float)g.max) * SCALE_MULT);
    if (pos == g.needle_pos) return;
    g.needle_pos = pos;
    lv_scale_set_line_needle_value(g.scale, g.needle, NEEDLE_LEN, pos);
}

// `ok`: value is fresh. `supported`: the car provides this signal at all.
static void gauge_update(Gauge &g, bool ok, bool supported, float v, uint32_t color, bool alarm) {
    char buf[16];
    if (ok) {
        g.shown += (v - g.shown) * 0.35f;
        gauge_set_needle(g, g.shown);
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(v));
        set_text(g.value, buf);
        set_text_color(g.value, color);
    } else {
        g.shown = g.min;
        gauge_set_needle(g, g.min);
        set_text(g.value, supported ? "--" : "N/A");
        set_text_color(g.value, COL_STALE);
    }
    bool flash = alarm && ((millis() / 400) & 1);
    set_border_color(g.bezel, flash ? COL_RED : (alarm ? 0x601010 : COL_BORDER));
}

// Min/max marker shown under the gauge title.
static void gauge_track_peak(Gauge &g, bool ok, float v, bool track_min) {
    if (ok && (!g.peak_valid || (track_min ? v < g.peak_val : v > g.peak_val))) {
        g.peak_val = v;
        g.peak_valid = true;
    }
    char buf[16] = "";
    if (g.peak_valid) snprintf(buf, sizeof(buf), "%s %d", track_min ? "MIN" : "MAX", (int)lroundf(g.peak_val));
    set_text(g.peak, buf);
}

// ---------------------------------------------------------------------------
// Text tiles
// ---------------------------------------------------------------------------
struct Tile { lv_obj_t *box, *title, *value, *unit; };

static void tile_create(Tile &t, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                        const char *title, const char *unit, const lv_font_t *font) {
    t.box = lv_obj_create(parent);
    make_static(t.box);
    lv_obj_set_pos(t.box, x, y);
    lv_obj_set_size(t.box, w, h);
    lv_obj_set_style_radius(t.box, 12, 0);
    lv_obj_set_style_bg_color(t.box, hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(t.box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t.box, 2, 0);
    lv_obj_set_style_border_color(t.box, hex(COL_BORDER), 0);
    lv_obj_set_style_pad_ver(t.box, 6, 0);
    lv_obj_set_style_pad_hor(t.box, 3, 0);  // narrow so "FUEL TRIM B1" fits at 20 px

    t.title = make_label(t.box, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 0, title);
    t.value = make_label(t.box, font, COL_STALE, LV_ALIGN_CENTER, 0, 2, "--");
    lv_obj_set_style_text_align(t.value, LV_TEXT_ALIGN_CENTER, 0);
    t.unit  = make_label(t.box, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_BOTTOM_MID, 0, 0, unit);
}

static void tile_update(Tile &t, bool ok, bool supported, const char *txt, uint32_t color) {
    set_text(t.value, ok ? txt : (supported ? "--" : "N/A"));
    set_text_color(t.value, ok ? color : COL_STALE);
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------
static Gauge g_oil, g_water, g_oilt;
static Tile t_batt, t_fuel_psi, t_speed, t_trim1, t_trim2;
static lv_obj_t *l_status;

static void on_long_press(lv_event_t *) {
    g_oil.peak_valid = false;
    g_water.peak_valid = false;
    g_oilt.peak_valid = false;
}

void ui_create() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_long_press, LV_EVENT_LONG_PRESSED, nullptr);

    static const Zone oil_zones[] = {
        {0, OIL_RED_BELOW_PSI, COL_RED},
        {OIL_RED_BELOW_PSI, OIL_AMBER_BELOW_PSI, COL_AMBER},
        {OIL_AMBER_BELOW_PSI, 120, COL_GREEN},
    };
    static const Zone water_zones[] = {
        {0, WATER_COLD_BELOW_C, COL_BLUE},
        {WATER_COLD_BELOW_C, WATER_AMBER_ABOVE_C, COL_GREEN},
        {WATER_AMBER_ABOVE_C, WATER_RED_ABOVE_C, COL_AMBER},
        {WATER_RED_ABOVE_C, 120, COL_RED},
    };
    static const Zone oil_temp_zones[] = {
        {0, OIL_TEMP_COLD_BELOW_C, COL_BLUE},
        {OIL_TEMP_COLD_BELOW_C, OIL_TEMP_AMBER_ABOVE_C, COL_GREEN},
        {OIL_TEMP_AMBER_ABOVE_C, OIL_TEMP_RED_ABOVE_C, COL_AMBER},
        {OIL_TEMP_RED_ABOVE_C, 150, COL_RED},
    };
    const int32_t gx0 = 10, gdx = GAUGE_SIZE + 12, gy = 8;
    gauge_create(g_oil,   scr, gx0 + 0 * gdx, gy, "OIL PRESS", "psi", k_scale_0_120, oil_zones, 3);
    gauge_create(g_water, scr, gx0 + 1 * gdx, gy, "WATER", "\xC2\xB0" "C", k_scale_0_120, water_zones, 4);
    gauge_create(g_oilt,  scr, gx0 + 2 * gdx, gy, "OIL TEMP", "\xC2\xB0" "C", k_scale_0_150, oil_temp_zones, 4);

    const int32_t w = 148, h = 172, x0 = 10, dx = 158, y = 272;
    const lv_font_t *f = &lv_font_montserrat_48;
    tile_create(t_batt,     scr, x0 + 0 * dx, y, w, h, "BATTERY", "volts", f);
    tile_create(t_fuel_psi, scr, x0 + 1 * dx, y, w, h, "FUEL PRESS", "psi", f);
    tile_create(t_speed,    scr, x0 + 2 * dx, y, w, h, "SPEED", "km/h", f);
    tile_create(t_trim1,    scr, x0 + 3 * dx, y, w, h, "FUEL TRIM B1", "%", &lv_font_montserrat_40);
    tile_create(t_trim2,    scr, x0 + 4 * dx, y, w, h, "FUEL TRIM B2", "%", &lv_font_montserrat_40);

    l_status = make_label(scr, &lv_font_montserrat_14, COL_DIM, LV_ALIGN_BOTTOM_MID, 0, -12, "");
}

// Short term on the first line, long term on the second.
static void trim_update(Tile &t, const ObdSnapshot &d, Sig st, Sig lt, uint32_t now) {
    char buf[24];
    bool ok = sig_fresh(d.s[st], now, STALE_MS) && sig_fresh(d.s[lt], now, STALE_MS);
    bool supported = (d.s[st].supported && d.s[lt].supported) || d.link != LINK_OK;
    float s = d.s[st].v, l = d.s[lt].v;
    snprintf(buf, sizeof(buf), "ST %+d\nLT %+d", (int)lroundf(s), (int)lroundf(l));
    tile_update(t, ok, supported, buf, fabsf(s + l) > FUEL_TRIM_WARN_PCT ? COL_AMBER : COL_TEXT);
}

void ui_update(const ObdSnapshot &d) {
    const uint32_t now = millis();
    char buf[24];
    auto ok = [&](Sig s) { return sig_fresh(d.s[s], now, STALE_MS); };
    auto val = [&](Sig s) { return d.s[s].v; };
    auto sup = [&](Sig s) { return d.s[s].supported || d.link != LINK_OK; };

    const bool running = ok(SIG_RPM) && val(SIG_RPM) > RUNNING_RPM;

    // --- Oil pressure ------------------------------------------------------
    {
        bool o = ok(SIG_OIL_PSI);
        float v = val(SIG_OIL_PSI);
        uint32_t c = v < OIL_RED_BELOW_PSI ? COL_RED : v < OIL_AMBER_BELOW_PSI ? COL_AMBER : COL_TEXT;
        gauge_update(g_oil, o, sup(SIG_OIL_PSI), v, c, o && running && v < OIL_WARN_PSI);
        gauge_track_peak(g_oil, o && running, v, true);
    }

    // --- Coolant -----------------------------------------------------------
    {
        bool o = ok(SIG_COOLANT_C);
        float v = val(SIG_COOLANT_C);
        uint32_t c = v >= WATER_RED_ABOVE_C ? COL_RED : v >= WATER_AMBER_ABOVE_C ? COL_AMBER
                   : v < WATER_COLD_BELOW_C ? COL_BLUE : COL_TEXT;
        gauge_update(g_water, o, sup(SIG_COOLANT_C), v, c, o && v >= WATER_WARN_C);
        gauge_track_peak(g_water, o, v, false);
    }

    // --- Oil temperature ---------------------------------------------------
    {
        bool o = ok(SIG_OIL_TEMP_C);
        float v = val(SIG_OIL_TEMP_C);
        uint32_t c = v >= OIL_TEMP_RED_ABOVE_C ? COL_RED : v >= OIL_TEMP_AMBER_ABOVE_C ? COL_AMBER
                   : v < OIL_TEMP_COLD_BELOW_C ? COL_BLUE : COL_TEXT;
        gauge_update(g_oilt, o, sup(SIG_OIL_TEMP_C), v, c, o && v >= OIL_TEMP_WARN_C);
        gauge_track_peak(g_oilt, o, v, false);
    }

    // --- Bottom row ------------------------------------------------------
    {
        float v = val(SIG_VBATT);
        snprintf(buf, sizeof(buf), "%.1f", v);
        bool warn = v < VBATT_LOW_WARN || v > VBATT_HIGH_WARN;
        tile_update(t_batt, ok(SIG_VBATT), sup(SIG_VBATT), buf, warn ? COL_AMBER : COL_TEXT);
    }

    snprintf(buf, sizeof(buf), "%d", (int)lroundf(val(SIG_FUEL_PSI)));
    tile_update(t_fuel_psi, ok(SIG_FUEL_PSI), sup(SIG_FUEL_PSI), buf, COL_TEXT);

    snprintf(buf, sizeof(buf), "%d", (int)lroundf(val(SIG_SPEED)));
    tile_update(t_speed, ok(SIG_SPEED), sup(SIG_SPEED), buf, COL_TEXT);

    trim_update(t_trim1, d, SIG_STFT1, SIG_LTFT1, now);
    trim_update(t_trim2, d, SIG_STFT2, SIG_LTFT2, now);

    // --- Status line -------------------------------------------------------
    switch (d.link) {
        case LINK_INIT:        set_text(l_status, "starting");            set_text_color(l_status, COL_DIM);   break;
        case LINK_WAITING_ECU: set_text(l_status, "waiting for ECU...");  set_text_color(l_status, COL_AMBER); break;
        case LINK_OK:          set_text(l_status, "OBD-II " LV_SYMBOL_OK); set_text_color(l_status, COL_GREEN); break;
        case LINK_BUS_ERROR:   set_text(l_status, "CAN bus error");       set_text_color(l_status, COL_RED);   break;
        case LINK_DEMO:        set_text(l_status, "DEMO DATA");           set_text_color(l_status, COL_BLUE);  break;
    }
}

// OBD-II gauge cluster for the Waveshare ESP32-S3-Touch-LCD-4.3B.
//   Core 0: CAN/OBD polling task (obd.cpp)
//   Core 1: LVGL + touch (this file)
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "board.h"
#include "config.h"
#include "obd.h"
#include "ui.h"

#define DRAW_BUF_LINES 40

static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map,
                            lv_area_get_width(area), lv_area_get_height(area));
    lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t *, lv_indev_data_t *data) {
    int16_t x, y;
    if (touch_read(x, y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void *alloc_draw_buf(size_t bytes) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nOBD-II gauges starting");

    if (!board_power_init()) Serial.println("[main] display power-up may have failed");
    if (!gfx->begin()) Serial.println("[main] gfx->begin() failed");
    gfx->fillScreen(RGB565_BLACK);
    bool have_touch = touch_init();

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    const size_t buf_bytes = LCD_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t);
    void *buf1 = alloc_draw_buf(buf_bytes);
    void *buf2 = alloc_draw_buf(buf_bytes);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    if (have_touch) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_cb);
    }

    ui_create();
    obd_begin();
}

void loop() {
    static uint32_t last_ui = 0;
    uint32_t now = millis();
    if (now - last_ui >= 50) {
        last_ui = now;
        ObdSnapshot snap;
        obd_snapshot(snap);
        ui_update(snap);
    }
    lv_timer_handler();
    delay(5);
}

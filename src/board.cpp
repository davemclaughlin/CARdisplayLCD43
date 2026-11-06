#include <Wire.h>
#include "board.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Pins (from Waveshare's ESP32-S3-Touch-LCD-4.3B demo code)
// ---------------------------------------------------------------------------
#define I2C_SDA   8
#define I2C_SCL   9
#define TOUCH_INT 4

// CH422G: each register is its own I2C address.
#define CH422G_ADDR_SET 0x24   // system parameter register
#define CH422G_ADDR_IO  0x38   // IO0..IO7 output levels
#define CH422G_SET_IO_OE 0x01  // IO0..IO7 as push-pull outputs

// CH422G IO bits. EXIO0 and EXIO5 are the isolated digital inputs DI0/DI1;
// they are left low, as in Waveshare's own LCD demo.
#define EXIO_TP_RST  (1 << 1)
#define EXIO_LCD_BL  (1 << 2)
#define EXIO_LCD_RST (1 << 3)
#define EXIO_SD_CS   (1 << 4)

// ---------------------------------------------------------------------------
// Display: ST7262 800x480, plain 16-bit RGB (no command init)
// ---------------------------------------------------------------------------
static Arduino_ESP32RGBPanel *s_rgbpanel = new Arduino_ESP32RGBPanel(
    5 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 7 /* PCLK */,
    1 /* R0 */, 2 /* R1 */, 42 /* R2 */, 41 /* R3 */, 40 /* R4 */,
    39 /* G0 */, 0 /* G1 */, 45 /* G2 */, 48 /* G3 */, 47 /* G4 */, 21 /* G5 */,
    14 /* B0 */, 38 /* B1 */, 18 /* B2 */, 17 /* B3 */, 10 /* B4 */,
    0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
    0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 16000000 /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */, LCD_WIDTH * 10 /* bounce_buffer_size_px */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, s_rgbpanel, DISPLAY_ROTATION, true /* auto_flush */);

// ---------------------------------------------------------------------------
// I2C / CH422G
// ---------------------------------------------------------------------------
static bool i2c_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static bool ch422g_write(uint8_t addr, uint8_t val) {
    for (int attempt = 0; attempt < 5; attempt++) {
        Wire.beginTransmission(addr);
        Wire.write(val);
        if (Wire.endTransmission() == 0) return true;
        delay(10 + attempt * 10);
    }
    return false;
}

bool board_power_init() {
    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    delay(20);

    const uint8_t base = EXIO_SD_CS;  // SD card deselected
    if (!ch422g_write(CH422G_ADDR_SET, CH422G_SET_IO_OE) ||
        !ch422g_write(CH422G_ADDR_IO, base)) {       // LCD + touch held in reset, backlight off
        Serial.println("[board] CH422G IO expander not responding");
        return false;
    }

    // Hold GT911 INT low while its reset is released -> address 0x5D.
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    delay(100);
    ch422g_write(CH422G_ADDR_IO, base | EXIO_LCD_RST | EXIO_TP_RST);
    delay(200);
    pinMode(TOUCH_INT, INPUT);

    ch422g_write(CH422G_ADDR_IO, base | EXIO_LCD_RST | EXIO_TP_RST | EXIO_LCD_BL);
    Serial.println("[board] CH422G ready");
    return true;
}

// ---------------------------------------------------------------------------
// GT911 touch (minimal polling driver)
// ---------------------------------------------------------------------------
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINT1 0x8150

static uint8_t s_gt911_addr = 0;
static bool s_touch_down = false;
static int16_t s_touch_x = 0, s_touch_y = 0;

static bool gt911_read(uint16_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(s_gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(s_gt911_addr, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static void gt911_write8(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(s_gt911_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    Wire.endTransmission();
}

bool touch_init() {
    const uint8_t addrs[] = {0x5D, 0x14};
    for (uint8_t a : addrs) {
        if (i2c_probe(a)) {
            s_gt911_addr = a;
            Serial.printf("[board] GT911 touch at 0x%02X\n", a);
            return true;
        }
    }
    Serial.println("[board] GT911 touch not found");
    return false;
}

bool touch_read(int16_t &x, int16_t &y) {
    if (s_gt911_addr) {
        uint8_t status;
        if (gt911_read(GT911_REG_STATUS, &status, 1) && (status & 0x80)) {
            uint8_t points = status & 0x0F;
            uint8_t p[4];
            if (points > 0 && gt911_read(GT911_REG_POINT1, p, 4)) {
                int16_t rx = p[0] | (p[1] << 8);
                int16_t ry = p[2] | (p[3] << 8);
                if ((DISPLAY_ROTATION & 3) == 2) {
                    s_touch_x = LCD_WIDTH - 1 - rx;
                    s_touch_y = LCD_HEIGHT - 1 - ry;
                } else {
                    s_touch_x = rx;
                    s_touch_y = ry;
                }
                s_touch_down = true;
            } else {
                s_touch_down = false;
            }
            gt911_write8(GT911_REG_STATUS, 0);
        }
    }
    x = s_touch_x;
    y = s_touch_y;
    return s_touch_down;
}

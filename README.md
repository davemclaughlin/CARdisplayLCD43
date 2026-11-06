# OBD-II Gauge Cluster: Waveshare ESP32-S3-Touch-LCD-4.3B

An LVGL 9 dashboard for the Waveshare ESP32-S3-Touch-LCD-4.3B (800×480 ST7262 display, GT911 touch, CH422G IO expander, built-in TJA1051 CAN transceiver). It reads live engine data from the car's OBD-II port over CAN.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│   ╭───────────╮            ╭───────────╮            ╭───────────╮            │
│  │  OIL PRESS  │          │    WATER    │          │  OIL TEMP   │           │  round gauges
│  │   MIN 32    │          │   MAX 98    │          │   MAX 112   │           │  colour zones, needle,
│  │     45      │          │     91      │          │    102      │           │  flash on alarm
│   ╰─── psi ───╯            ╰──── °C ───╯            ╰──── °C ───╯            │  0–120 / 0–120 / 0–150
├──────────────┬──────────────┬──────────────┬──────────────┬──────────────────┤
│   BATTERY    │  FUEL PRESS  │    SPEED     │ FUEL TRIM B1 │  FUEL TRIM B2    │
│     13.9     │      58      │      80      │    ST +3     │     ST +1        │
│              │              │              │    LT -2     │     LT +2        │
│    volts     │     psi      │     km/h     │      %       │       %          │
└──────────────┴──────────────┴──────────────┴──────────────┴──────────────────┘
                                 OBD-II ✓   (status line)
```

**Long-press anywhere** to reset the oil-pressure minimum and the coolant and oil temperature maximum markers.

## Build

This is a PlatformIO project. Open the folder in VS Code with the PlatformIO extension, or run:

```bash
pio run -t upload
```

Serial logs come out over the board's USB-C port at 115200 baud. If the board ever won't take an upload, hold BOOT, tap RESET, then release BOOT.

### Settings in `src/config.h`

| Setting | Purpose |
|---|---|
| `DEMO_MODE 1` | Generates fake data so you can test the UI on the bench without a car. |
| `CAN_BITRATE_KBPS` | 500 is right for almost every car. Set 250 for some older or commercial vehicles. |
| `OIL_TEMP_M22_*` | Fallback DID for oil temperature when the ECU lacks PID `0x5C`. Default is GM `0x1154`. |
| `OIL_SOURCE`, `OIL_M22_*` | Where oil pressure comes from. See the next section. |
| Zone and warning thresholds | Gauge colour bands and alarm limits, including `OIL_TEMP_*` for the oil temperature gauge. |
| `DISPLAY_ROTATION` | `0` for normal landscape, `2` to flip the screen upside down. |

## Where the data comes from

| Display | Source | Notes |
|---|---|---|
| Water temp | PID `0x05` | Supported by all OBD-II cars. |
| Fuel pressure | PID `0x0A` | Gauge pressure, polled in rotation. Many petrol cars (probably including the VE V8) don't support it and show `N/A`. |
| Vehicle speed | PID `0x0D` | km/h, polled every cycle. |
| Fuel trims B1 / B2 | PIDs `0x06`/`0x07` and `0x08`/`0x09` | Short term on the top line, long term below. Turns amber when ST + LT exceeds ±15 % (`FUEL_TRIM_WARN_PCT`). |
| Oil temp | PID `0x5C`, else Mode 0x22 DID (GM `0x1154`, A − 40 °C) | Polled about once a second. Shows `N/A` if the ECM rejects both. The GM DID is unverified on the VE. |
| Battery | PID `0x42` | |
| RPM | PID `0x0C` | Not displayed. Used so the oil-pressure alarm and minimum only apply with the engine running. |
| **Oil pressure** | **Manufacturer-specific Mode 0x22 DID** | See the warning below. |

> **Oil pressure is not a standard OBD-II PID.** Most cars don't report it at all. Some report it through a manufacturer-specific Mode 0x22 DID (common on Ford, GM and VAG).
>
> To use one, set `OIL_SOURCE OIL_SRC_MODE22`, enter the ECU IDs and DID, and edit `oil_m22_decode_psi()` with your car's scaling. Torque Pro, OBDLink or FORScan PID lists are good places to look these up.
>
> If your car doesn't report oil pressure, the oil gauge shows `N/A`. The alternative is an aftermarket pressure sender, connected either to a CAN-output gauge module or to an ADC.

At startup the app asks the ECU which PIDs it supports and polls only those. Unsupported readings show `N/A`. Readings older than 2 s show `--`. Fast-changing values (oil pressure, RPM, speed, short term trims) are polled every cycle. Slow values are polled in rotation.

## Wiring

| OBD-II pin | Board |
|---|---|
| 6 (CAN-H) | CAN H |
| 14 (CAN-L) | CAN L |
| 4/5 (ground) | GND |
| 16 (+12 V battery) | 7–36 V power input terminal (via a fuse; see below) |

- **Leave the board's 120 Ω CAN termination switch OFF.** The car's bus is already terminated.
- OBD pin 16 is live even with the ignition off. Power the board from a switched source, or unplug it when parked.
- Use a proper automotive buck converter if you can. Vehicle power has load-dump spikes.

## Board notes

Hardware details, all taken from Waveshare's own ESP32-S3-Touch-LCD-4.3B demo code:

| Function | Pins |
|---|---|
| CAN (TWAI) | TX GPIO15, RX GPIO16, on the board's CAN terminal |
| I²C (touch, expander, RTC) | SDA GPIO8, SCL GPIO9 |
| CH422G IO expander | EXIO0 isolated input DI0, EXIO1 touch reset, EXIO2 backlight, EXIO3 LCD reset, EXIO4 SD card CS, EXIO5 isolated input DI1 |
| GT911 touch | INT GPIO4, I²C 0x5D |
| RGB display | 800×480, 16 MHz pixel clock, 16-bit RGB565 |

- **This targets the ESP32-S3-Touch-LCD-4.3B.** The original 4.3 (no "B") routes CAN through the USB pins and needs different settings.
- The board takes 7–36 V on its power terminal, so it can run straight from the car's 12 V. Still use a fused, switched supply.
- RS485 (GPIO43/44), the isolated digital inputs/outputs and the RTC are not used.
- **Wrong colours, a shifted image or flicker:** the display timing comes from Waveshare's demo (16 MHz pixel clock, porches 8/4/8). If the image drifts sideways under load, try lowering `prefer_speed` in `src/board.cpp` to 14000000.

## Suggested additional readings

The bottom row shows battery voltage, fuel pressure, vehicle speed and both banks of fuel trims. Other useful readings you can swap in:

| Reading | PID | Why |
|---|---|---|
| RPM | `0x0C` | Already polled; just needs a tile. |
| Ignition timing | `0x0E` | Removed from the default layout. Shows knock retard when timing drops under load. |
| Engine load | `0x04` | Removed from the default layout. |
| Throttle position | `0x11` | Removed from the default layout. |
| Intake air temp | `0x0F` | Removed from the default layout. |
| Intake vacuum / boost | `0x0B` (MAP) and `0x33` (baro) | Removed from the default layout. |
| Air-fuel ratio | `0x34` / `0x24` (wideband), `0x44` (commanded) | Removed from the default layout. A VE V8 only reports commanded AFR. |
| Fuel level | `0x2F` | Removed from the default layout. |
| Catalyst temp | `0x3C` | |

To add one, add a `Sig` in `obd.h`, a row in `s_items[]` and `decode()` in `obd.cpp`, and a tile in `ui.cpp`.

The bottom row has room for a second row of five tiles if you shrink them to about 82 px high. You could also use touch to page between tile sets.

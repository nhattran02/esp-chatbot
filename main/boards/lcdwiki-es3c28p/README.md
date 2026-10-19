# LCDWIKI ES3C28P

2.8" ESP32-S3 display module (ES3C28P with touch, ES3N28P without touch).

Docs: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

| Item | Value |
|------|-------|
| MCU | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) |
| Display | 2.8" IPS 320x240 (landscape), ILI9341V, 4-wire SPI |
| Touch | FT6336G, I2C 0x38 (ES3C28P only) |
| Audio | ES8311 codec + FM8002E amplifier, on-board MEMS mic |
| LED | WS2812B on IO42 |

## Display

Landscape 320x240 "Sparkle Bot" UI (`sparkle_display.cc`), one page per device
state:

| Page | Device state |
|------|--------------|
| Clock + robot face | idle |
| Listening (rings, sound wave, transcript) | listening |
| Thinking (orbiting stars, quoted request) | connecting, listening after the transcript arrives, boot / activation / upgrade |
| Speaking (talking face + chat bubbles) | speaking |
| Wi-Fi setup (hotspot, URL, device code) | Wi-Fi configuration |

Rendering notes:

- Glows and blurs are pre-rendered A8 masks, tinted at draw time; translucent
  fills are pre-blended to opaque colors. No LVGL shadow blur on moving parts.
- Only the visible page animates; LVGL redraws just the changed rectangles.
- Two 24-line DMA buffers let LVGL render while the previous strip is on SPI.
- Fonts are Be Vietnam Pro and Baloo 2 (SIL OFL 1.1) with full Vietnamese
  coverage; other characters fall back to the theme font from the assets.

`sparkle_assets.c/.h` are generated; edit `tools/gen_sparkle_assets.py` and
rerun it instead (it documents the font sources). The idle hint mentions
"Xin chào": change `kIdleHint` if the configured wake word differs.

To rotate the picture by 180°, invert both `DISPLAY_MIRROR_X` and
`DISPLAY_MIRROR_Y` in `config.h`.

## Controls

- BOOT button / tap the screen: start or stop a conversation
- Hold the screen for 3 s: enter Wi-Fi configuration mode

## Build

```bash
python scripts/build.py lcdwiki-es3c28p --language vi-VN
```

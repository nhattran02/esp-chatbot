# LCDWIKI ES3C28P

2.8" ESP32-S3 display module (ES3C28P with touch, ES3N28P without touch).

Docs: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

| Item | Value |
|------|-------|
| MCU | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) |
| Display | 2.8" IPS 240x320, ILI9341V, 4-wire SPI |
| Touch | FT6336G, I2C 0x38 (ES3C28P only) |
| Audio | ES8311 codec + FM8002E amplifier, on-board MEMS mic |
| LED | WS2812B on IO42 |

## Controls

- BOOT button / tap the screen: start or stop a conversation
- Hold the screen for 3 s: enter Wi-Fi configuration mode

## Build

```bash
python scripts/build.py lcdwiki-es3c28p --language vi-VN
```

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
| Clock + interactive robot face | idle |
| Listening (rings, sound wave, transcript) | listening |
| Thinking (orbiting stars, quoted request) | connecting, listening after the transcript arrives, boot / activation / upgrade |
| Speaking (talking face with the reply's emotion + chat bubbles) | speaking |
| Wi-Fi setup (hotspot, URL, device code) | Wi-Fi configuration |

The idle and speaking faces are one `SparkleFace` (`sparkle_face.cc`): eyes,
lids, mouth and blush morph between 18 expressions, it blinks and glances
around on its own, and emits hearts, tears, "z"s or stars. The speaking face
shows the server's emotion (`happy`, `loving`, `sad`...); after two quiet
minutes on the idle page the bot dozes off.

Rendering notes:

- Glows and blurs are pre-rendered A8 masks, tinted at draw time; translucent
  fills are pre-blended to opaque colors. No LVGL shadow blur on moving parts.
- Only the visible page animates; LVGL redraws just the changed rectangles.
  Faces are updated once per frame and only push changed values to LVGL.
- 60 fps refresh and animation (LVGL default is 30), except the listening
  page whose large pulse rings stay at 30 fps. Page changes cross-fade.
- Two 24-line DMA buffers let LVGL render while the previous strip is on SPI.
- Fonts are Be Vietnam Pro and Baloo 2 (SIL OFL 1.1) with full Vietnamese
  coverage; other characters fall back to the theme font from the assets.

`sparkle_assets.c/.h` are generated; edit `tools/gen_sparkle_assets.py` and
rerun it instead (it documents the font sources). The idle hint names the wake
words that are actually loaded.

To rotate the picture by 180°, invert both `DISPLAY_MIRROR_X` and
`DISPLAY_MIRROR_Y` in `config.h`.

## Controls

- BOOT button: start or stop a conversation
- Tap the bot: it reacts with a random expression (5 quick taps: dizzy)
- Hold or stroke the idle bot: it gets petted (blush and hearts)
- Tap anywhere else: start or stop a conversation (with a ripple)
- Hold anywhere else for 3 s (a ring fills up): Wi-Fi configuration mode
- While touching, the bot's eyes follow your finger; the chat scrolls by drag

Touch coordinates use the display's transform (`TOUCH_*` in `config.h`). If
taps land mirrored on your unit, flip `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y`.

## Build

```bash
python scripts/build.py lcdwiki-es3c28p --language vi-VN
```

To wake it with "Hello" or "Hi" (MultiNet7 English, custom wake word):

```bash
sudo docker run --rm -it -v $PWD:/project -w /project espressif/idf:v6.1 \
  python scripts/build.py lcdwiki-es3c28p --language vi-VN --wake-word custom:hello,hi
```

Short words such as "hi" trigger more easily by mistake; tune
`CUSTOM_WAKE_WORD_THRESHOLD` (default 20 %, higher = stricter) in menuconfig.

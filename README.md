# ESP Chatbot – Sparkle Bot

A voice assistant running on the **LCDWIKI ES3C28P** (ESP32-S3, 2.8-inch 320×240 landscape display), based on the [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) firmware.

| Idle | Listening | Thinking |
|:---:|:---:|:---:|
| ![Idle](docs/screenshots/idle.png) | ![Listening](docs/screenshots/listening.png) | ![Thinking](docs/screenshots/thinking.png) |

| Speaking | Wi-Fi Setup |
|:---:|:---:|
| ![Speaking](docs/screenshots/speaking.png) | ![Wi-Fi Setup](docs/screenshots/wifi-setup.png) |

## Build

```bash
docker run --rm -it -v "$PWD":/project -w /project -u $(id -u):$(id -g) -e HOME=/tmp \
  espressif/idf:release-v6.1 python scripts/build.py lcdwiki-es3c28p --language vi-VN
```

For hardware details and UI documentation, see [main/boards/lcdwiki-es3c28p](main/boards/lcdwiki-es3c28p/README.md).

## License

MIT, inherited from the original project. See [LICENSE](LICENSE).

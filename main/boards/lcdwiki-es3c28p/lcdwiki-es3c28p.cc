// LCDWIKI ES3C28P / ES3N28P - 2.8" ESP32-S3 display module
//   ILI9341V 240x320 IPS (SPI), FT6336G touch, ES8311 codec, WS2812 LED
//   https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

#include "adc_battery_monitor.h"
#include "application.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "sparkle_display.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "LCDWikiES3C28P"

// ILI9341V init sequence from LCDWIKI (ILI9341V_Init.txt for this panel)
static const ili9341_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xCF, (uint8_t[]){0x00, 0xC1, 0x30}, 3, 0},
    {0xED, (uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t[]){0x85, 0x00, 0x78}, 3, 0},
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t[]){0x20}, 1, 0},
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t[]){0x13}, 1, 0},
    {0xC1, (uint8_t[]){0x13}, 1, 0},
    {0xC5, (uint8_t[]){0x22, 0x35}, 2, 0},
    {0xC7, (uint8_t[]){0xBD}, 1, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0xB6, (uint8_t[]){0x0A, 0xA2}, 2, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0xF6, (uint8_t[]){0x01, 0x30}, 2, 0},
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t[]){0x00}, 1, 0},
    {0x26, (uint8_t[]){0x01}, 1, 0},
    {0xE0, (uint8_t[]){0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7,
                       0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58,
                       0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 20},
};

class LCDWikiES3C28PBoard : public WifiBoard {
private:
    Button boot_button_;
    SparkleDisplay* display_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    AdcBatteryMonitor* battery_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {.enable_internal_pullup = 1},
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus_));
    }

    void InitializeBattery() {
        // Same divider as the Freenove 2.8" board, which shares this PCB design
        battery_ = new AdcBatteryMonitor(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 200000, 200000,
                                         GPIO_NUM_NC);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(ili9341_lcd_init_cmd_t),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;   // none: software reset
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void*)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        ESP_LOGI(TAG, "ILI9341V installed");

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SparkleDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // Wi-Fi setup starts the AP and web server: keep that off the LVGL task
    // (the main loop may still be busy starting up)
    void EnterWifiConfigModeAsync() {
        xTaskCreate(
            [](void* arg) {
                static_cast<LCDWikiES3C28PBoard*>(arg)->EnterWifiConfigMode();
                vTaskDelete(nullptr);
            },
            "wifi_cfg_touch", 4096, this, 2, nullptr);
    }

    // FT6336G as an LVGL pointer, so the UI knows where the screen is touched:
    // the bot reacts to touches, elsewhere tap = start/stop, hold 3 s = Wi-Fi setup
    void InitializeTouch() {
        gpio_config_t rst = {};
        rst.pin_bit_mask = 1ULL << TOUCH_RST_PIN;
        rst.mode = GPIO_MODE_OUTPUT;
        gpio_config(&rst);
        gpio_set_level(TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        if (i2c_master_probe(i2c_bus_, TOUCH_I2C_ADDR, 100) != ESP_OK) {
            ESP_LOGW(TAG, "FT6336G not found (ES3N28P has no touch panel)");
            return;
        }

        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = TOUCH_I2C_ADDR;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 0;
        io_config.lcd_cmd_bits = 8;
        io_config.flags.disable_control_phase = 1;
        io_config.scl_speed_hz = 400000;
        if (esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &io) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create touch panel IO");
            return;
        }
        esp_lcd_touch_config_t tp_config = {};
        tp_config.x_max = DISPLAY_HEIGHT;  // native portrait axes
        tp_config.y_max = DISPLAY_WIDTH;
        tp_config.rst_gpio_num = GPIO_NUM_NC;  // reset above
        tp_config.int_gpio_num = GPIO_NUM_NC;
        tp_config.flags.swap_xy = TOUCH_SWAP_XY;
        tp_config.flags.mirror_x = TOUCH_MIRROR_X;
        tp_config.flags.mirror_y = TOUCH_MIRROR_Y;
        esp_lcd_touch_handle_t tp = nullptr;
        if (esp_lcd_touch_new_i2c_ft5x06(io, &tp_config, &tp) != ESP_OK || tp == nullptr) {
            ESP_LOGW(TAG, "FT6336G init failed, continuing without touch");
            esp_lcd_panel_io_del(io);
            return;
        }

        lvgl_port_touch_cfg_t touch_config = {};
        touch_config.disp = lv_display_get_default();
        touch_config.handle = tp;
        lv_indev_t* indev = lvgl_port_add_touch(&touch_config);
        if (indev == nullptr) {
            ESP_LOGW(TAG, "Failed to register touch with LVGL");
            return;
        }
        if (lvgl_port_lock(0)) {
            // Read at 60 Hz so the eyes follow a finger smoothly
            lv_timer_set_period(lv_indev_get_read_timer(indev), 16);
            lvgl_port_unlock();
        }

        // Callbacks run in the LVGL task and must not block it
        display_->EnableTouch(
            [this]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigModeAsync();
                } else {
                    app.ToggleChatState();  // only sets an event bit
                }
            },
            [this]() { EnterWifiConfigModeAsync(); });
        ESP_LOGI(TAG, "FT6336G touch ready");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
                           "End this conversation and enter WiFi configuration mode.\n"
                           "**CAUTION** You must ask the user to confirm this action.",
                           PropertyList(), [this](const PropertyList& properties) {
                               EnterWifiConfigMode();
                               return true;
                           });
    }

public:
    LCDWikiES3C28PBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeBattery();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_, AUDIO_CODEC_I2C_NUM, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true,
            AUDIO_CODEC_PA_INVERTED);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = battery_->IsCharging();
        discharging = battery_->IsDischarging();
        level = battery_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(LCDWikiES3C28PBoard);

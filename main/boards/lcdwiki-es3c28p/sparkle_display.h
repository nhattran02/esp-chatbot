#ifndef SPARKLE_DISPLAY_H
#define SPARKLE_DISPLAY_H

#include "device_state.h"
#include "display/lcd_display.h"

#include <cstdint>
#include <string>
#include <vector>

// "Sparkle Bot" UI for the 320x240 landscape panel (see the board README).
// One page per device state: idle clock, listening, thinking, speaking and
// Wi-Fi setup. Pages are built once; only the active page animates.
class SparkleDisplay : public LcdDisplay {
public:
    SparkleDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, bool mirror_x, bool mirror_y, bool swap_xy);

    virtual void SetupUI() override;
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void UpdateStatusBar(bool update_all = false) override;

    // Animation targets that need more than one object
    struct Eye {
        lv_obj_t* body = nullptr;
        lv_obj_t* glow = nullptr;
        lv_obj_t* shine = nullptr;
        int32_t center_y = 0;
        int32_t height = 0;
        int32_t last_height = -1;
    };
    struct Blink {
        Eye eyes[2];
    };
    struct Orbit {
        lv_obj_t* items[3] = {};
        int16_t angles[3] = {};
        int32_t radius[3] = {};
        int32_t half[3] = {};
        int count = 0;
        int32_t cx = 0;
        int32_t cy = 0;
        int direction = 1;
    };
    struct Look {
        lv_obj_t* items[4] = {};
        int32_t last = INT32_MIN;
    };

private:
    enum Page { kPageNone = -1, kPageIdle, kPageListening, kPageThinking, kPageSpeaking, kPageWifi,
                kPageCount };

    lv_obj_t* pages_[kPageCount] = {};
    Page page_ = kPageNone;
    lv_timer_t* tick_timer_ = nullptr;
    std::vector<void*> anim_vars_;

    // Top bar
    lv_obj_t* bar_star_ = nullptr;
    lv_obj_t* bar_title_ = nullptr;
    lv_obj_t* mic_pill_ = nullptr;
    lv_obj_t* mic_dot_ = nullptr;
    lv_obj_t* battery_box_ = nullptr;
    lv_obj_t* battery_fill_ = nullptr;

    // Idle
    lv_obj_t* idle_face_ = nullptr;
    lv_obj_t* idle_antenna_star_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* info_label_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    Blink idle_blink_;

    // Listening
    lv_obj_t* rings_[2] = {};
    lv_obj_t* listen_bars_[9] = {};
    lv_obj_t* transcript_label_ = nullptr;
    lv_obj_t* caret_ = nullptr;

    // Thinking
    Orbit stars_orbit_;
    Orbit dots_orbit_;
    Look look_;
    lv_obj_t* think_title_ = nullptr;
    lv_obj_t* think_caption_ = nullptr;
    lv_obj_t* think_dots_[3] = {};

    // Speaking
    lv_obj_t* speak_face_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* speak_bars_[5] = {};
    lv_obj_t* chat_list_ = nullptr;

    // Wi-Fi setup
    lv_obj_t* spinner_[2] = {};
    lv_obj_t* wifi_arcs_[3] = {};
    lv_obj_t* wifi_ssid_ = nullptr;
    lv_obj_t* wifi_url_ = nullptr;
    lv_obj_t* wifi_code_ = nullptr;

    // Twinkling stars per page
    std::vector<lv_obj_t*> twinkles_[kPageCount];
    std::vector<uint32_t> twinkle_timing_[kPageCount];

    // Conversation state, guarded by the LVGL lock
    std::string user_text_;
    std::string system_text_;
    int64_t user_text_us_ = 0;
    int64_t listening_since_us_ = 0;
    DeviceState last_state_ = kDeviceStateUnknown;
    int clock_minute_ = -2;
    bool texts_dirty_ = true;

    void BuildTopBar(lv_obj_t* screen);
    void BuildIdle();
    void BuildListening();
    void BuildThinking();
    void BuildSpeaking();
    void BuildWifi();

    lv_obj_t* AddTwinkle(Page page, int32_t x, int32_t y, int32_t size, uint32_t color,
                         uint32_t period_ms, uint32_t delay_ms);
    void ShowPage(Page page);
    void StartAnimations(Page page);
    void StopAnimations();
    void Animate(void* var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t duration,
                 uint32_t delay, bool reverse, lv_anim_path_cb_t path = lv_anim_path_ease_in_out);

    void OnTick();
    void UpdateClock();
    void UpdateTexts(DeviceState state);
    void UpdateWifiInfo();
    void AddBubble(bool user, const char* text);
    void ApplyFallbackFont();
};

#endif  // SPARKLE_DISPLAY_H

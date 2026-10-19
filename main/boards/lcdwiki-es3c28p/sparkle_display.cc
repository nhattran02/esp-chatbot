#include "sparkle_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "lvgl_theme.h"
#include "sparkle_assets.h"
#include "system_info.h"
#include "wifi_manager.h"

#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <material_symbols.h>
#include <algorithm>
#include <cstring>
#include <ctime>

#define TAG "SparkleDisplay"

LV_FONT_DECLARE(font_material_symbols_16_4);

namespace {

// Palette from the design spec
constexpr uint32_t kBackground = 0x130B2B;
constexpr uint32_t kFace = 0x24164F;
constexpr uint32_t kFaceIdleBorder = 0x4A2F9C;
constexpr uint32_t kFaceScreen = 0x0B0620;
constexpr uint32_t kCyan = 0x5CE1E6;
constexpr uint32_t kPink = 0xFF6FB5;
constexpr uint32_t kPurple = 0xB79CFF;
constexpr uint32_t kYellow = 0xFFE27A;
constexpr uint32_t kGreen = 0x7CF5B6;
constexpr uint32_t kText = 0xF4EEFF;
constexpr uint32_t kSubText = 0xCFC3F2;
constexpr uint32_t kWhite = 0xFFFFFF;
// The design's translucent fills, pre-blended over kBackground: opaque
// fills are a plain memset for the software renderer
constexpr uint32_t kPillFill = 0x2A1F49;    // rgba(183,156,255,.14)
constexpr uint32_t kPillBorder = 0x4C3E75;  // rgba(183,156,255,.35)
constexpr uint32_t kCyanFill = 0x1A203E;    // rgba(92,225,230,.10)
constexpr uint32_t kCyanBorder = 0x306176;  // rgba(92,225,230,.40)
constexpr uint32_t kMicFill = 0x3D1D44;     // rgba(255,111,181,.18)

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kTickMs = 100;
constexpr int64_t kThinkingDelayUs = 700 * 1000;  // keep the transcript up briefly
constexpr uint32_t kMaxBubbles = 8;
constexpr intptr_t kRoleUser = 1;
constexpr intptr_t kRoleAssistant = 2;

// Must match the configured wake word to be truthful
constexpr const char* kIdleHint = "Nói #FF6FB5 “Xin chào”# để bắt đầu";
constexpr const char* kListeningPlaceholder = "Mình đang nghe nè…";

const char* const kWeekdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư",
                                 "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

// ---------------------------------------------------------------- widgets

lv_obj_t* Box(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* Rect(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color,
               int32_t radius) {
    lv_obj_t* obj = Box(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

void Border(lv_obj_t* obj, int32_t width, uint32_t color) {
    lv_obj_set_style_border_width(obj, width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
}

lv_obj_t* Text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

// A8 mask (glow, sparkle) drawn in a single color
lv_obj_t* Mask(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color, int32_t x,
               int32_t y) {
    lv_obj_t* img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(img, src);
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x, y);
    return img;
}

lv_obj_t* Arc(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, int32_t start, int32_t end,
              int32_t width, uint32_t color) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, start, end);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    return arc;
}

// Size a label to its text, wrapping only past max_width
void FitLabel(lv_obj_t* label, const char* text, int32_t max_width, int32_t padding) {
    const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    int32_t line_space = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, line_space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(label, std::min<int32_t>(size.x + 1 + padding, max_width));
    lv_label_set_text(label, text);
}

void SetTextIfChanged(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

// ---------------------------------------------------------------- animation callbacks
// LVGL only calls these when the animated value changes.

void TranslateYCb(void* var, int32_t v) {
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(var), v, 0);
}

void HeightCb(void* var, int32_t v) { lv_obj_set_height(static_cast<lv_obj_t*>(var), v); }

void OpaCb(void* var, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(v), 0);
}

void ImageOpaCb(void* var, int32_t v) {
    lv_obj_set_style_image_opa(static_cast<lv_obj_t*>(var), static_cast<lv_opa_t>(v), 0);
}

void ArcRotationCb(void* var, int32_t v) { lv_arc_set_rotation(static_cast<lv_obj_t*>(var), v); }

// twinkle: opacity .2 -> 1, scale .5 -> 1, rotate 0 -> 45 deg
void TwinkleCb(void* var, int32_t v) {
    auto img = static_cast<lv_obj_t*>(var);
    int32_t scale = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(img)));
    lv_obj_set_style_image_opa(img, static_cast<lv_opa_t>(51 + v * 204 / 256), 0);
    lv_image_set_scale(img, scale / 2 + scale * v / 512);
    lv_image_set_rotation(img, v * 450 / 256);
}

// ring: scale .7 -> 1.5, opacity .9 -> 0 (base diameter kept in user data)
void RingCb(void* var, int32_t v) {
    auto ring = static_cast<lv_obj_t*>(var);
    int32_t base = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(ring)));
    int32_t size = base * (700 + 800 * v / 1000) / 1000;
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_border_opa(ring, static_cast<lv_opa_t>(230 * (1000 - v) / 1000), 0);
}

// blink: scaleY 1 -> .08 -> 1 between 90 % and 100 % of a 4.5 s cycle
void BlinkCb(void* var, int32_t t) {
    auto blink = static_cast<SparkleDisplay::Blink*>(var);
    int32_t s = 1000;
    if (t >= 4050 && t < 4230) {
        s = 1000 - (t - 4050) * 920 / 180;
    } else if (t >= 4230) {
        s = 80 + (t - 4230) * 920 / 270;
    }
    for (auto& eye : blink->eyes) {
        if (eye.body == nullptr) {
            continue;
        }
        int32_t h = std::max<int32_t>(2, eye.height * s / 1000);
        if (h == eye.last_height) {
            continue;
        }
        eye.last_height = h;
        lv_obj_set_height(eye.body, h);
        lv_obj_set_y(eye.body, eye.center_y - h / 2);
        lv_obj_set_style_image_opa(eye.glow, static_cast<lv_opa_t>(s * 255 / 1000), 0);
        if (s < 600) {
            lv_obj_add_flag(eye.shine, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(eye.shine, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// orbit: place items on a circle, v in degrees
void OrbitCb(void* var, int32_t v) {
    auto orbit = static_cast<SparkleDisplay::Orbit*>(var);
    for (int i = 0; i < orbit->count; i++) {
        int16_t angle = static_cast<int16_t>((orbit->angles[i] + orbit->direction * v + 720) % 360);
        int32_t x = orbit->cx + orbit->radius[i] * lv_trigo_cos(angle) / LV_TRIGO_SIN_MAX;
        int32_t y = orbit->cy + orbit->radius[i] * lv_trigo_sin(angle) / LV_TRIGO_SIN_MAX;
        lv_obj_set_pos(orbit->items[i], x - orbit->half[i], y - orbit->half[i]);
    }
}

// pupils glance right / left: hold 40 %, swing 10 %, hold 40 %, swing 10 %
void LookCb(void* var, int32_t t) {
    auto look = static_cast<SparkleDisplay::Look*>(var);
    int32_t offset;
    if (t < 1200) {
        offset = 4;
    } else if (t < 1500) {
        offset = 4 - 8 * (t - 1200) / 300;
    } else if (t < 2700) {
        offset = -4;
    } else {
        offset = -4 + 8 * (t - 2700) / 300;
    }
    if (offset == look->last) {
        return;
    }
    look->last = offset;
    for (auto item : look->items) {
        if (item != nullptr) {
            lv_obj_set_style_translate_x(item, offset, 0);
        }
    }
}

// typing dots: hop 6 px and brighten during the first 80 % of 1.2 s
void DotCb(void* var, int32_t t) {
    auto dot = static_cast<lv_obj_t*>(var);
    int32_t up = 0;
    if (t < 480) {
        up = 6 * t / 480;
    } else if (t < 960) {
        up = 6 * (960 - t) / 480;
    }
    lv_obj_set_style_translate_y(dot, -up, 0);
    lv_obj_set_style_opa(dot, static_cast<lv_opa_t>(89 + 166 * up / 6), 0);
}

}  // namespace

// ---------------------------------------------------------------- display setup

SparkleDisplay::SparkleDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // Paint the background color before LVGL starts, so boot has no white flash
    uint16_t c = lv_color_to_u16(lv_color_hex(kBackground));
    std::vector<uint16_t> line(width_, static_cast<uint16_t>((c >> 8) | (c << 8)));
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, line.data());
    }

    lv_init();

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    // Two DMA strips in internal RAM: LVGL renders the next strip while SPI
    // DMA sends the previous one (the stock SPI path renders and sends serially)
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 24),
        .double_buffer = true,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .swap_bytes = 1,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };
    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
    }
}

void SparkleDisplay::ApplyFallbackFont() {
    // Characters outside the Vietnamese set (emoji, CJK...) fall back to the
    // theme text font, which is the one the assets partition provides
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* fallback = lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr
                                    ? lvgl_theme->text_font()->font()
                                    : nullptr;
    sparkle_font_text_12.fallback = fallback;
    sparkle_font_text_10.fallback = fallback;
    sparkle_font_title_12.fallback = fallback;
    sparkle_font_title_20.fallback = fallback;
}

void SparkleDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    Display::SetupUI();
    DisplayLockGuard lock(this);
    ApplyFallbackFont();

    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_text_font(screen, &sparkle_font_text_12, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);

    for (auto& page : pages_) {
        page = Box(screen);
        lv_obj_set_size(page, kScreenWidth, kScreenHeight);
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
    BuildIdle();
    BuildListening();
    BuildThinking();
    BuildSpeaking();
    BuildWifi();
    BuildTopBar(screen);

    // Base-class labels: status text is read from here, notifications toast
    lv_obj_t* holder = Box(screen);
    lv_obj_add_flag(holder, LV_OBJ_FLAG_HIDDEN);
    status_label_ = lv_label_create(holder);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = Text(lv_layer_top(), &sparkle_font_text_12, kText, "");
    lv_obj_set_style_bg_opa(notification_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(notification_label_, lv_color_hex(kPillFill), 0);
    Border(notification_label_, 1, kPillBorder);
    lv_obj_set_style_radius(notification_label_, 12, 0);
    lv_obj_set_style_pad_hor(notification_label_, 12, 0);
    lv_obj_set_style_pad_ver(notification_label_, 4, 0);
    lv_obj_set_style_max_width(notification_label_, 296, 0);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(notification_label_, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    low_battery_popup_ = Rect(lv_layer_top(), 0, 0, 280, 36, kPink, 18);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -10);
    low_battery_label_ = Text(low_battery_popup_, &sparkle_font_text_12, kWhite,
                              Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    tick_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<SparkleDisplay*>(lv_timer_get_user_data(timer))->OnTick();
        },
        kTickMs, this);
    OnTick();
}

lv_obj_t* SparkleDisplay::AddTwinkle(Page page, int32_t x, int32_t y, int32_t size,
                                     uint32_t color, uint32_t period_ms, uint32_t delay_ms) {
    // The 16 px star is scaled around its center, so shift the box to keep
    // the design's top-left corner
    int32_t offset = (16 - size) / 2;
    lv_obj_t* star = Mask(pages_[page], &sparkle_star_16, color, x - offset, y - offset);
    int32_t scale = size * 256 / 16;
    lv_obj_set_user_data(star, reinterpret_cast<void*>(static_cast<intptr_t>(scale)));
    lv_image_set_scale(star, scale);
    twinkles_[page].push_back(star);
    twinkle_timing_[page].push_back((period_ms << 16) | delay_ms);
    return star;
}

void SparkleDisplay::BuildTopBar(lv_obj_t* screen) {
    lv_obj_t* bar = Box(screen);
    lv_obj_set_size(bar, kScreenWidth, 22);

    bar_star_ = Mask(bar, &sparkle_star_16, kPink, 6, 3);
    lv_image_set_scale(bar_star_, 9 * 256 / 16);
    bar_title_ = Text(bar, &sparkle_font_title_12, kSubText, "");
    lv_obj_align(bar_title_, LV_ALIGN_LEFT_MID, 10, 0);

    mic_pill_ = Rect(bar, 0, 0, 48, 14, kMicFill, 7);
    lv_obj_align(mic_pill_, LV_ALIGN_CENTER, 0, 0);
    mic_dot_ = Rect(mic_pill_, 8, 4, 6, 6, kPink, LV_RADIUS_CIRCLE);
    lv_obj_t* mic_text = Text(mic_pill_, &sparkle_font_text_10, 0xFF9CCB, "MIC");
    lv_obj_align(mic_text, LV_ALIGN_LEFT_MID, 18, 0);

    // Battery: 18x9 outline + fill + nub
    battery_box_ = Box(bar);
    lv_obj_set_size(battery_box_, 18, 9);
    Border(battery_box_, 1, kSubText);
    lv_obj_set_style_radius(battery_box_, 3, 0);
    lv_obj_set_style_pad_all(battery_box_, 1, 0);
    lv_obj_align(battery_box_, LV_ALIGN_RIGHT_MID, -12, 0);
    battery_fill_ = Rect(battery_box_, 0, 0, 12, 5, kGreen, 1);
    lv_obj_t* nub = Rect(bar, 0, 0, 2, 4, kSubText, 1);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_flag(battery_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(battery_box_, nub);
    lv_obj_add_flag(nub, LV_OBJ_FLAG_HIDDEN);

    network_label_ = Text(bar, &font_material_symbols_16_4, kCyan, "");
    lv_obj_align(network_label_, LV_ALIGN_RIGHT_MID, -36, 0);
    mute_label_ = Text(bar, &font_material_symbols_16_4, kPink, "");
    lv_obj_align(mute_label_, LV_ALIGN_RIGHT_MID, -56, 0);
}

// ---------------------------------------------------------------- pages

void SparkleDisplay::BuildIdle() {
    lv_obj_t* p = pages_[kPageIdle];
    Mask(p, &sparkle_glow_bloom, kPurple, -40, 20);

    AddTwinkle(kPageIdle, 22, 40, 12, kYellow, 2200, 0);
    AddTwinkle(kPageIdle, 150, 58, 9, kCyan, 1700, 400);
    AddTwinkle(kPageIdle, 30, 196, 10, kPink, 2600, 900);
    AddTwinkle(kPageIdle, 292, 44, 8, kPurple, 2000, 1200);

    // Robot: halo, antenna star, head, inner screen, eyes, smile, cheeks.
    // The group bobs as a whole; coordinates are relative to (5, 42).
    idle_face_ = Box(p);
    lv_obj_set_pos(idle_face_, 5, 42);
    lv_obj_set_size(idle_face_, 170, 170);
    Mask(idle_face_, &sparkle_glow_face_l, kPurple, 0, 0);
    idle_antenna_star_ = Mask(idle_face_, &sparkle_star_glow_16, kYellow, 73, -5);
    lv_image_set_scale(idle_antenna_star_, 14 * 256 / 16);
    Rect(idle_face_, 83, 14, 3, 10, kPurple, 2);

    lv_obj_t* head = Rect(idle_face_, 24, 24, 122, 114, kFace, LV_RADIUS_CIRCLE);
    Border(head, 2, kFaceIdleBorder);
    lv_obj_t* screen = Rect(head, 0, 0, 94, 76, kFaceScreen, LV_RADIUS_CIRCLE);
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, 0);
    for (int i = 0; i < 2; i++) {
        int32_t x = 18 + i * 39;
        Eye& eye = idle_blink_.eyes[i];
        eye.glow = Mask(screen, &sparkle_glow_eye_tall, kCyan, x - 14, 20 - 14);
        eye.body = Rect(screen, x, 20, 19, 24, kCyan, LV_RADIUS_CIRCLE);
        eye.shine = Rect(eye.body, 3, 4, 5, 6, kWhite, 3);
        eye.center_y = 32;
        eye.height = 24;
    }
    Arc(screen, 40, 42, 14, 20, 160, 3, kPink);
    for (int i = 0; i < 2; i++) {
        lv_obj_t* cheek = Rect(head, 0, 0, 12, 7, kPink, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(cheek, LV_OPA_70, 0);
        lv_obj_align(cheek, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                     i == 0 ? 10 : -10, -28);
    }

    // Clock column
    clock_label_ = Text(p, &sparkle_font_clock_50, kWhite, "--:--");
    lv_obj_set_pos(clock_label_, 172, 60);
    date_label_ = Text(p, &sparkle_font_text_10, kSubText, "");
    lv_obj_set_pos(date_label_, 172, 102);
    lv_obj_t* wifi_icon = Text(p, &font_material_symbols_16_4, kCyan, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_pos(wifi_icon, 172, 126);
    info_label_ = Text(p, &sparkle_font_text_10, kText, "");
    lv_obj_set_pos(info_label_, 194, 126);
    lv_obj_set_width(info_label_, 116);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_MODE_DOTS);

    // Hint pill
    lv_obj_t* pill = Rect(p, 12, 194, 296, 34, kPillFill, 17);
    Border(pill, 1, kPillBorder);
    lv_obj_set_style_pad_hor(pill, 12, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 8, 0);
    Text(pill, &font_material_symbols_16_4, kPink, MATERIAL_SYMBOLS_MIC);
    hint_label_ = Text(pill, &sparkle_font_text_12, kText, "");
    lv_obj_set_style_max_width(hint_label_, 240, 0);
    lv_label_set_long_mode(hint_label_, LV_LABEL_LONG_MODE_DOTS);
}

void SparkleDisplay::BuildListening() {
    lv_obj_t* p = pages_[kPageListening];

    // Two pulse rings around the face, centered on (87, 109)
    lv_obj_t* ring_box = Box(p);
    lv_obj_set_pos(ring_box, 87 - 98, 109 - 98);
    lv_obj_set_size(ring_box, 196, 196);
    for (int i = 0; i < 2; i++) {
        rings_[i] = Box(ring_box);
        lv_obj_set_size(rings_[i], 130, 130);
        lv_obj_set_style_radius(rings_[i], LV_RADIUS_CIRCLE, 0);
        Border(rings_[i], 2, i == 0 ? kPink : kCyan);
        lv_obj_set_user_data(rings_[i], reinterpret_cast<void*>(static_cast<intptr_t>(130)));
        lv_obj_align(rings_[i], LV_ALIGN_CENTER, 0, 0);
    }

    Mask(p, &sparkle_glow_face_m, kPink, 87 - 77, 109 - 74);
    lv_obj_t* head = Rect(p, 34, 59, 106, 100, kFace, LV_RADIUS_CIRCLE);
    Border(head, 2, kPink);
    lv_obj_t* screen = Rect(head, 0, 0, 82, 66, kFaceScreen, LV_RADIUS_CIRCLE);
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, 0);
    for (int i = 0; i < 2; i++) {
        int32_t x = 13 + i * 34;
        Mask(screen, &sparkle_glow_eye_round, kCyan, x - 14, 15 - 14);
        lv_obj_t* eye = Rect(screen, x, 15, 22, 22, kCyan, LV_RADIUS_CIRCLE);
        Rect(eye, 4, 4, 7, 7, kWhite, LV_RADIUS_CIRCLE);
    }
    lv_obj_t* mouth = Box(screen);
    lv_obj_set_pos(mouth, 36, 42);
    lv_obj_set_size(mouth, 9, 9);
    lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
    Border(mouth, 2, kPink);

    AddTwinkle(kPageListening, 150, 40, 11, kYellow, 1800, 0);
    AddTwinkle(kPageListening, 14, 168, 9, kCyan, 2300, 600);

    lv_obj_t* title = Text(p, &sparkle_font_title_20, 0xFF9CCB, "Đang nghe…");
    lv_obj_set_pos(title, 168, 48);

    // Sound wave: 9 bars, heights animate around the vertical center
    static const uint32_t kBarColors[] = {kPink,    0xE58CFF, kPurple,  0x8FB8FF, kCyan,
                                          0x8FB8FF, kPurple,  0xE58CFF, kPink};
    lv_obj_t* wave = Box(p);
    lv_obj_set_pos(wave, 168, 84);
    lv_obj_set_size(wave, 140, 52);
    for (int i = 0; i < 9; i++) {
        listen_bars_[i] = Rect(wave, 0, 0, 6, 46, kBarColors[i], 3);
        lv_obj_align(listen_bars_[i], LV_ALIGN_LEFT_MID, i * 11, 0);
    }

    // Live transcript box
    lv_obj_t* box = Rect(p, 12, 188, 296, 40, kCyanFill, 14);
    Border(box, 1, kCyanBorder);
    lv_obj_set_style_pad_hor(box, 12, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 8, 0);
    Text(box, &font_material_symbols_16_4, kCyan, MATERIAL_SYMBOLS_MIC);
    transcript_label_ = Text(box, &sparkle_font_text_12, kText, "");
    lv_label_set_long_mode(transcript_label_, LV_LABEL_LONG_MODE_DOTS);
    caret_ = Rect(box, 0, 0, 2, 12, kCyan, 0);
    lv_obj_set_style_margin_left(caret_, -6, 0);
}

void SparkleDisplay::BuildThinking() {
    lv_obj_t* p = pages_[kPageThinking];
    constexpr int32_t kCx = 160;
    constexpr int32_t kCy = 100;

    lv_obj_t* ring = Mask(p, &sparkle_ring_dashed, kPurple, kCx - 85, kCy - 85);
    lv_obj_set_style_image_opa(ring, 90, 0);

    // Stars riding the dashed orbit (6 s per turn)
    static const uint32_t kStarColors[] = {kYellow, kPink, kCyan};
    static const int32_t kStarSizes[] = {16, 11, 11};
    static const int16_t kStarAngles[] = {270, 151, 29};
    stars_orbit_.count = 3;
    stars_orbit_.cx = kCx;
    stars_orbit_.cy = kCy;
    for (int i = 0; i < 3; i++) {
        lv_obj_t* star = Mask(p, &sparkle_star_glow_16, kStarColors[i], 0, 0);
        lv_image_set_scale(star, kStarSizes[i] * 256 / 16);
        stars_orbit_.items[i] = star;
        stars_orbit_.angles[i] = kStarAngles[i];
        stars_orbit_.radius[i] = 84;
        stars_orbit_.half[i] = 12;
    }
    OrbitCb(&stars_orbit_, 0);

    // Two glowing dots on an inner orbit, turning the other way (4 s)
    static const uint32_t kDotColors[] = {kPurple, kGreen};
    static const int32_t kDotSizes[] = {7, 5};
    dots_orbit_.count = 2;
    dots_orbit_.cx = kCx;
    dots_orbit_.cy = kCy;
    dots_orbit_.direction = -1;
    for (int i = 0; i < 2; i++) {
        lv_obj_t* dot = Box(p);
        lv_obj_set_size(dot, 29, 29);
        Mask(dot, &sparkle_glow_pupil, kDotColors[i], 0, 0);
        lv_obj_t* core = Rect(dot, 0, 0, kDotSizes[i], kDotSizes[i], kDotColors[i],
                              LV_RADIUS_CIRCLE);
        lv_obj_center(core);
        dots_orbit_.items[i] = dot;
        dots_orbit_.angles[i] = i == 0 ? 0 : 180;
        dots_orbit_.radius[i] = 59;
        dots_orbit_.half[i] = 14;
    }
    OrbitCb(&dots_orbit_, 0);

    Mask(p, &sparkle_glow_face_s, kPurple, kCx - 73, kCy - 70);
    lv_obj_t* head = Rect(p, kCx - 49, kCy - 46, 98, 92, kFace, LV_RADIUS_CIRCLE);
    Border(head, 2, kPurple);
    lv_obj_t* screen = Rect(head, 0, 0, 74, 60, kFaceScreen, LV_RADIUS_CIRCLE);
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, 0);
    for (int i = 0; i < 2; i++) {
        int32_t sx = 15 + i * 28;
        Rect(screen, sx, 17, 16, 16, 0x3B2A7A, LV_RADIUS_CIRCLE);
        // Pupils sit 4 px up and glance sideways
        look_.items[i * 2] = Mask(screen, &sparkle_glow_pupil, kCyan, sx - 6, 7);
        look_.items[i * 2 + 1] = Rect(screen, sx + 4, 17, 9, 9, kCyan, LV_RADIUS_CIRCLE);
    }
    static const lv_point_precise_t kMouth[] = {{0, 2}, {12, 0}};
    lv_obj_t* mouth = lv_line_create(screen);
    lv_line_set_points(mouth, kMouth, 2);
    lv_obj_set_style_line_width(mouth, 3, 0);
    lv_obj_set_style_line_color(mouth, lv_color_hex(kPink), 0);
    lv_obj_set_style_line_rounded(mouth, true, 0);
    lv_obj_set_pos(mouth, 31, 39);

    AddTwinkle(kPageThinking, 30, 60, 10, kPurple, 2100, 0);
    AddTwinkle(kPageThinking, 280, 150, 12, kYellow, 2500, 700);
    AddTwinkle(kPageThinking, 270, 46, 8, kPink, 1900, 1100);

    // "Đang nghĩ" + bouncing dots, then the quoted request
    lv_obj_t* column = Box(p);
    lv_obj_set_size(column, 300, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, 2, 0);
    lv_obj_align(column, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_t* row = Box(column);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    think_title_ = Text(row, &sparkle_font_title_20, 0xD9CBFF, "Đang nghĩ");
    lv_obj_t* dots = Box(row);
    lv_obj_set_size(dots, 26, 16);
    static const uint32_t kThinkDots[] = {kPink, kPurple, kCyan};
    for (int i = 0; i < 3; i++) {
        think_dots_[i] = Rect(dots, i * 10, 9, 6, 6, kThinkDots[i], LV_RADIUS_CIRCLE);
    }
    think_caption_ = Text(column, &sparkle_font_text_10, 0xB5A7E0, "");
    lv_obj_set_width(think_caption_, 300);
    lv_obj_set_style_text_align(think_caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(think_caption_, -3, 0);
}

void SparkleDisplay::BuildSpeaking() {
    lv_obj_t* p = pages_[kPageSpeaking];

    // Small talking face; group origin is the halo's top-left
    speak_face_ = Box(p);
    lv_obj_set_pos(speak_face_, 50 - 58, 66 - 56);
    lv_obj_set_size(speak_face_, 116, 112);
    Mask(speak_face_, &sparkle_glow_face_xs, kCyan, 0, 0);
    lv_obj_t* head = Rect(speak_face_, 20, 20, 76, 72, kFace, LV_RADIUS_CIRCLE);
    Border(head, 2, kCyan);
    lv_obj_t* screen = Rect(head, 0, 0, 58, 48, kFaceScreen, LV_RADIUS_CIRCLE);
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, 0);
    for (int i = 0; i < 2; i++) {
        Arc(screen, 12 + i * 22, 15, 12, 180, 360, 3, kCyan);  // happy ^ ^ eyes
    }
    mouth_ = Rect(screen, 23, 26, 12, 6, kPink, 4);
    for (int i = 0; i < 2; i++) {
        lv_obj_t* cheek = Rect(head, 0, 0, 8, 5, kPink, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(cheek, 191, 0);
        lv_obj_align(cheek, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                     i == 0 ? 5 : -5, -13);
    }

    static const uint32_t kBarColors[] = {kCyan, 0x8FB8FF, kPurple, 0xE58CFF, kPink};
    lv_obj_t* bars = Box(p);
    lv_obj_set_pos(bars, 36, 110);
    lv_obj_set_size(bars, 27, 16);
    for (int i = 0; i < 5; i++) {
        speak_bars_[i] = Rect(bars, 0, 0, 3, 14, kBarColors[i], 2);
        lv_obj_align(speak_bars_[i], LV_ALIGN_LEFT_MID, i * 6, 0);
    }
    AddTwinkle(kPageSpeaking, 45, 134, 10, kYellow, 2000, 0);

    // Conversation bubbles
    chat_list_ = Box(p);
    lv_obj_add_flag(chat_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(chat_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_pos(chat_list_, 98, 28);
    lv_obj_set_size(chat_list_, 212, 202);
    lv_obj_set_flex_flow(chat_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chat_list_, 7, 0);
    // Room for the assistant bubble glow
    lv_obj_set_style_pad_all(chat_list_, 4, 0);
}

void SparkleDisplay::BuildWifi() {
    lv_obj_t* p = pages_[kPageWifi];

    Mask(p, &sparkle_glow_disc, kPurple, 2, 28);
    Rect(p, 24, 50, 92, 92, 0x1E1245, LV_RADIUS_CIRCLE);
    spinner_[0] = Arc(p, 14, 40, 112, 225, 315, 3, kPink);
    spinner_[1] = Arc(p, 14, 40, 112, 315, 45, 3, kPurple);

    // Wi-Fi glyph from the design's SVG, scaled x2.17: three arcs around (70, 113)
    static const int32_t kArcSizes[] = {70, 48, 27};
    for (int i = 0; i < 3; i++) {
        int32_t size = kArcSizes[i];
        wifi_arcs_[i] = Arc(p, 70 - size / 2, 113 - size / 2, size, 228, 312, 5, kCyan);
    }
    Rect(p, 67, 110, 6, 6, kCyan, LV_RADIUS_CIRCLE);

    AddTwinkle(kPageWifi, 110, 44, 14, kYellow, 1800, 0);
    AddTwinkle(kPageWifi, 14, 134, 10, kPink, 2200, 500);

    lv_obj_t* waiting = Text(p, &sparkle_font_title_12, 0xD9CBFF, "Chờ kết nối…");
    lv_obj_set_width(waiting, 96);
    lv_obj_set_style_text_align(waiting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(waiting, 22, 160);

    lv_obj_t* title = Text(p, &sparkle_font_title_20, kWhite, "Kết nối Wi-Fi");
    lv_obj_set_pos(title, 138, 28);

    static const uint32_t kBadgeColors[] = {kPink, kPurple, kCyan};
    static const char* const kSteps[] = {"Dùng điện thoại vào Wi-Fi", "Mở trình duyệt, vào",
                                         "Chọn Wi-Fi nhà bạn và nhập mật khẩu"};
    for (int i = 0; i < 3; i++) {
        int32_t y = 64 + i * 38;
        lv_obj_t* badge = Rect(p, 138, y, 18, 18, kBadgeColors[i], LV_RADIUS_CIRCLE);
        char number[2] = {static_cast<char>('1' + i), 0};
        lv_obj_center(Text(badge, &sparkle_font_text_10, 0x1E1240, number));
        lv_obj_t* step = Text(p, &sparkle_font_text_10, 0xE6DDFF, kSteps[i]);
        lv_obj_set_pos(step, 164, y);
        lv_obj_set_width(step, 144);
        lv_obj_set_style_text_line_space(step, -3, 0);
        if (i < 2) {
            lv_obj_t* value = Text(p, &sparkle_font_text_10, kCyan, "");
            lv_obj_set_pos(value, 164, y + 14);
            lv_obj_set_width(value, 144);
            lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
            (i == 0 ? wifi_ssid_ : wifi_url_) = value;
        }
    }

    lv_obj_t* pill = Rect(p, 138, 202, 170, 26, kPillFill, 13);
    Border(pill, 1, kPillBorder);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 2, 0);
    lv_obj_t* star = Mask(pill, &sparkle_star_16, kYellow, 0, 0);
    lv_image_set_scale(star, 9 * 256 / 16);
    wifi_code_ = Text(pill, &sparkle_font_text_10, kSubText, "");
}

// ---------------------------------------------------------------- animation control

void SparkleDisplay::Animate(void* var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                             uint32_t duration, uint32_t delay, bool reverse,
                             lv_anim_path_cb_t path) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    if (reverse) {
        lv_anim_set_reverse_duration(&a, duration);
    }
    lv_anim_start(&a);
    anim_vars_.push_back(var);
}

void SparkleDisplay::StopAnimations() {
    for (void* var : anim_vars_) {
        lv_anim_delete(var, nullptr);
    }
    anim_vars_.clear();
}

void SparkleDisplay::StartAnimations(Page page) {
    for (size_t i = 0; i < twinkles_[page].size(); i++) {
        uint32_t timing = twinkle_timing_[page][i];
        Animate(twinkles_[page][i], TwinkleCb, 0, 256, (timing >> 16) / 2, timing & 0xFFFF, true);
    }

    switch (page) {
        case kPageIdle:
            Animate(idle_face_, TranslateYCb, 0, -4, 1600, 0, true);
            Animate(idle_antenna_star_, ImageOpaCb, 140, 255, 800, 0, true);
            idle_blink_.eyes[0].last_height = idle_blink_.eyes[1].last_height = -1;
            Animate(&idle_blink_, BlinkCb, 0, 4499, 4500, 0, false, lv_anim_path_linear);
            break;
        case kPageListening: {
            for (int i = 0; i < 2; i++) {
                Animate(rings_[i], RingCb, 0, 1000, 1800, i * 900, false, lv_anim_path_ease_out);
            }
            static const uint32_t kPhase[] = {0, 150, 300, 450, 600, 450, 300, 150, 0};
            for (int i = 0; i < 9; i++) {
                Animate(listen_bars_[i], HeightCb, 12, 46, 450, kPhase[i], true);
            }
            Animate(caret_, OpaCb, 0, 255, 500, 0, true, lv_anim_path_step);
            Animate(mic_dot_, OpaCb, 0, 255, 500, 0, true, lv_anim_path_step);
            break;
        }
        case kPageThinking:
            Animate(&stars_orbit_, OrbitCb, 0, 359, 6000, 0, false, lv_anim_path_linear);
            Animate(&dots_orbit_, OrbitCb, 0, 359, 4000, 0, false, lv_anim_path_linear);
            look_.last = INT32_MIN;
            Animate(&look_, LookCb, 0, 2999, 3000, 0, false, lv_anim_path_linear);
            for (int i = 0; i < 3; i++) {
                Animate(think_dots_[i], DotCb, 0, 1199, 1200, i * 200, false, lv_anim_path_linear);
            }
            break;
        case kPageSpeaking:
            Animate(speak_face_, TranslateYCb, 0, -3, 1200, 0, true);
            Animate(mouth_, HeightCb, 3, 10, 175, 0, true);
            for (int i = 0; i < 5; i++) {
                Animate(speak_bars_[i], HeightCb, 4, 14, 350, i * 100, true);
            }
            break;
        case kPageWifi:
            for (auto arc : spinner_) {
                Animate(arc, ArcRotationCb, 0, 359, 1600, 0, false, lv_anim_path_linear);
            }
            for (int i = 0; i < 3; i++) {
                Animate(wifi_arcs_[i], OpaCb, 64, 255, 750, (2 - i) * 300, true);
            }
            break;
        default:
            break;
    }
}

void SparkleDisplay::ShowPage(Page page) {
    if (page == page_) {
        return;
    }
    StopAnimations();
    if (page_ != kPageNone) {
        lv_obj_add_flag(pages_[page_], LV_OBJ_FLAG_HIDDEN);
    }
    page_ = page;
    lv_obj_remove_flag(pages_[page_], LV_OBJ_FLAG_HIDDEN);
    StartAnimations(page_);

    // Top bar: brand on idle, "Cài đặt" in setup, the time elsewhere
    bool idle = page_ == kPageIdle;
    if (idle) {
        lv_obj_remove_flag(bar_star_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bar_star_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(bar_title_, LV_ALIGN_LEFT_MID, idle ? 23 : 10, 0);
    if (page_ == kPageListening) {
        lv_obj_remove_flag(mic_pill_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mic_pill_, LV_OBJ_FLAG_HIDDEN);
    }
    if (page_ == kPageWifi) {
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        UpdateWifiInfo();
    } else {
        lv_obj_remove_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    }
    clock_minute_ = -2;  // re-render the bar title
}

// ---------------------------------------------------------------- state and text

void SparkleDisplay::OnTick() {
    auto state = Application::GetInstance().GetDeviceState();
    int64_t now = esp_timer_get_time();
    if (state != last_state_) {
        if (state == kDeviceStateListening) {
            listening_since_us_ = now;
        }
        last_state_ = state;
        texts_dirty_ = true;
    }

    Page page;
    switch (state) {
        case kDeviceStateIdle:
            page = kPageIdle;
            break;
        case kDeviceStateWifiConfiguring:
            page = kPageWifi;
            break;
        case kDeviceStateListening:
            // Once the transcript is in, the server is working on the answer
            page = user_text_us_ > listening_since_us_ && now - user_text_us_ > kThinkingDelayUs
                       ? kPageThinking
                       : kPageListening;
            break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            page = kPageSpeaking;
            break;
        default:  // starting, connecting, activating, upgrading, errors
            page = kPageThinking;
            break;
    }
    ShowPage(page);

    if (texts_dirty_) {
        texts_dirty_ = false;
        UpdateTexts(state);
    }
    if (page_ == kPageWifi) {
        UpdateWifiInfo();
    }
    UpdateClock();
}

void SparkleDisplay::UpdateClock() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    bool valid = t.tm_year >= 2025 - 1900;
    int minute = valid ? t.tm_hour * 60 + t.tm_min : -1;
    if (minute == clock_minute_) {
        return;
    }
    clock_minute_ = minute;

    char hhmm[8] = "--:--";
    char date[40] = "";
    if (valid) {
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
        snprintf(date, sizeof(date), "%s · %02d/%02d", kWeekdays[t.tm_wday], t.tm_mday,
                 t.tm_mon + 1);
    }
    SetTextIfChanged(clock_label_, hhmm);
    SetTextIfChanged(date_label_, date);
    const char* title = hhmm;
    if (page_ == kPageIdle) {
        title = "Sparkle";
    } else if (page_ == kPageWifi) {
        title = "Cài đặt";
    } else if (!valid) {
        title = "";
    }
    SetTextIfChanged(bar_title_, title);
}

void SparkleDisplay::UpdateTexts(DeviceState state) {
    // Thinking page doubles as the "busy" page for boot, activation, upgrade
    bool conversation = state == kDeviceStateListening || state == kDeviceStateConnecting;
    const char* status = lv_label_get_text(status_label_);
    SetTextIfChanged(think_title_, conversation ? "Đang nghĩ" : status);
    std::string caption;
    if (conversation && !user_text_.empty()) {
        caption = "“" + user_text_ + "”";
    } else {
        caption = system_text_;
    }
    SetTextIfChanged(think_caption_, caption.c_str());

    bool heard = user_text_us_ > listening_since_us_ && !user_text_.empty();
    const char* transcript = heard ? user_text_.c_str() : kListeningPlaceholder;
    lv_obj_set_style_text_color(transcript_label_, lv_color_hex(heard ? kText : kSubText), 0);
    FitLabel(transcript_label_, transcript, 236, 0);

    // Alerts replace the idle hint until the application clears them
    lv_label_set_recolor(hint_label_, system_text_.empty());
    SetTextIfChanged(hint_label_, system_text_.empty() ? kIdleHint : system_text_.c_str());

    std::string ssid = WifiManager::GetInstance().GetSsid();
    SetTextIfChanged(info_label_, ssid.empty() ? "Chưa có Wi-Fi" : ssid.c_str());
}

void SparkleDisplay::UpdateWifiInfo() {
    auto& wifi = WifiManager::GetInstance();
    std::string ssid = wifi.GetApSsid();
    std::string url = wifi.GetApWebUrl();
    if (url.rfind("http://", 0) == 0) {
        url = url.substr(7);
    }
    SetTextIfChanged(wifi_ssid_, ssid.c_str());
    SetTextIfChanged(wifi_url_, url.c_str());

    // The server identifies the device by its MAC; show the last 3 bytes
    std::string mac = SystemInfo::GetMacAddress();
    std::string code;
    for (char ch : mac) {
        if (ch != ':') {
            code += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        }
    }
    if (code.size() > 6) {
        code = code.substr(code.size() - 6);
    }
    SetTextIfChanged(wifi_code_, ("Mã thiết bị: " + code).c_str());
}

void SparkleDisplay::AddBubble(bool user, const char* text) {
    while (lv_obj_get_child_count(chat_list_) >= kMaxBubbles) {
        lv_obj_delete(lv_obj_get_child(chat_list_, 0));
    }

    lv_obj_t* row = Box(chat_list_);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(user ? kRoleUser : kRoleAssistant));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* bubble = lv_label_create(row);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_set_style_pad_hor(bubble, 10, 0);
    lv_obj_set_style_text_line_space(bubble, -3, 0);
    if (user) {
        lv_obj_set_style_text_font(bubble, &sparkle_font_text_10, 0);
        lv_obj_set_style_text_color(bubble, lv_color_hex(0xEDE6FF), 0);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x3A2580), 0);
        lv_obj_set_style_pad_ver(bubble, 5, 0);
        FitLabel(bubble, text, 180, 20);
    } else {
        lv_obj_set_style_text_font(bubble, &sparkle_font_text_12, 0);
        lv_obj_set_style_text_color(bubble, lv_color_hex(0x1E1240), 0);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(kText), 0);
        lv_obj_set_style_pad_ver(bubble, 6, 0);
        lv_obj_set_style_shadow_width(bubble, 12, 0);
        lv_obj_set_style_shadow_color(bubble, lv_color_hex(kCyan), 0);
        lv_obj_set_style_shadow_opa(bubble, 90, 0);
        FitLabel(bubble, text, 196, 20);
    }

    lv_obj_update_layout(chat_list_);
    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

void SparkleDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_list_ == nullptr || role == nullptr || content == nullptr) {
        return;
    }
    texts_dirty_ = true;
    if (strcmp(role, "system") == 0) {
        system_text_ = content;
        return;
    }
    if (content[0] == '\0') {
        return;
    }
    bool user = strcmp(role, "user") == 0;
    if (user) {
        user_text_ = content;
        user_text_us_ = esp_timer_get_time();
    }
    AddBubble(user, content);
}

void SparkleDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_list_ == nullptr || role == nullptr || content == nullptr) {
        return;
    }
    if (strcmp(role, "system") == 0) {
        if (system_text_ == content) {
            system_text_.clear();
            texts_dirty_ = true;
        }
        return;
    }
    intptr_t wanted = strcmp(role, "user") == 0 ? kRoleUser : kRoleAssistant;
    for (uint32_t i = 0; i < lv_obj_get_child_count(chat_list_); i++) {
        lv_obj_t* row = lv_obj_get_child(chat_list_, i);
        lv_obj_t* bubble = lv_obj_get_child(row, 0);
        if (reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)) == wanted && bubble != nullptr &&
            strcmp(lv_label_get_text(bubble), content) == 0) {
            lv_obj_delete(row);
            return;
        }
    }
}

void SparkleDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (chat_list_ == nullptr) {
        return;
    }
    lv_obj_clean(chat_list_);
    user_text_.clear();
    system_text_.clear();
    texts_dirty_ = true;
}

void SparkleDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    texts_dirty_ = true;
}

void SparkleDisplay::SetEmotion(const char* emotion) {
    // Faces follow the device state; emotions have no separate artwork
    (void)emotion;
}

void SparkleDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    // No camera on this board; drop the image
    (void)image;
}

void SparkleDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    Display::SetTheme(theme);
    ApplyFallbackFont();
    if (setup_ui_called_) {
        lv_obj_invalidate(lv_screen_active());
    }
}

void SparkleDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);

    int level = 0;
    bool charging = false;
    bool discharging = false;
    bool has_battery = Board::GetInstance().GetBatteryLevel(level, charging, discharging);
    DisplayLockGuard lock(this);
    if (battery_box_ == nullptr) {
        return;
    }
    auto nub = static_cast<lv_obj_t*>(lv_obj_get_user_data(battery_box_));
    if (!has_battery) {
        lv_obj_add_flag(battery_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(nub, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(battery_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(nub, LV_OBJ_FLAG_HIDDEN);
    level = std::clamp(level, 0, 100);
    int32_t width = std::max<int32_t>(1, 14 * level / 100);
    if (lv_obj_get_width(battery_fill_) != width) {
        lv_obj_set_width(battery_fill_, width);
    }
    uint32_t color = charging ? kYellow : (level <= 20 ? kPink : kGreen);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(color), 0);
}

#include "jolly_chat_display.h"
#include "assets/lang_config.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <sdkconfig.h>
#include <noto_emoji.h>
#include <algorithm>
#include <cstring>

#if !CONFIG_LV_FONT_UNSCII_8 || !CONFIG_LV_FONT_UNSCII_16
#error "JollyChatDisplay needs CONFIG_LV_FONT_UNSCII_8 and CONFIG_LV_FONT_UNSCII_16"
#endif

#define TAG "JollyChatDisplay"

#define MAX_MESSAGES 20

namespace {

// Dark Instagram / Messenger palette
constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kDivider = 0x262626;
constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kSubText = 0xA8A8A8;
constexpr uint32_t kAssistantBubble = 0x262626;
constexpr uint32_t kUserBubbleStart = 0x8B3DFF;  // purple
constexpr uint32_t kUserBubbleEnd = 0x1E8CFF;    // blue
constexpr uint32_t kJollyTop = 0xFFD84A;         // mascot yellow
constexpr uint32_t kJollyBottom = 0xF0821E;      // mascot orange
constexpr uint32_t kAvatarInk = 0x1A1A1A;

constexpr int kHeaderHeight = 40;
constexpr int kHeaderAvatar = 32;
constexpr int kMessageAvatar = 22;
constexpr int kAsciiRowHeight = 8;

// Role tags stored as LVGL user data, so they must outlive the objects
const char kRoleProfile[] = "profile";
const char kRoleUser[] = "user";
const char kRoleAssistant[] = "assistant";
const char kRoleSystem[] = "system";
const char kRoleImage[] = "image";

// Jolly, the muse.ai gadgets mascot (https://gadgets.muse.ai/), shrunk from
// the 89x56 original to 16x16 cells of the 8 px UNSCII font (128x128 px).
const char* const kJollyArt[] = {
    "     .8888.",
    "    UUU!!888",
    "   .|U688B@8.",
    "   .UU8888BB8",
    "   UU  88  8@",
    "   UUUUU8d$88",
    "   cUVUU8888@",
    "  .|UcUUdU888.",
    "  }zVUdc388$88",
    " OU!UUdUU$888d@",
    " U!UUU(ddU888c8",
    ".U!cUUUUd888BcU.",
    "Ud.UdUUU688$8ddB",
    "YU UUUdUddUd$ c.",
    "   .zUUUUdU8.",
    "   .U)!..:(6.",
};
constexpr int kJollyArtRows = sizeof(kJollyArt) / sizeof(kJollyArt[0]);

lv_color_t Mix(uint32_t from, uint32_t to, int step, int steps) {
    uint8_t ratio = steps > 1 ? step * 255 / (steps - 1) : 0;
    return lv_color_mix(lv_color_hex(to), lv_color_hex(from), ratio);
}

void MakePlain(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Shrink-wrap a label to its text, wrapping only when it exceeds max_width
void FitLabel(lv_obj_t* label, const char* text, int max_width) {
    const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, size.x < max_width ? size.x + 1 : max_width);
    lv_label_set_text(label, text);
}

// The label of a message row (system rows hold it directly, others in the bubble)
lv_obj_t* RowLabel(lv_obj_t* row) {
    uint32_t count = lv_obj_get_child_cnt(row);
    if (count == 0) {
        return nullptr;
    }
    auto role = static_cast<const char*>(lv_obj_get_user_data(row));
    if (role == kRoleSystem) {
        return lv_obj_get_child(row, 0);
    }
    lv_obj_t* bubble = lv_obj_get_child(row, count - 1);
    return lv_obj_get_child_cnt(bubble) > 0 ? lv_obj_get_child(bubble, 0) : nullptr;
}

}  // namespace

JollyChatDisplay::JollyChatDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

lv_obj_t* JollyChatDisplay::CreateAvatar(lv_obj_t* parent, int size) {
    lv_obj_t* avatar = lv_obj_create(parent);
    MakePlain(avatar);
    lv_obj_set_size(avatar, size, size);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(kJollyTop), 0);
    lv_obj_set_style_bg_grad_color(avatar, lv_color_hex(kJollyBottom), 0);
    lv_obj_set_style_bg_grad_dir(avatar, LV_GRAD_DIR_VER, 0);

    lv_obj_t* label = lv_label_create(avatar);
    lv_obj_set_style_text_font(label, size >= 28 ? &lv_font_unscii_16 : &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kAvatarInk), 0);
    lv_label_set_text(label, "J");
    lv_obj_center(label);
    return avatar;
}

void JollyChatDisplay::CreateProfileCard() {
    lv_obj_t* card = lv_obj_create(content_);
    MakePlain(card);
    lv_obj_set_user_data(card, (void*)kRoleProfile);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* art = lv_obj_create(card);
    MakePlain(art);
    lv_obj_set_size(art, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(art, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < kJollyArtRows; i++) {
        lv_obj_t* line = lv_label_create(art);
        lv_obj_set_style_text_font(line, &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(line, Mix(kJollyTop, kJollyBottom, i, kJollyArtRows), 0);
        lv_obj_set_height(line, kAsciiRowHeight);  // UNSCII is 9 px tall, keep the art square
        lv_label_set_text_static(line, kJollyArt[i]);
    }

    lv_obj_t* info = lv_obj_create(card);
    MakePlain(info);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info, 6, 0);

    lv_obj_t* title = lv_label_create(info);
    lv_obj_set_style_text_font(title, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kJollyTop), 0);
    lv_label_set_text_static(title, "JOLLY");

    card_brand_ = lv_obj_create(info);
    MakePlain(card_brand_);
    lv_obj_set_size(card_brand_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card_brand_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_brand_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card_brand_, 6, 0);

    lv_obj_t* tagline = lv_label_create(card_brand_);
    lv_obj_set_style_text_font(tagline, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(tagline, lv_color_hex(kJollyBottom), 0);
    lv_label_set_text_static(tagline, "bone-eyes");

    lv_obj_t* site = lv_label_create(card_brand_);
    lv_obj_set_style_text_font(site, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(site, lv_color_hex(kSubText), 0);
    lv_label_set_text_static(site, "muse gadgets");

    // Before the chat starts, system hints (Wi-Fi setup, activation code...)
    // sit beside the logo so the whole mascot stays on screen
    card_note_ = lv_label_create(info);
    lv_obj_set_style_text_color(card_note_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(card_note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(card_note_, "");
    lv_obj_add_flag(card_note_, LV_OBJ_FLAG_HIDDEN);
}

void JollyChatDisplay::SetCardNote(const char* text) {
    if (card_note_ == nullptr) {
        return;
    }
    if (text == nullptr || text[0] == '\0') {
        if (chat_message_label_ == card_note_) {
            chat_message_label_ = nullptr;
        }
        lv_label_set_text(card_note_, "");
        lv_obj_add_flag(card_note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(card_brand_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(card_brand_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(card_note_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(content_);
    lv_obj_t* info = lv_obj_get_parent(card_note_);
    FitLabel(card_note_, text, lv_obj_get_content_width(info));
    chat_message_label_ = card_note_;
}

void JollyChatDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);

    container_ = lv_obj_create(screen);
    MakePlain(container_);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kBackground), 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);

    /* Header: avatar, name, live status and status icons */
    top_bar_ = lv_obj_create(container_);
    MakePlain(top_bar_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, kHeaderHeight);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_border_color(top_bar_, lv_color_hex(kDivider), 0);
    lv_obj_set_style_pad_hor(top_bar_, 6, 0);
    lv_obj_set_style_pad_column(top_bar_, 6, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* avatar = CreateAvatar(top_bar_, kHeaderAvatar);
    avatar_label_ = lv_obj_get_child(avatar, 0);

    name_label_ = lv_label_create(top_bar_);
    lv_label_set_text_static(name_label_, "Jolly");

    status_label_ = lv_label_create(top_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(kSubText), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(top_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(notification_label_, lv_color_hex(kJollyTop), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(top_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(top_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Conversation: profile card followed by message rows */
    content_ = lv_obj_create(container_);
    MakePlain(content_);
    lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_style_pad_hor(content_, 8, 0);
    lv_obj_set_style_pad_bottom(content_, 6, 0);
    lv_obj_set_style_pad_row(content_, 6, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    CreateProfileCard();
    chat_message_label_ = nullptr;

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0xED4956), 0);
    lv_obj_set_style_border_width(low_battery_popup_, 0, 0);
    lv_obj_set_style_radius(low_battery_popup_, 16, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* JollyChatDisplay::CreateRow(const char* role) {
    lv_obj_t* row = lv_obj_create(content_);
    MakePlain(row);
    lv_obj_set_user_data(row, (void*)role);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_flex_align_t main_align = LV_FLEX_ALIGN_START;
    if (role == kRoleUser) {
        main_align = LV_FLEX_ALIGN_END;
    } else if (role == kRoleSystem) {
        main_align = LV_FLEX_ALIGN_CENTER;
    }
    // Assistant avatar sits at the bottom of its bubble, like Messenger
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    return row;
}

void JollyChatDisplay::TrimMessages() {
    // Child 0 is the profile card and is never trimmed
    while (lv_obj_get_child_cnt(content_) > MAX_MESSAGES) {
        lv_obj_t* oldest = lv_obj_get_child(content_, 1);
        if (chat_message_label_ != nullptr && RowLabel(oldest) == chat_message_label_) {
            chat_message_label_ = nullptr;
        }
        lv_obj_delete(oldest);
    }
}

void JollyChatDisplay::ScrollToLatest(lv_obj_t* row) {
    lv_obj_update_layout(content_);
    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

void JollyChatDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s') called before SetupUI() - message will be lost!", role);
    }
    DisplayLockGuard lock(this);
    if (content_ == nullptr || role == nullptr) {
        return;
    }

    const char* tag = kRoleSystem;
    if (strcmp(role, "user") == 0) {
        tag = kRoleUser;
    } else if (strcmp(role, "assistant") == 0) {
        tag = kRoleAssistant;
    }

    if (tag == kRoleSystem && lv_obj_get_child_cnt(content_) == 1) {
        // Only the profile card so far: show the hint next to the logo
        SetCardNote(content);
        lv_obj_scroll_to_y(content_, 0, LV_ANIM_ON);
        return;
    }
    if (tag != kRoleSystem) {
        SetCardNote(nullptr);
    }

    // Only the latest system line is kept, the way chat apps show a status chip
    if (tag == kRoleSystem) {
        lv_obj_t* last = lv_obj_get_child(content_, -1);
        if (last != nullptr && lv_obj_get_user_data(last) == kRoleSystem) {
            if (chat_message_label_ == RowLabel(last)) {
                chat_message_label_ = nullptr;
            }
            lv_obj_delete(last);
        }
    }

    if (content == nullptr || content[0] == '\0') {
        return;
    }

    const int max_text_width = LV_HOR_RES * 70 / 100 - 20;
    lv_obj_t* row = CreateRow(tag);
    lv_obj_t* label = nullptr;

    if (tag == kRoleSystem) {
        label = lv_label_create(row);
        lv_obj_set_style_text_color(label, lv_color_hex(kSubText), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        // Full row width keeps long hints (e.g. Wi-Fi setup) to a couple of lines
        FitLabel(label, content, lv_obj_get_content_width(content_));
    } else {
        if (tag == kRoleAssistant) {
            CreateAvatar(row, kMessageAvatar);
        }
        lv_obj_t* bubble = lv_obj_create(row);
        MakePlain(bubble);
        lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble, 16, 0);
        lv_obj_set_style_pad_hor(bubble, 10, 0);
        lv_obj_set_style_pad_ver(bubble, 5, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        if (tag == kRoleUser) {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(kUserBubbleStart), 0);
            lv_obj_set_style_bg_grad_color(bubble, lv_color_hex(kUserBubbleEnd), 0);
            lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_HOR, 0);
        } else {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(kAssistantBubble), 0);
        }

        label = lv_label_create(bubble);
        lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
        FitLabel(label, content, max_text_width);
    }

    chat_message_label_ = label;
    TrimMessages();
    ScrollToLatest(row);
}

void JollyChatDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr || role == nullptr || content == nullptr) {
        return;
    }
    if (strcmp(role, "system") == 0 && card_note_ != nullptr &&
        strcmp(lv_label_get_text(card_note_), content) == 0) {
        SetCardNote(nullptr);
        return;
    }
    for (uint32_t i = 1; i < lv_obj_get_child_cnt(content_); i++) {
        lv_obj_t* row = lv_obj_get_child(content_, i);
        auto row_role = static_cast<const char*>(lv_obj_get_user_data(row));
        lv_obj_t* label = RowLabel(row);
        if (row_role == nullptr || label == nullptr || strcmp(row_role, role) != 0 ||
            strcmp(lv_label_get_text(label), content) != 0) {
            continue;
        }
        if (chat_message_label_ == label) {
            chat_message_label_ = nullptr;
        }
        lv_obj_delete(row);
        return;
    }
}

void JollyChatDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }
    lv_obj_clean(content_);
    chat_message_label_ = nullptr;
    CreateProfileCard();
    lv_obj_scroll_to_y(content_, 0, LV_ANIM_OFF);
}

void JollyChatDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr || image == nullptr) {
        return;
    }

    auto img_dsc = image->image_dsc();
    int32_t img_width = img_dsc->header.w;
    int32_t img_height = img_dsc->header.h;
    if (img_width == 0 || img_height == 0) {
        ESP_LOGW(TAG, "Invalid preview image size %ldx%ld", img_width, img_height);
        return;
    }

    lv_obj_t* row = CreateRow(kRoleImage);
    CreateAvatar(row, kMessageAvatar);

    lv_obj_t* bubble = lv_obj_create(row);
    MakePlain(bubble);
    lv_obj_set_style_radius(bubble, 16, 0);
    lv_obj_set_style_clip_corner(bubble, true, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(kAssistantBubble), 0);

    int32_t max_width = LV_HOR_RES * 60 / 100;
    int32_t max_height = LV_VER_RES * 60 / 100;
    int32_t zoom = std::min(max_width * 256 / img_width, max_height * 256 / img_height);
    zoom = std::min<int32_t>(zoom, 256);

    lv_obj_t* preview = lv_image_create(bubble);
    lv_image_set_src(preview, img_dsc);
    lv_image_set_scale(preview, zoom);
    lv_obj_set_size(bubble, img_width * zoom / 256, img_height * zoom / 256);
    lv_obj_center(preview);

    // The LVGL object owns the image from now on
    lv_obj_add_event_cb(
        preview, [](lv_event_t* e) { delete static_cast<LvglImage*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, image.release());

    TrimMessages();
    ScrollToLatest(row);
}

void JollyChatDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (avatar_label_ == nullptr || emotion == nullptr) {
        return;
    }
    // The header avatar shows Jolly's "J" when calm and the emotion emoji otherwise
    const char* utf8 = strcmp(emotion, "neutral") == 0 ? nullptr : noto_emoji_get_utf8(emotion);
    if (utf8 == nullptr) {
        lv_obj_set_style_text_font(avatar_label_, &lv_font_unscii_16, 0);
        lv_label_set_text(avatar_label_, "J");
    } else {
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        lv_obj_set_style_text_font(avatar_label_, lvgl_theme->emoji_font()->font(), 0);
        lv_label_set_text(avatar_label_, utf8);
    }
    lv_obj_center(avatar_label_);
}

void JollyChatDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    // The chat palette is fixed dark; only rebind fonts (e.g. after assets load)
    if (setup_ui_called_) {
        lv_obj_set_style_text_font(lv_screen_active(), lvgl_theme->text_font()->font(), 0);
        auto icon_font = lvgl_theme->icon_font()->font();
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    }
    Display::SetTheme(theme);
}

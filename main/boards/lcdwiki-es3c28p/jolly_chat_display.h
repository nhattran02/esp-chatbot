#ifndef JOLLY_CHAT_DISPLAY_H
#define JOLLY_CHAT_DISPLAY_H

#include "display/lcd_display.h"

// Landscape Messenger / Instagram DM style chat UI with the Jolly (muse.ai)
// ASCII mascot as the conversation's profile card.
class JollyChatDisplay : public SpiLcdDisplay {
public:
    JollyChatDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);

    virtual void SetupUI() override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetTheme(Theme* theme) override;

private:
    lv_obj_t* avatar_label_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* card_brand_ = nullptr;
    lv_obj_t* card_note_ = nullptr;

    lv_obj_t* CreateAvatar(lv_obj_t* parent, int size);
    void CreateProfileCard();
    void SetCardNote(const char* text);
    lv_obj_t* CreateRow(const char* role);
    void TrimMessages();
    void ScrollToLatest(lv_obj_t* row);
};

#endif  // JOLLY_CHAT_DISPLAY_H

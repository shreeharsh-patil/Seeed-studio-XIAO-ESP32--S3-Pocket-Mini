#pragma once

#include <esp_lcd_panel_ops.h>
#include <array>
#include "lvgl_display.h"
#include "lvgl_theme.h"

// Uses the existing display/font abstraction with a single fixed UI tree.
class PocketDisplay : public LvglDisplay {
public:
    PocketDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel);
    ~PocketDisplay() override;
    bool Ready() const { return display_ != nullptr; }
    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* text) override;
    void ClearChatMessages() override;
    void SetActivationCode(const std::string& code) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetTheme(Theme* theme) override;
    bool SetTextFont(std::shared_ptr<LvglFont> font) override;
    bool SupportsGuiOperations() const override { return false; }
    void WaitForAnswer();

protected:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

private:
    enum class Animation { Idle, Listen, Think, Speak, Error };
    Animation animation_ = Animation::Idle;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* code_ = nullptr;
    lv_obj_t* face_ = nullptr;
    std::array<lv_obj_t*, 2> eyes_{};
    std::array<lv_obj_t*, 5> bars_{};
    lv_timer_t* animation_timer_ = nullptr;
    uint32_t phase_ = 0;
    bool waiting_ = false;
    int64_t waiting_since_ = 0;
    LvglTheme theme_{"pocket"};
    lv_obj_t* Label(int x, int y, int width);
    void Animate();
};

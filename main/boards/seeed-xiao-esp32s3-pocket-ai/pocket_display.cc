#include "pocket_display.h"
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <material_symbols.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <wifi_manager.h>
#include <algorithm>
#include <cstring>
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "config.h"
#include "lvgl_font.h"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

PocketDisplay::PocketDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel) {
    width_ = TFT_WIDTH;
    height_ = TFT_HEIGHT;
    theme_.set_text_font(std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT));
    theme_.set_icon_font(std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT));
    current_theme_ = &theme_;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_priority = 1;
    port.task_affinity = 1;
    if (lvgl_port_init(&port) != ESP_OK)
        return;
    // The port initializes LVGL, including after a failed display was deinitialized.
    lvgl_port_lock(0);
    lv_image_cache_resize(0, true);  // geometric animation, no decoded image cache
    lvgl_port_unlock();
    lvgl_port_display_cfg_t cfg{};
    cfg.io_handle = io;
    cfg.panel_handle = panel;
    cfg.buffer_size = TFT_WIDTH * 20;
    cfg.hres = TFT_WIDTH;
    cfg.vres = TFT_HEIGHT;
    cfg.color_format = LV_COLOR_FORMAT_RGB565;
    cfg.flags.buff_dma = true;
    cfg.flags.swap_bytes = true;
    // One 9.6 KB internal DMA strip; LVGL objects/font storage use PSRAM pool.
    display_ = lvgl_port_add_disp(&cfg);
    if (display_)
        lv_display_set_offset(display_, TFT_OFFSET_X, TFT_OFFSET_Y);
    else
        lvgl_port_deinit();
}

bool PocketDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void PocketDisplay::Unlock() { lvgl_port_unlock(); }

PocketDisplay::~PocketDisplay() {
    if (animation_timer_) {
        DisplayLockGuard lock(this);
        if (lock)
            lv_timer_delete(animation_timer_);
    }
}

lv_obj_t* PocketDisplay::Label(int x, int y, int width) {
    auto label = lv_label_create(lv_display_get_screen_active(display_));
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, "");
    return label;
}

void PocketDisplay::SetupUI() {
    if (!Ready() || setup_ui_called_)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    auto screen = lv_display_get_screen_active(display_);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x09131e), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xeaf5ff), 0);
    lv_obj_set_style_text_font(screen, &BUILTIN_TEXT_FONT, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    title_ = Label(8, 8, 176);
    lv_label_set_text(title_, "Pocket AI");
    network_label_ = Label(207, 8, 26);
    lv_obj_set_style_text_font(network_label_, &BUILTIN_ICON_FONT, 0);
    status_label_ = Label(8, 42, 224);
    notification_label_ = Label(8, 42, 224);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    face_ = lv_obj_create(screen);
    lv_obj_set_pos(face_, 48, 82);
    lv_obj_set_size(face_, 144, 64);
    lv_obj_set_style_bg_opa(face_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_, 0, 0);
    lv_obj_set_style_pad_all(face_, 0, 0);
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t i = 0; i < eyes_.size(); ++i) {
        eyes_[i] = lv_obj_create(face_);
        lv_obj_set_pos(eyes_[i], 24 + i * 68, 16);
        lv_obj_set_size(eyes_[i], 28, 32);
        lv_obj_set_style_radius(eyes_[i], 12, 0);
        lv_obj_set_style_border_width(eyes_[i], 0, 0);
        lv_obj_set_style_bg_color(eyes_[i], lv_color_hex(0x6de8cb), 0);
    }
    for (size_t i = 0; i < bars_.size(); ++i) {
        bars_[i] = lv_obj_create(screen);
        lv_obj_set_pos(bars_[i], 76 + i * 20, 149);
        lv_obj_set_size(bars_[i], 8, 5);
        lv_obj_set_style_radius(bars_[i], 4, 0);
        lv_obj_set_style_border_width(bars_[i], 0, 0);
        lv_obj_set_style_bg_color(bars_[i], lv_color_hex(0x6de8cb), 0);
    }
    code_ = Label(8, 83, 224);
    lv_obj_set_style_text_letter_space(code_, 4, 0);
    lv_obj_add_flag(code_, LV_OBJ_FLAG_HIDDEN);
    message_ = Label(8, 180, 224);
    lv_obj_set_height(message_, 90);
    lv_obj_set_style_text_color(message_, lv_color_hex(0xabc2d6), 0);
    setup_ui_called_ = true;
    lv_label_set_text(status_label_, "Initializing...");
    lv_label_set_text(message_, "Display ready\nPSRAM checked at boot\nStarting audio / Wi-Fi");
    animation_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<PocketDisplay*>(lv_timer_get_user_data(timer))->Animate();
        },
        180, this);
}

void PocketDisplay::Animate() {
    ++phase_;
    for (size_t i = 0; i < eyes_.size(); ++i) {
        if (animation_ == Animation::Listen) {
            // Reuse the two eye objects as a pulsing mic capsule and base.
            lv_obj_set_pos(eyes_[i], i ? 56 : 62, i ? 46 : 10);
            lv_obj_set_size(eyes_[i], i ? 32 : 20, i ? 4 : 24 + phase_ % 4);
        } else {
            auto h = animation_ == Animation::Error ? 12 : (phase_ % 28 == 0 ? 5 : 32);
            lv_obj_set_pos(eyes_[i], 24 + i * 68, 16);
            lv_obj_set_size(eyes_[i], 28, h);
        }
    }
    for (size_t i = 0; i < bars_.size(); ++i) {
        int h = 5;
        if (animation_ == Animation::Listen || animation_ == Animation::Speak)
            h += ((phase_ + i * 3) % 7) * 3;
        else if (animation_ == Animation::Think)
            h = (phase_ % 5 == i) ? 16 : 5;
        lv_obj_set_height(bars_[i], h);
        lv_obj_set_y(bars_[i], 172 - h);
    }
}

void PocketDisplay::SetStatus(const char* status) {
    if (!setup_ui_called_ || !status)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    if (!strcmp(status, Lang::Strings::LISTENING)) {
        status = "Listening...";
        animation_ = Animation::Listen;
        waiting_ = false;
    } else if (!strcmp(status, Lang::Strings::SPEAKING)) {
        status = "Speaking...";
        animation_ = Animation::Speak;
        waiting_ = false;
    } else if (!strcmp(status, Lang::Strings::CONNECTING)) {
        status = "Connecting...";
        animation_ = Animation::Think;
    } else if (!strcmp(status, Lang::Strings::STANDBY)) {
        status = waiting_ ? "Thinking..." : "Ready";
        animation_ = waiting_ ? Animation::Think : Animation::Idle;
        lv_obj_add_flag(code_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_HIDDEN);
    } else if (!strcmp(status, Lang::Strings::ACTIVATION)) {
        status = "Activation required";
    }
    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void PocketDisplay::SetEmotion(const char* emotion) {
    if (!setup_ui_called_ || !emotion)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    if (!strcmp(emotion, "cloud_off") || !strcmp(emotion, "triangle_exclamation"))
        animation_ = Animation::Error;
}

void PocketDisplay::SetChatMessage(const char* role, const char* text) {
    if (!setup_ui_called_ || !text)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    // Single bounded subtitle: never accumulate chat objects or arbitrary server text.
    size_t length = strnlen(text, 320);
    if (length == 320) {
        while (length && (static_cast<uint8_t>(text[length]) & 0xc0) == 0x80)
            --length;
    }
    std::string bounded(text, length);
    if (role && !strcmp(role, "system") &&
        Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring) {
        auto& wifi = WifiManager::GetInstance();
        bounded = "Join Wi-Fi:\n" + wifi.GetApSsid() + "\nOpen browser:\n" + wifi.GetApWebUrl();
    }
    lv_label_set_text(message_, bounded.c_str());
    if (role && !strcmp(role, "assistant"))
        waiting_ = false;
}

void PocketDisplay::ClearChatMessages() {
    if (!setup_ui_called_)
        return;
    DisplayLockGuard lock(this);
    if (lock && !waiting_)
        lv_label_set_text(message_, "Press button to talk");
}

void PocketDisplay::SetActivationCode(const std::string& code) {
    if (!setup_ui_called_)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    lv_label_set_text(code_, code.substr(0, 16).c_str());
    lv_obj_remove_flag(code_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
}

void PocketDisplay::WaitForAnswer() {
    if (!setup_ui_called_)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    waiting_ = true;
    waiting_since_ = esp_timer_get_time();
    animation_ = Animation::Think;
    lv_label_set_text(status_label_, "Thinking...");
}

void PocketDisplay::UpdateStatusBar(bool update_all) {
    if (!setup_ui_called_)
        return;
    auto icon = Board::GetInstance().GetNetworkStateIcon();
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    if (icon && (update_all || network_icon_ != icon)) {
        network_icon_ = icon;
        lv_label_set_text(network_label_, icon);
    }
    if (waiting_ && esp_timer_get_time() - waiting_since_ > 30000000) {
        waiting_ = false;
        animation_ = Animation::Error;
        lv_label_set_text(status_label_, "Response timeout");
        lv_label_set_text(message_, "Press button to retry");
    }
}

void PocketDisplay::SetTheme(Theme* theme) {
    // Keep server-provided fonts usable without loading large background/emoji assets.
    if (!theme || !setup_ui_called_)
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    auto font = theme->GetTextFont();
    if (font)
        lv_obj_set_style_text_font(lv_display_get_screen_active(display_), font->font(), 0);
    current_theme_ = theme;
}

bool PocketDisplay::SetTextFont(std::shared_ptr<LvglFont> font) {
    if (!font || !font->font() || !setup_ui_called_)
        return false;
    DisplayLockGuard lock(this);
    if (!lock)
        return false;
    auto previous_font = theme_.GetTextFont();
    theme_.set_text_font(font);
    lv_obj_set_style_text_font(lv_display_get_screen_active(display_), font->font(), 0);
    return true;
}

#include <driver/spi_master.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <atomic>
#include <cstring>
#include <memory>
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "pocket_audio_codec.h"
#include "pocket_display.h"
#include "quiet_test_tone.h"
#include "wifi_board.h"

static constexpr const char* TAG = "PocketAI";

class SeeedXiaoEsp32s3PocketAI : public WifiBoard {
private:
    Button button_{POCKET_BUTTON, false, 5000};
    PocketAudioCodec codec_;
    NoDisplay no_display_;
    std::unique_ptr<PocketDisplay> pocket_display_;
    std::atomic<Display*> display_{&no_display_};
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool spi_ready_ = false;
    TaskHandle_t maintenance_task_ = nullptr;
    std::atomic<bool> tone_requested_{false};

    void CleanupDisplay() {
        pocket_display_.reset();
        if (panel_)
            esp_lcd_panel_del(panel_);
        if (panel_io_)
            esp_lcd_panel_io_del(panel_io_);
        panel_ = nullptr;
        panel_io_ = nullptr;
    }

    void InitializeDisplay() {
        if (display_.load() != &no_display_)
            return;
        esp_err_t err = ESP_OK;
        if (!spi_ready_) {
            spi_bus_config_t bus{};
            bus.mosi_io_num = TFT_MOSI;
            bus.miso_io_num = GPIO_NUM_NC;
            bus.sclk_io_num = TFT_SCK;
            bus.quadwp_io_num = bus.quadhd_io_num = GPIO_NUM_NC;
            bus.max_transfer_sz = TFT_WIDTH * 20 * 2;
            err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
            spi_ready_ = err == ESP_OK;
        }
        esp_lcd_panel_io_spi_config_t io{};
        io.cs_gpio_num = TFT_CS;
        io.dc_gpio_num = TFT_DC;
        io.spi_mode = 0;
        io.pclk_hz = TFT_SPI_HZ;
        io.trans_queue_depth = 2;
        io.lcd_cmd_bits = io.lcd_param_bits = 8;
        if (err == ESP_OK)
            err = esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &panel_io_);
        esp_lcd_panel_dev_config_t panel_cfg{};
        panel_cfg.reset_gpio_num = TFT_RST;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        if (err == ESP_OK)
            err = esp_lcd_new_panel_st7789(panel_io_, &panel_cfg, &panel_);
        if (err == ESP_OK)
            err = esp_lcd_panel_reset(panel_);
        if (err == ESP_OK)
            err = esp_lcd_panel_init(panel_);
        if (err == ESP_OK)
            err = esp_lcd_panel_invert_color(panel_, true);
        if (err == ESP_OK)
            err = esp_lcd_panel_mirror(panel_, false, false);
        if (err == ESP_OK)
            err = esp_lcd_panel_disp_on_off(panel_, true);
        if (err == ESP_OK) {
            pocket_display_ = std::make_unique<PocketDisplay>(panel_io_, panel_);
            if (!pocket_display_->Ready())
                err = ESP_ERR_NO_MEM;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Display initialization failed: %s; serial remains available",
                     esp_err_to_name(err));
            CleanupDisplay();
            return;
        }
        pocket_display_->SetupUI();
        display_.store(pocket_display_.get());
        ESP_LOGW(TAG, "ST7789 ready: 240x280 RGB565 SPI2 20MHz offset 0,20");
    }

    void InitializeButton() {
        button_.OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                auto& app = Application::GetInstance();
                auto state = app.GetDeviceState();
                if (!codec_.Ready()) {
                    GetDisplay()->SetStatus("Audio unavailable");
                    return;
                }
                if (state == kDeviceStateIdle || state == kDeviceStateSpeaking) {
                    codec_.ArmSpeaker();
                    app.StartListening();  // manual-stop mode; click again to submit
                } else if (state == kDeviceStateListening) {
                    auto display = display_.load();
                    if (display != &no_display_)
                        static_cast<PocketDisplay*>(display)->WaitForAnswer();
                    app.StopListening();
                }
            });
        });
        button_.OnLongPress([this]() {
            Application::GetInstance().Schedule([this]() {
                // Current WifiBoard reopens provisioning without erasing credentials.
                if (!in_config_mode_)
                    EnterWifiConfigMode();
            });
        });
        button_.OnMultipleClick([this]() { tone_requested_.store(true); }, 3);
        button_.OnDoubleClick([this]() {
            Application::GetInstance().Schedule([this]() {
                auto& app = Application::GetInstance();
                auto state = app.GetDeviceState();
                if (state == kDeviceStateWifiConfiguring || state == kDeviceStateAudioTesting) {
                    codec_.ArmSpeaker();
                    app.ToggleChatState();  // upstream record-and-playback microphone test
                    if (state == kDeviceStateWifiConfiguring)
                        GetDisplay()->SetStatus("Recording microphone...");
                    else
                        GetDisplay()->SetStatus("Quiet playback test");
                }
            });
        });
    }

    void PrintDiagnostics() {
        auto desc = esp_app_get_description();
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        uint32_t flash = 0;
        esp_flash_get_size(nullptr, &flash);
        ESP_LOGW(TAG, "Pocket AI %s / %s / IDF %s / ESP32-S3 rev %d / %d cores", desc->version,
                 POCKET_BOARD_ID, esp_get_idf_version(), chip.revision, chip.cores);
        ESP_LOGW(TAG, "Flash %lu bytes / octal PSRAM %u bytes / native USB console",
                 static_cast<unsigned long>(flash), static_cast<unsigned>(esp_psram_get_size()));
        if (!esp_psram_is_initialized() || esp_psram_get_size() < 8 * 1024 * 1024)
            ESP_LOGE(TAG, "Expected XIAO 8MB PSRAM; verify board variant before operation");
        ESP_LOGW(TAG, "Wi-Fi / Xiaozhi: pending official provisioning and activation");
    }

    void Maintenance() {
        unsigned tick = 0;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            if (!codec_.Ready()) {
                codec_.RetryInitialize();
                GetDisplay()->SetStatus(codec_.Ready() ? "Audio ready"
                                                       : "Audio initialization failed");
            }
            if (display_.load() == &no_display_)
                InitializeDisplay();
            if (tone_requested_.exchange(false)) {
                auto& app = Application::GetInstance();
                auto state = app.GetDeviceState();
                if (codec_.Ready() &&
                    (state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring) &&
                    app.GetAudioService().IsPlaybackIdle()) {
                    codec_.ArmSpeaker();
                    // Submit through the existing Opus/output queues, never write I2S here.
                    app.PlaySound(std::string_view(reinterpret_cast<const char*>(kPocketQuietTone),
                                                   sizeof(kPocketQuietTone)));
                }
            }
            if (++tick % 12 == 0) {
                ESP_LOGW(
                    TAG,
                    "Heap free %u min %u / PSRAM free %u / mic peak %lu / RX %lu TX %lu timeouts",
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                    static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                    static_cast<unsigned long>(codec_.MicPeak()),
                    static_cast<unsigned long>(codec_.ReadTimeouts()),
                    static_cast<unsigned long>(codec_.WriteTimeouts()));
                if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 20000)
                    ESP_LOGE(TAG, "Low internal heap; finish conversation and inspect diagnostics");
            }
        }
    }

public:
    // Board owns the fixed audio scratch arrays; keep them in internal SRAM explicitly.
    static void* operator new(std::size_t size) {
        auto storage = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!storage)
            esp_system_abort("Pocket AI internal SRAM allocation failed");
        return storage;
    }
    static void operator delete(void* storage) { heap_caps_free(storage); }

    SeeedXiaoEsp32s3PocketAI() {
        PrintDiagnostics();
        InitializeDisplay();
        InitializeButton();
    }

    AudioCodec* GetAudioCodec() override { return &codec_; }
    Display* GetDisplay() override { return display_.load(); }
    bool IsFirmwareCompatible(const esp_app_desc_t& image) const override {
        return strncmp(image.project_name, POCKET_BOARD_ID, sizeof(image.project_name)) == 0;
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback(
            [this, callback = std::move(callback)](NetworkEvent event, const std::string& data) {
                Application::GetInstance().Schedule([this, event]() {
                    if (event == NetworkEvent::Disconnected) {
                        GetDisplay()->SetStatus("Wi-Fi disconnected");
                        GetDisplay()->SetChatMessage("system", "Reconnecting automatically...");
                    } else if (event == NetworkEvent::Connecting) {
                        GetDisplay()->SetStatus("Connecting Wi-Fi...");
                    } else if (event == NetworkEvent::WifiConfigModeEnter) {
                        GetDisplay()->SetStatus("Wi-Fi setup");
                    }
                });
                if (callback)
                    callback(event, data);
            });
    }

    void StartNetwork() override {
        // Board construction has completed: safe to use application/network callbacks now.
        if (!maintenance_task_) {
            auto ok = xTaskCreate(
                [](void* arg) { static_cast<SeeedXiaoEsp32s3PocketAI*>(arg)->Maintenance(); },
                "pocket_health", 4096, this, 1, &maintenance_task_);
            if (ok != pdPASS)
                ESP_LOGE(TAG, "Cannot create health task");
        }
        auto& mcp = McpServer::GetInstance();
        mcp.AddUserOnlyTool(
            "self.pocket.quiet_test_tone",
            "Play a quiet 440Hz 200ms speaker test. Speaker remains limited to 30%.",
            PropertyList(), [this](const PropertyList&) -> ToolResult {
                tone_requested_.store(true);
                return true;
            });
        GetDisplay()->SetStatus("Connecting Wi-Fi...");
        WifiBoard::StartNetwork();
    }
};

DECLARE_BOARD(SeeedXiaoEsp32s3PocketAI);

#include "pocket_audio_codec.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "application.h"
#include "config.h"
#include "pocket_audio_math.h"
#include "settings.h"

static constexpr const char* TAG = "PocketAudio";

PocketAudioCodec::PocketAudioCodec() {
    duplex_ = true;
    input_reference_ = false;
    input_sample_rate_ = output_sample_rate_ = POCKET_SAMPLE_RATE;
    input_channels_ = output_channels_ = 1;
    output_volume_ = POCKET_DEFAULT_VOLUME;
}

PocketAudioCodec::~PocketAudioCodec() { ReleaseChannels(); }

void PocketAudioCodec::ReleaseChannels() {
    if (rx_running_)
        i2s_channel_disable(rx_handle_);
    if (tx_running_)
        i2s_channel_disable(tx_handle_);
    if (rx_handle_)
        i2s_del_channel(rx_handle_);
    if (tx_handle_)
        i2s_del_channel(tx_handle_);
    rx_handle_ = tx_handle_ = nullptr;
    tx_running_ = rx_running_ = false;
}

void PocketAudioCodec::RetryInitialize() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (ready_.load())
        return;
    // Recovery waits for bounded I/O before replacing handles; RX/TX remain independent.
    std::scoped_lock io_lock(rx_mutex_, tx_mutex_);
    ReleaseChannels();
    tx_buffer_.fill(0);
    gain_ = 0;
    consecutive_read_timeouts_ = 0;
    flush_rx_.store(true);
    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(XIAOZHI_I2S_PORT(0), I2S_ROLE_MASTER);
    channels.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    channels.dma_frame_num = kFrames;
    channels.auto_clear_after_cb = true;  // silence on underrun, never repeat old audio
    auto err = i2s_new_channel(&channels, &tx_handle_, &rx_handle_);
    i2s_std_config_t cfg{};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(POCKET_SAMPLE_RATE);
    cfg.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = I2S_BCLK;
    cfg.gpio_cfg.ws = I2S_WS;
    cfg.gpio_cfg.dout = I2S_SPEAKER_DATA_OUT;
    cfg.gpio_cfg.din = I2S_MIC_DATA_IN;
    if (err == ESP_OK)
        err = i2s_channel_init_std_mode(tx_handle_, &cfg);
    if (err == ESP_OK)
        err = i2s_channel_init_std_mode(rx_handle_, &cfg);
    // DMA memory is zeroed by the driver. Preload explicit silence before clocks start.
    size_t loaded = 0;
    if (err == ESP_OK)
        err = i2s_channel_preload_data(tx_handle_, tx_buffer_.data(), sizeof(tx_buffer_), &loaded);
    if (err == ESP_OK) {
        err = i2s_channel_enable(tx_handle_);
        tx_running_ = err == ESP_OK;
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(rx_handle_);
        rx_running_ = err == ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio initialization failed: %s; retry in 5 seconds", esp_err_to_name(err));
        ReleaseChannels();
        return;
    }
    ready_.store(true);
    ESP_LOGW(TAG, "MIC left 24-bit / AMP stereo 32-bit: 48000 Hz, BCLK 3072000 Hz, DMA 6x240");
}

void PocketAudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = std::clamp<int32_t>(settings.GetInt("output_volume", POCKET_DEFAULT_VOLUME), 0,
                                         POCKET_MAX_VOLUME);
    target_gain_.store(pocket_audio::GainQ16(output_volume_));
    RetryInitialize();
}

void PocketAudioCodec::SetOutputVolume(int volume) {
    AudioCodec::SetOutputVolume(std::clamp(volume, 0, POCKET_MAX_VOLUME));
    target_gain_.store(pocket_audio::GainQ16(output_volume_));
}

void PocketAudioCodec::SetInputGain(float gain) {
    // Unity conversion is intentional: stock NoAudioCodec's shift-by-12 adds 24 dB.
    input_gain_ = 0.0f;
    (void)gain;
}

void PocketAudioCodec::EnableInput(bool enable) {
    if (enable && !capture_.exchange(true))
        flush_rx_.store(true);
    if (!enable)
        capture_.store(false);
    AudioCodec::EnableInput(enable);
    // Both hardware channels stay clocked; disabling TX would stop the microphone clock.
}

void PocketAudioCodec::EnableOutput(bool enable) {
    playback_.store(enable);
    AudioCodec::EnableOutput(enable);
}

bool PocketAudioCodec::InputData(std::vector<int16_t>& data) {
    return Read(data.data(), data.size()) == static_cast<int>(data.size());
}

int PocketAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(rx_mutex_);
    if (!ready_.load() || !capture_.load()) {
        vTaskDelay(pdMS_TO_TICKS(20));
        return 0;
    }
    if (flush_rx_.exchange(false)) {
        // Discard at most the bounded DMA queue after idle, then get fresh PCM.
        for (int i = 0; i < AUDIO_CODEC_DMA_DESC_NUM + 1; ++i) {
            size_t bytes = 0;
            if (i2s_channel_read(rx_handle_, rx_buffer_.data(), sizeof(rx_buffer_), &bytes, 0) !=
                ESP_OK)
                break;
        }
    }
    int done = 0;
    uint32_t peak = 0;
    while (done < samples) {
        int count = std::min(kFrames, samples - done);
        size_t bytes = 0;
        auto err = i2s_channel_read(rx_handle_, rx_buffer_.data(), count * 8, &bytes, 100);
        if (err != ESP_OK || bytes != static_cast<size_t>(count * 8)) {
            read_timeouts_.fetch_add(1);
            if (err != ESP_ERR_TIMEOUT || ++consecutive_read_timeouts_ >= 3)
                ready_.store(false);
            std::fill(dest, dest + samples, 0);
            return 0;  // upstream must not feed a partial/stale vector to AFE
        }
        for (int i = 0; i < count; ++i) {
            int16_t pcm = pocket_audio::MicrophonePcm(rx_buffer_[2 * i]);
            dest[done + i] = pcm;
            peak = std::max(peak, static_cast<uint32_t>(std::abs(static_cast<int>(pcm))));
        }
        done += count;
        consecutive_read_timeouts_ = 0;
    }
    auto previous = mic_peak_.load();
    while (peak > previous && !mic_peak_.compare_exchange_weak(previous, peak)) {
    }
    return done;
}

void PocketAudioCodec::OutputData(std::vector<int16_t>& data) {
    auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateSpeaking || state == kDeviceStateNotifying ||
        state == kDeviceStateAudioTesting)
        armed_.store(true);
    // Provisioning, activation and success tones remain silent until user interaction.
    if (armed_.load())
        Write(data.data(), data.size());
}

int PocketAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!ready_.load() || !playback_.load())
        return 0;
    int64_t now = esp_timer_get_time();
    if (now - last_write_us_ > 60000)
        gain_ = 0;
    int done = 0;
    while (done < samples) {
        int count = std::min(kFrames, samples - done);
        for (int i = 0; i < count; ++i) {
            gain_ = pocket_audio::RampGain(gain_, target_gain_.load());
            auto value = pocket_audio::SpeakerSlot(data[done + i], gain_);
            tx_buffer_[2 * i] = tx_buffer_[2 * i + 1] = value;
        }
        // Handle partial writes by advancing bytes, never replay the same samples.
        size_t offset = 0;
        size_t length = count * 8;
        int64_t deadline = esp_timer_get_time() + 200000;
        while (offset < length) {
            size_t written = 0;
            auto err = i2s_channel_write(tx_handle_,
                                         reinterpret_cast<uint8_t*>(tx_buffer_.data()) + offset,
                                         length - offset, &written, 20);
            offset += written;
            if ((err != ESP_OK && err != ESP_ERR_TIMEOUT) || esp_timer_get_time() >= deadline) {
                write_timeouts_.fetch_add(1);
                ready_.store(false);
                gain_ = 0;
                return done + offset / 8;
            }
        }
        done += count;
    }
    last_write_us_ = esp_timer_get_time();
    return done;
}

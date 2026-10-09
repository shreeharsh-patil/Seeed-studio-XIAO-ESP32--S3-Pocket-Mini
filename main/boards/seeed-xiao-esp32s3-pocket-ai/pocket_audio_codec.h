#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include "audio_codec.h"

class PocketAudioCodec : public AudioCodec {
public:
    PocketAudioCodec();
    ~PocketAudioCodec() override;
    void Start() override;
    void SetOutputVolume(int volume) override;
    void SetInputGain(float gain) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void OutputData(std::vector<int16_t>& data) override;
    bool InputData(std::vector<int16_t>& data) override;
    bool Ready() const { return ready_.load(); }
    void RetryInitialize();
    void ArmSpeaker() { armed_.store(true); }
    uint32_t ReadTimeouts() const { return read_timeouts_.load(); }
    uint32_t WriteTimeouts() const { return write_timeouts_.load(); }
    uint32_t MicPeak() const { return mic_peak_.load(); }

protected:
    int Read(int16_t* dest, int samples) override;
    int Write(const int16_t* data, int samples) override;

private:
    static constexpr int kFrames = 240;
    std::array<int32_t, kFrames * 2> rx_buffer_{};
    std::array<int32_t, kFrames * 2> tx_buffer_{};
    std::mutex init_mutex_;
    std::mutex rx_mutex_;
    std::mutex tx_mutex_;
    unsigned consecutive_read_timeouts_ = 0;
    std::atomic<bool> ready_{false};
    std::atomic<bool> armed_{false};
    std::atomic<bool> capture_{false};
    std::atomic<bool> playback_{false};
    std::atomic<bool> flush_rx_{true};
    std::atomic<int32_t> target_gain_{0};
    std::atomic<uint32_t> read_timeouts_{0};
    std::atomic<uint32_t> write_timeouts_{0};
    std::atomic<uint32_t> mic_peak_{0};
    int32_t gain_ = 0;  // owned only by the upstream output task
    int64_t last_write_us_ = 0;
    bool tx_running_ = false;
    bool rx_running_ = false;
    void ReleaseChannels();
};

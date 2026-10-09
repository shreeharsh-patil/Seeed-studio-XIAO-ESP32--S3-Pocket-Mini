"""Exercise PCM extrema, output bounds, ramp and the actual embedded Opus test tone."""
import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/seeed-xiao-esp32s3-pocket-ai"


class PocketAudioTest(unittest.TestCase):
    def test_shared_rate_supported_by_both_devices_and_opus(self):
        config = (BOARD / "config.h").read_text(encoding="utf-8")
        match = re.search(r"^#define POCKET_SAMPLE_RATE (\d+)$", config, re.MULTILINE)
        self.assertIsNotNone(match)
        rate = int(match.group(1))
        # MAX98357A datasheet: 11.025/12/22.05/24 kHz LRCLK are unsupported.
        self.assertIn(rate, {8000, 16000, 32000, 44100, 48000, 88200, 96000})
        # INMP441's 24-bit stereo framing needs 64 BCLK ticks at 7.8..50 kHz.
        self.assertGreaterEqual(rate, 7800)
        self.assertLessEqual(rate, 50000)
        self.assertLessEqual(rate * 64, 3200000)
        # The upstream decoder initially opens at the codec's local output rate.
        self.assertIn(rate, {8000, 12000, 16000, 24000, 48000})

    def test_pcm_and_tone(self):
        source = r'''
#include "pocket_audio_math.h"
#include "quiet_test_tone.h"
#include "ogg_demuxer.h"
#include <cassert>
#include <climits>
#include <cstdlib>
int main() {
    using namespace pocket_audio;
    assert(MicrophonePcm(0) == 0);
    assert(MicrophonePcm(INT32_MIN) == INT16_MIN);
    assert(MicrophonePcm(0x7fffff00) == INT16_MAX);
    assert(MicrophonePcm(-256) == -1);
    assert(MicrophonePcm(0x00010000) == 1);
    assert(GainQ16(-1) == 0);
    assert(GainQ16(100) == GainQ16(30));
    assert(GainQ16(30) < 65536 / 10);
    for (int v = 0; v <= 100; ++v) {
        for (int s = INT16_MIN; s <= INT16_MAX; ++s) {
            auto out = SpeakerSlot(static_cast<int16_t>(s), GainQ16(v));
            assert(std::abs(static_cast<int64_t>(out)) <= INT64_C(193273528));
        }
    }
    int32_t gain = 0;
    for (int i = 0; i < 1200; ++i) {
        auto next = RampGain(gain, GainQ16(30));
        assert(next >= gain && next - gain <= 5);
        gain = next;
    }
    assert(gain == GainQ16(30));
    for (int i = 0; i < 1200; ++i) gain = RampGain(gain, 0);
    assert(gain == 0);
    OggDemuxer demux;
    unsigned packets = 0;
    demux.OnPacket([&](const uint8_t*, int rate, int duration, size_t size) {
        assert(rate == 24000);
        assert(duration == 60);
        assert(size > 0);
        ++packets;
    });
    // Fragment at every byte boundary to exercise the firmware's real parser.
    for (auto byte : kPocketQuietTone) demux.Process(&byte, 1);
    assert(!demux.HasError());
    assert(demux.Finish());
    assert(packets >= 3 && packets <= 5);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "esp_log.h").write_text(
                "#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n"
                "#define ESP_LOGD(...) ((void)0)\n", encoding="utf-8"
            )
            cpp = path / "pocket_test.cc"
            cpp.write_text(source, encoding="utf-8")
            exe = path / "pocket_test"
            compiler = shlex.split(os.environ.get("CXX", "c++"))
            subprocess.run(compiler + ["-std=c++17", "-O2", "-I", str(BOARD),
                "-I", str(path), "-I", str(ROOT / "main/audio/demuxer"), str(cpp),
                str(ROOT / "main/audio/demuxer/ogg_demuxer.cc"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

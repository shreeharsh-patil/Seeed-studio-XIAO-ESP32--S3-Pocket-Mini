"""Run the actual OTA transfer method against fragmented/failing HTTP and flash mocks."""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PocketOtaTest(unittest.TestCase):
    def test_fragmented_and_incompatible_images(self):
        ota = (ROOT / "main/ota.cc").read_text(encoding="utf-8")
        start = ota.index("bool Ota::Upgrade(")
        end = ota.index("bool Ota::StartUpgrade(", start)
        method = ota[start:end]
        source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_INTERNAL 0
constexpr int ESP_OK=0, ESP_ERR_OTA_VALIDATE_FAILED=1;
constexpr uint32_t ESP_APP_DESC_MAGIC_WORD=0xabcd5432;
constexpr int OTA_WITH_SEQUENTIAL_WRITES=0;
using esp_err_t=int;
using esp_ota_handle_t=uint32_t;
struct esp_image_header_t { char bytes[24]; };
struct esp_image_segment_header_t { char bytes[8]; };
struct esp_app_desc_t {
    uint32_t magic_word;
    char project_name[32];
    char pad[220];
};
struct Partition { const char* label="ota_1"; uint32_t address=0x310000,size=65536; };
Partition partition;
std::vector<uint8_t> input, flash;
size_t declared_length=0, fragment=1, fail_at=SIZE_MAX;
unsigned begin_count=0, abort_count=0, boot_count=0;
struct Error { std::string ToString() const {return "simulated timeout";} };
struct Http {
    size_t position=0;
    std::expected<void,Error> Open(const char*,const std::string&) {return {};}
    std::expected<int,Error> GetStatusCode() {return 200;}
    size_t GetBodyLength() {return declared_length;}
    std::expected<int,Error> Read(char* dst,size_t capacity) {
        if(position>=fail_at) return std::unexpected(Error{});
        auto n=std::min({capacity,fragment,input.size()-position});
        std::memcpy(dst,input.data()+position,n);
        position+=n;
        return static_cast<int>(n);
    }
    void Close() {}
};
struct Network { std::unique_ptr<Http> CreateHttp(int) {return std::make_unique<Http>();} };
struct Board {
    static Board& GetInstance() {static Board b; return b;}
    Network* GetNetwork() {static Network n;return &n;}
    bool IsFirmwareCompatible(const esp_app_desc_t& desc) {
        return std::strncmp(desc.project_name,"seeed-xiao-esp32s3-pocket-ai",32)==0;
    }
};
const Partition* esp_ota_get_next_update_partition(void*) {return &partition;}
esp_err_t esp_ota_begin(const Partition*,int,esp_ota_handle_t* handle) {
    ++begin_count; *handle=7; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle,const void* data,size_t size) {
    assert(handle==7);
    auto bytes=static_cast<const uint8_t*>(data);
    flash.insert(flash.end(),bytes,bytes+size);
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {assert(handle==7);return ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const Partition*) {++boot_count;return ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t) {++abort_count;return ESP_OK;}
void* heap_caps_malloc(size_t n,int) {return std::malloc(n);}
void heap_caps_free(void* p) {std::free(p);}
int64_t esp_timer_get_time() {static int64_t ticks=0;return ++ticks;}
struct Ota {
    static bool Upgrade(const std::string&,std::function<void(int,size_t)>);
};
'''
        source += method
        source += r'''
void Reset(size_t chunk=1) {
    input.assign(9197,0x5a);
    esp_app_desc_t desc{};
    desc.magic_word=ESP_APP_DESC_MAGIC_WORD;
    std::strcpy(desc.project_name,"seeed-xiao-esp32s3-pocket-ai");
    std::memcpy(input.data()+32,&desc,sizeof(desc));
    declared_length=input.size(); fragment=chunk; fail_at=SIZE_MAX;
    begin_count=abort_count=boot_count=0; flash.clear();
}
int main() {
    for(size_t chunk : {1U,17U,255U,287U,4096U,5000U}) {
        Reset(chunk);
        assert(Ota::Upgrade("https://example.invalid/firmware",{}));
        assert(begin_count==1 && boot_count==1 && abort_count==0);
        assert(flash==input);
    }
    Reset(); input[36]='X';
    assert(!Ota::Upgrade("",{}));
    assert(begin_count==0 && boot_count==0 && flash.empty());
    Reset(); input[32]=0;
    assert(!Ota::Upgrade("",{})); assert(begin_count==0);
    Reset(); declared_length=partition.size+1;
    assert(!Ota::Upgrade("",{})); assert(begin_count==0);
    Reset(); declared_length=0;
    assert(!Ota::Upgrade("",{})); assert(begin_count==0);
    Reset(17); input.resize(199);
    assert(!Ota::Upgrade("",{})); assert(begin_count==0 && boot_count==0);
    Reset(1000); input.resize(5000);
    assert(!Ota::Upgrade("",{})); assert(abort_count==1 && boot_count==0);
    Reset(1000); fail_at=4500;
    assert(!Ota::Upgrade("",{})); assert(abort_count==1 && boot_count==0);
    Reset(1000); declared_length=8000;
    assert(!Ota::Upgrade("",{})); assert(abort_count==1 && boot_count==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            cpp = path / "ota_test.cc"
            exe = path / "ota_test"
            cpp.write_text(source, encoding="utf-8")
            compiler = shlex.split(os.environ.get("CXX", "c++"))
            subprocess.run(compiler + ["-std=c++23", "-O2", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

# Seeed XIAO ESP32-S3 Pocket AI

The hardware, build, flashing and staged validation guide is
[README_XIAO_POCKET_AI.md](../../../README_XIAO_POCKET_AI.md).

Use the canonical variant build so the application image embeds the unique OTA identity:

```sh
python scripts/build.py seeed-xiao-esp32s3-pocket-ai --name seeed-xiao-esp32s3-pocket-ai --language en-US --wake-word disabled
```

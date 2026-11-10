
去下載並安裝免費軟件 Processing (https://processing.org/download)。

軟件 Processing 直接撳 ▶️ Play 就可以 Gen 個 .vlw 出嚟

然後分別 Gen 出新嘅 Noto24.vlw 同 Noto16.vlw，放入 data 資料夾。

cmd 指令，打包 in DOS:
1）
如果用 default 4MB spiffs:
py -m littlefs create data littlefs.bin --block-size 4096 --block-count 256

download to ESP32 S3
py -m esptool --chip esp32s3 --port COM4 --baud 921600 write_flash 0x290000 littlefs.bin


2）
如果用 8MB with spiffs(3MB APP/1.5MB SPIFFS)：



3）
如果用 No OTA(2MB APP/2MB SPIFFS)
py -m littlefs create data littlefs.bin --block-size 4096 --block-count 480

download to ESP32 S3
py -m esptool --chip esp32s3 --port COM4 --baud 921600 write_flash 0x210000 littlefs.bin





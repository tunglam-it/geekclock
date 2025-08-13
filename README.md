# ESP8266 HelloCubic (VN TZ)

Bản mẫu dự án ESP8266 (Arduino) tái tạo tính năng tương tự firmware gốc:
- Portal Wi‑Fi (WiFiManager) + mDNS (`hellocubic.local`)
- WebServer trả JSON & trang cấu hình (LittleFS)
- NTP + **mặc định múi giờ Việt Nam** (TZ POSIX `ICT-7`), luôn hiển thị giờ VN
- Endpoint tương tự: `/config.json`, `/ntp.json`, `/time.json`, `/wifi.json` (đọc trạng thái Wi‑Fi),
  `/fs/list`, `/fs/upload`, `/fs/delete`
- OTA qua web: `/update` (AsyncElegantOTA) để nạp **.bin firmware**
- Cho phép chỉnh NTP server & timezone trong JSON, lưu vào LittleFS

## Build

1. Cài [PlatformIO](https://platformio.org/install)
2. `pio run -t upload` để flash firmware
3. `pio run -t uploadfs` để nạp dữ liệu LittleFS từ thư mục `data/`
4. Kết nối Wi‑Fi Portal, cấu hình Wi‑Fi
5. Truy cập `http://hellocubic.local` hoặc IP thiết bị

## Ghi chú múi giờ (ESP8266/newlib)
- ESP dùng biến môi trường `TZ` (POSIX). VN: `ICT-7` (UTC+7, không DST).
- Code gọi: `setenv("TZ","ICT-7",1); tzset();`
- `configTime(0, 0, ntp)` để luôn dùng giờ UTC từ NTP, sau đó `localtime()` sẽ bù TZ.

## API
- `GET /time.json` → `{ "epoch":..., "iso": "...", "local": "...", "tz":"ICT-7" }`
- `GET /config.json` / `PUT /config.json` → đặt `timezone`, `ntp`
- `GET /ntp.json` (alias, read-only theo config)
- `GET /wifi.json` → RSSI, SSID, IP
- `GET /fs/list`, `POST /fs/upload` (multipart), `DELETE /fs/delete?path=/x`
- `GET /settings.html.gz` trang cấu hình đơn giản
- `GET /update` → OTA (AsyncElegantOTA)

> Bạn có thể mở rộng endpoint theo nhu cầu.
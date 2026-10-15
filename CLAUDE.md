# CLAUDE.md – WazeHUD cho LILYGO T-Display-S3 (320×170)

Trả lời người dùng bằng **tiếng Việt**.

## 1. Dự án là gì

Firmware ESP32-S3 (ESP-IDF 5.5.x, C++17) nhận dữ liệu Waze qua **BLE / HLP/1** (app WazeMod) và vẽ HUD 320×170 trên ST7789V của LILYGO T-Display-S3. Không có loa, cảm ứng, WiFi/OTA.

- Tách riêng từ `D:\PROJECT\APP WAZEHUD\Waze-Hud-ES3C28P-S3` (nhánh `ES3C28P-S3`, commit gốc `7a0a2b1`, kèm vài thay đổi chưa commit lúc chép). **Không sửa repo ES3C28P từ đây.** Mã dùng chung (giao thức, state, cấu hình, font, asset) là bản sao: sửa một bên không tự cập nhật bên kia.
- Bản LilyGo cũ (V10-25082026) ở `D:\PROJECT\APP WAZEHUD\WAZE HUD LILYGO T-Display S3` chỉ để tham khảo. Driver màn hình và chân GPIO lấy từ đó.
- Mã firmware: `waze-hud/main/`.

## 2. Quy trình làm việc

1. Thay đổi giao thức, cấu hình hoặc bố cục nhiều file: nêu ngắn phạm vi và rủi ro **trước khi sửa**. Sửa nhỏ, rõ ràng thì làm thẳng.
2. Chỉ build bản T-Display BLE (`waze-hud\build_tdisplay_s3.ps1`). Bản mock (`-Mock`) chỉ khi người dùng cần xem nhanh giao diện.
3. **Người dùng nạp thử trên board thật rồi mới xác nhận.** Sau mỗi thay đổi: build, đưa đường dẫn file `.bin`, hỏi xác nhận. Không tuyên bố "đã chạy" khi mới chỉ biên dịch.
4. Không commit/push/phát hành khi chưa được yêu cầu. Không commit file `.bin`. Không `git push --force`.
5. Thư mục này chưa có remote. Trước khi đặt remote hoặc push, hỏi người dùng repo đích.

## 3. Phần cứng (theo bản LilyGo gốc)

LCD ST7789V i80 8 bit: nguồn GPIO 15, RD 9, WR 8, DC 7, CS 6, RST 5, D0–D7 = 39–42, 45–48, đèn nền GPIO 38 (LEDC), gap 35 px, xoay/lật bằng `swap_xy` + `mirror`. Nút KEY GPIO 14. Pin GPIO 4 (ADC1 CH3, chia 2:1). Console USB Serial/JTAG. GPIO 33–37 là bus PSRAM octal, không dùng.

## 4. Bố cục 320×170

3 vùng dirty-region: `MainCluster` {0,0,225,140}, `Alerts` {225,0,95,140}, `Street` {0,140,320,30} (`display/layout.h`). Mã vẽ MainCluster/Alerts giữ toạ độ của dải 130 px (bản ES3C28P), `kClusterShiftY = 5` dịch xuống để căn giữa. Nếu đổi toạ độ, nhớ khối làn (`kLaneBlockWidth/Height`) và banner tên đường kế tiếp dùng chung góc trên-trái.

Không còn vùng Guidance: làn đường vẽ mini ở góc trên-trái MainCluster (`drawMiniLanes`, nét 1–2 px), ETA ở thanh Street, cột Alerts có 1 biển lớn + tối đa 2 biển nhỏ.

## 5. Giao thức HLP và cài đặt

Mã ở `main/protocol/` và `main/config/device_config.*`. HUD **sở hữu** danh mục cài đặt. Schema hiện tại: `kSchemaRevision = 8`, `kItemCount = 11`, bit `pending.mask` 0–10 (`display_mode` = bit 9, `show_street` = bit 10).

Khi thêm/bớt/đổi một mục cài đặt, cập nhật đồng bộ: `DeviceSettings` (`.h`), `kSchemaRevision`, `kItemCount`, `saveSettings`, phần đọc NVS trong `DeviceConfig::init`, `publishSchema`, nhánh `cfg_set`, và `hasSettingsChanged` trong `hud_renderer.cpp`. Đổi schema có thể làm app từ chối giao dịch: kiểm tra với app trước khi phát hành.

**Bất khả xâm phạm:**

- **Không gửi lại `dev` khi nhận `hi`.** `dev` chỉ gửi khi BLE bật notify (`NotificationsEnabled`). Gửi trong nhánh `hi` gây vòng lặp `dev` ↔ `hi`, reset session, mất phân làn và hủy giao dịch cài đặt.
- **Không chèn `vTaskDelay` vào `sendJson`/đường gửi `cfg_*`** (11 bản tin chạy trên task `hlp_protocol`). Chia gói theo MTU thuộc `BleTransport::sendLine`.
- Callback BLE chỉ copy dữ liệu vào queue; JSON, state và vẽ chạy ngoài callback.
- Frame HLP tối đa 512 byte, kiểm tra UTF-8, bỏ frame lỗi an toàn.

## 6. Ưu tiên biển báo cảnh báo

Hai nơi sắp xếp, giữ nhất quán: `alertPriorityScore` trong `hlp_decoder.cpp` (chuẩn hóa `alrs`) và `collectSortedAlerts` trong `hud_renderer.cpp` (3 hạng: biển cấm/hạ tốc độ, camera/đèn đỏ/CSGT, còn lại). Biển số 1 vẽ lớn, hai biển tiếp theo vẽ nhỏ.

## 7. Asset

`main/assets/generated_assets.cpp` chép từ bản ES3C28P. Bộ asset nguồn không có trong repo và script sinh asset của bản ES3C28P đã lỗi thời (thiếu `TrafficLight`, `kNumberSpeedLimit*`), nên không tạo lại được. Không sửa tay file sinh sẵn.

## 8. Ghi nhận tác giả và tuyên bố AI

Mọi bản phát hành, README, CHANGELOG phải cảm ơn và dẫn link: [ShindouAris (WazeHUD)](https://github.com/ShindouAris/WazeHUD), [WazeMod Vietnam](https://wazemod.io.vn), [quangdo92](https://github.com/quangdo92/waze-hud); giữ giấy phép GPL-3.0; nêu rõ bản này được phát triển với sự hỗ trợ của AI (Claude Code, Anthropic).

## 9. Kiểm thử

Không có unit test. Kiểm chứng = build thành công + nạp board thật. Bản mock dùng để xem nhanh 12 kịch bản giao diện.

Không cần board để xem giao diện: `tools\host_preview\run.ps1` (cần `pip install ziglang`) biên dịch `hud_renderer.cpp` trên PC và xuất ảnh; `-Test` kiểm tra vẽ lại từng vùng khớp vẽ lại toàn bộ. Công cụ này chỉ chứng minh phần renderer, không thay cho build ESP-IDF và nạp board.

# WazeHUD cho LILYGO T-Display-S3 (320×170)

HUD ô tô hiển thị dữ liệu dẫn đường Waze qua Bluetooth BLE (giao thức HLP/1 của Waze Mod) trên màn hình ST7789 1.9" của **LILYGO T-Display-S3**.

Dự án này được tách riêng từ bản [ESP32-S3 ES3C28P](../Waze-Hud-ES3C28P-S3) (giao diện, giao thức, cấu hình), bỏ phần cứng không có trên T-Display-S3 (loa, cảm ứng, WiFi OTA) và dựng lại bố cục cho màn hình 320×170.

> [!NOTE]
> **Lời cảm ơn & tuyên bố dự án:** bản này được phát triển với sự hỗ trợ của **AI (Claude Code, Anthropic)**, trên nền mã nguồn bản CYD của [quangdo92](https://github.com/quangdo92/waze-hud) (B15/B16, theo changelog của họ được phát triển với Google Gemini), vốn dựa trên [WazeHUD của ShindouAris](https://github.com/ShindouAris/WazeHUD) và cộng đồng [WazeMod Vietnam](https://wazemod.io.vn). Xin trân trọng cảm ơn các tác giả. Đây là bản cá nhân hóa phục vụ nghiên cứu, độ chế và sử dụng thực tế trên xe, chia sẻ mã nguồn mở (GPL-3.0) cho cộng đồng cùng sở thích.

> [!WARNING]
> **Trạng thái:** build thành công với ESP-IDF 5.5.5 và đã được tác giả nạp thử trên board LILYGO T-Display-S3: giao diện hiển thị đúng. Kết nối BLE với WazeMod, nhận cài đặt schema rev 8, nút KEY và đo pin chưa được xác nhận riêng. Phần vẽ giao diện còn có thể xem thử trên PC bằng `tools/host_preview` (ảnh mẫu trong `previews/`).

## Xem thử giao diện trên PC (không cần board, không cần ESP-IDF)

```powershell
pip install ziglang
cd tools\host_preview
.\run.ps1          # ảnh tổng hợp trong tools\host_preview\out\
.\run.ps1 -Test    # thêm: kiểm tra vẽ lại từng vùng == vẽ lại toàn bộ
```

Công cụ biên dịch đúng `hud_renderer.cpp`, `bitmap_font.cpp` và `generated_assets.cpp` với các header ESP giả và một framebuffer thay cho driver LCD. Ảnh mẫu: `previews/`.

## Bố cục (3 vùng vẽ lại độc lập)

```text
┌──────────────────────────────┬───────────────┐
│ MainCluster  225×140         │ Alerts 95×140 │
│  trái: lane mini, mũi tên rẽ,│  ngày|giờ     │
│        khoảng cách (hoặc     │  biển cảnh báo│
│        đồng hồ khi chạy tự do)│  + 2 biển phụ │
│  phải: biển giới hạn tốc độ  │  (rảnh: so    │
│        + tốc độ xe           │  sánh giới hạn)│
├──────────────────────────────┴───────────────┤
│ Street 320×30: tên đường (chạy chữ)  | ETA   │
└──────────────────────────────────────────────┘
```

- Nội dung MainCluster và Alerts được thiết kế cho dải cao 130 px (như bản ES3C28P) và dịch xuống 5 px để căn giữa trong 140 px.
- **Làn đường** vẽ thu nhỏ ở góc trên-trái của MainCluster (tối đa 12 làn hiển thị, tự cuộn theo làn được chọn). Khi có làn, banner tên đường kế tiếp bắt đầu bên phải khối làn.
- **ETA** nằm ở góc phải thanh Street. Không còn quãng đường/thời gian còn lại, thẻ hành trình và các thẻ cảnh báo ở đáy như bản 240 px.
- Pin (nếu đọc được) hiện ở góc dưới-trái của MainCluster.

## Phần cứng

| Thành phần | Chân / thông số |
|---|---|
| Vi điều khiển | ESP32-S3R8, 16 MB flash, 8 MB PSRAM octal |
| Màn hình | ST7789V 170×320, giao tiếp i80 8 bit; xoay/lật bằng phần cứng |
| Nguồn ngoại vi LCD | GPIO 15 (bật mức cao) |
| LCD | RD 9, WR 8, DC 7, CS 6, RST 5; D0–D7: 39, 40, 41, 42, 45, 46, 47, 48 |
| Đèn nền | GPIO 38 (LEDC) |
| Nút KEY | GPIO 14 |
| Pin | GPIO 4 qua bộ chia 2:1 (ADC1 CH3) |
| Console | USB Serial/JTAG |

## Nút KEY (GPIO 14)

| Thao tác | Tác dụng |
|---|---|
| Nhấn 1 lần | Xoay HUD 180° |
| Nhấn đúp | Bật/tắt lật gương (Mirror HUD) |
| Giữ khoảng 1,2 giây | Hiện trạng thái pin và BLE; thả để quay lại |

## Cấu hình từ Waze Mod (HLP Device Config, schema rev 8, 11 mục)

| Mục (`id`) | Giá trị | Ý nghĩa |
|---|---|---|
| `fixed_brightness` | bật/tắt | Bật: dùng độ sáng chọn tay; tắt: tự động theo giờ (100% ban ngày, 20% ban đêm) |
| `brightness` | 10–100, bước 5 | Chỉ chỉnh được khi bật cố định độ sáng |
| `theme` | auto/day/night | Màu giao diện biển báo |
| `invert_color` | bật/tắt | Mặc định tắt (đúng màu trên T-Display). Bật sẽ đảo màu |
| `display_mode` | `current_speed` / `speed_limit_only` | Hiện cả tốc độ xe, hoặc chỉ biển giới hạn lớn (ẩn tốc độ xe và thẻ so sánh giới hạn, vẫn giữ viền đỏ khi quá tốc độ) |
| `show_street` | bật/tắt | Bật/tắt tên đường ở thanh dưới (ETA vẫn hiện) |
| `mirror_hud` | bật/tắt | Lật ngang để chiếu hắt kính lái |
| `rotate_display` | bật/tắt | Xoay 180° (mặc định tắt) |
| `overspeed_offset` | −10 đến +5 km/h | Bù vào giới hạn trước khi cảnh báo |
| `offset_x`, `offset_y` | −5 đến +5 px | Tinh chỉnh vị trí giao diện |

So với bản ES3C28P (rev 7, 9 mục): thêm `display_mode` và `show_street`. Cần thử với app WazeMod: nếu app từ chối giao dịch cài đặt thì xem log `CONFIG`.

## Build

Yêu cầu ESP-IDF **5.5.x** (profile mặc định `C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1`, đổi bằng `-IdfProfile`).

```powershell
cd waze-hud
.\build_tdisplay_s3.ps1              # -> dist\waze_hud_tdisplay_s3_factory.bin (nạp tại 0x0)
.\build_tdisplay_s3.ps1 -Mock        # bản mock 12 kịch bản, không cần điện thoại
.\build_tdisplay_s3.ps1 -Port COM9   # build, gộp và nạp luôn
```

Nạp thủ công: `python -m esptool --chip esp32s3 -p COM9 -b 460800 write_flash 0x0 dist\waze_hud_tdisplay_s3_factory.bin`. Nếu board không tự vào chế độ nạp: giữ BOOT, nhấn RESET, thả BOOT.

## Khác biệt so với các bản khác

| | ES3C28P (S3 2.8") | LilyGo cũ (V10-25082026) | Bản này |
|---|---|---|---|
| Màn hình | ILI9341 SPI 320×240, xoay mềm | ST7789 i80 320×170 | ST7789 i80 320×170 |
| Vùng vẽ | 4 (MainCluster, Alerts, Guidance, Street) | 5 | 3 (MainCluster, Alerts, Street) |
| Schema cấu hình | rev 7, 9 mục | rev 6, 9 mục | rev 8, 11 mục |
| Giao thức / ưu tiên cảnh báo | mới (B16) | cũ | mới (B16) |
| Loa, cảm ứng, WiFi OTA | loa có; cảm ứng/OTA là stub | không | không |

## Ghi chú về asset

`main/assets/generated_assets.cpp` được chép nguyên từ bản ES3C28P. Bộ asset nguồn (PNG/font) và script sinh asset không nằm trong repo, và script của bản ES3C28P đã lỗi thời so với file sinh sẵn, nên chưa thể tạo lại asset từ đây.

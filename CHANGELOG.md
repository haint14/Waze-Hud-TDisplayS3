# Nhật Ký Thay Đổi (Changelog) - WazeHUD LILYGO T-Display-S3

Firmware phát hành dạng **Factory All-in-One** (đã gồm bootloader, bảng phân vùng và firmware), nạp một file tại offset **`0x0`**, chip `esp32s3`, flash 16 MB.

> [!NOTE]
> **Lời cảm ơn & tuyên bố dự án:** bản này được phát triển với sự hỗ trợ của **AI (Claude Code, Anthropic)**, trên nền mã nguồn bản CYD của [quangdo92](https://github.com/quangdo92/waze-hud) (B15/B16, theo changelog của họ được phát triển với Google Gemini), vốn dựa trên [WazeHUD của ShindouAris](https://github.com/ShindouAris/WazeHUD) và cộng đồng [WazeMod Vietnam](https://wazemod.io.vn). Xin trân trọng cảm ơn các tác giả. Đây là bản cá nhân hóa phục vụ nghiên cứu, độ chế và sử dụng thực tế trên xe, chia sẻ mã nguồn mở (GPL-3.0) cho cộng đồng cùng sở thích.

---

## [r1] - 2026-10-15

Bản đầu tiên, dựng lại từ bản ESP32-S3 ES3C28P (nền B16) cho màn hình T-Display-S3 320×170.

- **Bố cục 3 vùng:** MainCluster (mũi tên rẽ, làn đường, biển giới hạn, tốc độ xe), Alerts (ngày giờ, cảnh báo) và Street (tên đường, ETA).
- **Làn đường thu nhỏ** ở góc trên-trái, tối đa 12 làn hiển thị.
- **Cột cảnh báo:** 1 biển lớn và tối đa 2 biển nhỏ; khi rảnh hiện so sánh tốc độ với giới hạn (DƯỚI/BẰNG/VƯỢT).
- **Cài đặt từ app (schema rev 8, 11 mục):** thêm *Kiểu hiển thị* (hiện cả tốc độ / chỉ giới hạn tốc độ lớn) và *Hiện tên đường*, giữ cố định độ sáng, đảo màu, lật gương, xoay, ngưỡng quá tốc, dịch vị trí.
- **Nút KEY (GPIO 14):** nhấn 1 lần xoay 180°, nhấn đúp lật gương, giữ để xem trạng thái pin và BLE.
- Độ sáng tự động theo giờ (100% ban ngày, 20% ban đêm), viền đỏ nháy khi quá tốc độ, tên đường và tên đường kế tiếp chạy chữ.
- Giao thức, giải mã cảnh báo và ưu tiên biển báo theo B16.
- Không có trên bản này: loa/giọng nói, cảm ứng, WiFi OTA (nạp bằng cáp USB-C).
- Firmware báo số phiên bản nội bộ `2.8.15-tds3`.

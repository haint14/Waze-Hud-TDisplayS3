# Nhật Ký Thay Đổi (Changelog) - WazeHUD LILYGO T-Display-S3

Firmware phát hành dạng **Factory All-in-One** (đã gồm bootloader, bảng phân vùng và firmware), nạp một file tại offset **`0x0`**, chip `esp32s3`, flash 16 MB.

> [!NOTE]
> **Lời cảm ơn & tuyên bố dự án:** bản này được phát triển với sự hỗ trợ của **AI (Claude Code, Anthropic)**, trên nền mã nguồn bản CYD của [quangdo92](https://github.com/quangdo92/waze-hud) (B15/B16, theo changelog của họ được phát triển với Google Gemini), vốn dựa trên [WazeHUD của ShindouAris](https://github.com/ShindouAris/WazeHUD) và cộng đồng [WazeMod Vietnam](https://wazemod.io.vn). Xin trân trọng cảm ơn các tác giả. Đây là bản cá nhân hóa phục vụ nghiên cứu, độ chế và sử dụng thực tế trên xe, chia sẻ mã nguồn mở (GPL-3.0) cho cộng đồng cùng sở thích.

---

## [r2] - 2026-10-17

- **Cột cảnh báo gọn hơn:** chỉ còn 1 biển lớn và tối đa 1 biển phụ (icon bên trái, khoảng cách bên phải, ngăn cách bằng một đường kẻ mảnh), thay cho 1 biển lớn và 2 biển nhỏ xếp ngang. Khoảng cách không còn bị tràn mép phải.
- **Lọc theo khoảng cách:** biển chính chỉ hiện khi cách dưới 800 m, biển phụ khi cách dưới 1,5 km. Biển xa hơn bị ẩn; không còn biển nào trong tầm thì cột quay về thẻ so sánh tốc độ với giới hạn.
- Vùng cấm vượt đang đi trong đó và cảnh báo không có khoảng cách vẫn luôn hiển thị.
- **Ô tốc độ xe lớn hơn:** số 36 px vẽ ở 85% trong khung co giãn theo con số (trước đây số 27 px, số 3 chữ số chỉ 13 px).
- **Rút gọn chỉ dẫn:** "Tiếp tục đi thẳng ..." thành "Đi thẳng ..." trong tên đường hiện tại và kế tiếp (ý tưởng từ B17 của quangdo92).
- **Nhãn kẹt xe chỉ còn mức độ** (NHẸ, VỪA, NẶNG, ĐỨNG IM, ĐƯỜNG ĐÓNG), bỏ số phút (ý tưởng từ B17 của quangdo92).
- Giao thức, schema cài đặt (rev 8) và thứ tự ưu tiên biển báo không đổi.
- Đã xem thử bằng `tools/host_preview` (kiểm tra vẽ lại từng vùng: 0 điểm lệch) và đã chạy trên board T-Display-S3 thật.
- Firmware báo số phiên bản nội bộ `2.8.16-tds3`.

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

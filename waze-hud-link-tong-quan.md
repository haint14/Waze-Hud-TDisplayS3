# Bắt đầu với Waze HUD Link

> Kiến trúc, luồng dữ liệu và sketch tối thiểu để ESP32 nhận trạng thái điều hướng từ Waze.

Canonical page: /tai-lieu/esp32

## 1. Là gì

`HudLink` là bộ **producer** nằm trong mod Waze (`com.waze.gw.HudLink`). Nó gom trạng thái HUD trực
tiếp — tốc độ, giới hạn tốc độ, hướng rẽ kế, khoảng cách, đường hiện tại/kế, ETA, vùng đo tốc độ
trung bình — thành **JSON mỗi dòng** (giao thức **HLP/1**) rồi đẩy qua một transport tới HUD ngoài.
App hiện hỗ trợ **Bluetooth Classic SPP** và **BLE GATT** bằng cùng frame HLP/1.

Vì dữ liệu tách rời khỏi việc vẽ, bạn **chế HUD tùy ý mà không phải build lại Waze**. Nó còn tiếp tục
đẩy dữ liệu **kể cả khi màn điện thoại TẮT** → dùng HUD ngoài cho phép tắt/giảm màn (mát hơn, đỡ pin).

## 2. Kiến trúc

```
Tiến trình Waze ──(hook smali)──► trạng thái Gateway.hud*
                                       │
                                       ▼
                      HudLinkService + HudConnectionManager
                                       │  snapshot HLP/1
                          ┌────────────┴────────────┐
                          ▼                         ▼
                BLE GATT (1–4 Hz)        Classic SPP (1–10 Hz)
                          │                         │
                          └──────────► HUD ngoài ◄──────────┘
```

- Foreground service `HudLinkService` sở hữu transport, handshake, writer và vòng kết nối lại. Hook
  nóng `onSpeedo` chỉ gửi yêu cầu khôi phục service theo cách idempotent và có giới hạn nhịp khi
  Android tạo lại process Waze.
- `HudStateSnapshot` đọc các field `Gateway.hud*` và phát **snapshot đầy đủ** mỗi message (không phải
  delta), nên mất một gói không làm hỏng trạng thái bên nhận.
- Phát khi có thay đổi và vẫn phát state không đổi ít nhất mỗi giây. BLE được giới hạn trong
  **1–4 Hz**; Classic SPP mặc định 8 Hz và nhận yêu cầu tối đa 10 Hz.

## 3. Bật lên

Preferences (file SharedPreferences `waze_hud_gw`):

| key                       | kiểu | mặc định | ý nghĩa |
|---------------------------|------|----------|---------|
| `hud_link`                | bool | false    | bật service HUD Link |
| `hud_link_log`            | bool | false    | log payload HLP RX/TX đã kết nối dưới tag `WazeHudLink` |
| `hud_link_transport`      | int  | 0        | `0` tự động, `1` Classic SPP, `2` BLE GATT |
| `hud_link_device_address` | str  | rỗng     | địa chỉ Bluetooth chính xác đã chọn |
| `hud_link_device_name`    | str  | rỗng     | tên hiển thị của thiết bị đã chọn |
| `hud_link_device_transport` | int | không có | transport lưu cùng thiết bị; picker đồng thời mirror sang `hud_link_transport` |

Mở **Cài đặt Mod → Thiết bị → HUD Link**, cấp quyền Thiết bị lân cận khi được hỏi, chọn đúng
transport/thiết bị rồi bật HUD Link. Thiết bị Classic phải được pair trong Cài đặt Android trước;
thiết bị BLE được tìm trong picker của app theo HLP service UUID cố định.

Bật log không tạo transport giả, nên không thể phát stream nếu chưa kết nối thiết bị. Khi chẩn đoán
với thiết bị thật, chỉ bật `hud_link_log` tạm thời rồi xem:

```bash
adb logcat -s WazeHudLink WazeHudLink-BLE
# {"v":1,"t":"s","nav":0,"spd":47,"lim":50,...,"st":"Nguyễn Trãi","ts":124890}
```

Preference cũ `hud_link_dev` chỉ dùng để migrate. App chỉ chuyển nó thành một thiết bị bonded cụ thể
khi tìm được đúng một kết quả không mơ hồ.

## 4. Luồng dữ liệu (HLP/1)

Mỗi dòng là 1 object JSON, UTF-8, kết bằng `\n`. Mọi message: `{"v":1,"t":"...", ...}`.
Xem `HLP_v1.md` cho spec đầy đủ (handshake `dev`/`hi`, thương lượng năng lực, versioning).

**Message trạng thái** (`"t":"s"`) — các field HUD cần:

| key    | đơn vị / khoảng      | ý nghĩa                                         |
|--------|----------------------|-------------------------------------------------|
| `nav`  | 0/1                  | đang dẫn đường theo tuyến                        |
| `spd`  | km/h                 | tốc độ hiện tại                                 |
| `lim`  | km/h, 0 = không có   | giới hạn tốc độ ![Biển tốc độ](/assets/icons/alerts/bigpin_speed_limit_world_50.png) |
| `over` | 0/1                  | đang vượt tốc (quá ngưỡng của mod)              |
| `trn`  | enum rẽ §4.1         | hướng rẽ kế tiếp ![Hướng rẽ](/assets/icons/Waze/car_big_trans_direction_left.png) |
| `trn2` | enum rẽ §4.1         | hướng rẽ sau đó ("rồi…")                        |
| `dst`  | mét, -1 = không có   | khoảng cách tới hướng rẽ kế                     |
| `exit` | ≥0                   | số lối ra ở vòng xuyến ![Vòng xuyến](/assets/icons/Waze/car_big_trans_directions_roundabout.png) |
| `lan`  | cặp bitmask làn      | chỉ dẫn làn opt-in, thứ tự trái sang phải       |
| `st`   | chuỗi                | đường **hiện tại** (đang đi)                     |
| `st2`  | chuỗi                | đường **kế tiếp** (rẽ vào)                       |
| `eta`  | "HH:MM"              | giờ đến dự kiến                                 |
| `rmin` | phút                 | thời gian còn lại                              |
| `rm`   | mét                  | quãng đường còn lại chính xác, ưu tiên hiển thị |
| `rkm`  | km (1 chữ số)        | quãng đường còn lại                            |
| `avg`  | 0/1                  | đang trong **vùng cấm vượt** (VN ![Cấm vượt](/assets/icons/alerts/no_passing_in.png)) |
| `avgL/avgR/avgP` | m / km/h / % | vùng: còn lại / tốc độ khuyến nghị / phần trăm |
| `alr`  | enum alert §4.2      | biển/cảnh báo **gần nhất** ![Cảnh báo](/assets/icons/alerts/bigpin_speed_camera.png) (0 = không) |
| `alrD` | mét, -1 = không có   | khoảng cách tới biển gần nhất                   |
| `alrV` | km/h                 | giá trị của biển gần nhất (giới hạn sắp tới cho biển giảm tốc) |
| `alrs` | mảng                 | tối đa 4 biển kế `[{k,d,v}]`, gần→xa (opt-in)   |
| `ts`   | ms                   | uptime producer (kiểm cũ/mới, thứ tự)          |

Quy ước vắng: `0` cho `lim`/`exit`/`avg*`; `-1` cho `dst`. `alrV` omit khi không áp dụng;
`alrs` đã thương lượng dùng mảng rỗng khi chưa có cảnh báo.
Thiếu key ⇒ dùng giá trị mặc định.

> **Vùng cấm vượt (`avg`).** VN không có camera đo tốc độ trung bình; editor tag hình học đó thành
> **cấm vượt**, Waze vẫn giao dưới dạng "avg-speed zone". Hiện biển **cấm vượt** + đếm ngược `avgL`,
> đừng vẽ camera. / *Vietnam has no average-speed cameras; render a no-passing sign, not a camera.*

> **Cảnh báo (`alr`/`alrs`).** Khi có cảnh báo, `alr`/`alrD`/`alrV` mô tả biển **gần nhất** và
> **mirror `alrs[0]`** (consumer không parse mảng vẫn có biển gần nhất). `alrs` = danh sách đầy đủ
> (≤4, gần→xa), **opt-in** — phải khai `alrs` trong `dev.want.fields` mới nhận. Trong `alrs`, key loại
> là **`k`** (không phải `t` — `t` là key loại-message ở envelope): `[{"k":2,"d":300},{"k":8,"d":800,"v":40}]`.
> `alrV`/`v` chỉ có với biển mang giá trị (giảm tốc → giới hạn mới); còn lại omit.
> Mã alert `0..9` giữ nguyên ý nghĩa cũ. HLP/1 bổ sung mã chi tiết `10..75` cho biển WME,
> trạm dừng/lối ra, loại camera, thời tiết, permanent hazard và đèn giao thông. Mã mới chưa biết phải fallback
> về icon nguy hiểm chung. Xem bảng đầy đủ tại `HLP_v1.md §4.2`.

**Enum rẽ** (`trn`,`trn2`): `0` không · `1` đi thẳng · `2` trái · `3` phải · `4/5` chếch trái/phải ·
`6/7` gắt trái/phải · `8` quay đầu · `10-12` vòng xuyến / trái / phải (dùng `exit`) · `13/14` giữ
trái/phải · `15/16` ra nhánh trái/phải · `17` đến nơi · `19` đi thẳng qua vòng xoay · `20` vào vòng
xoay rồi quay đầu (dùng `exit`).
(Bảng đầy đủ ở `HLP_v1.md §4.1`.)

## 5. Viết bên nhận (ESP32, Bluetooth SPP)

Sketch Arduino tối thiểu — đọc dòng, parse, vẽ:

```cpp
#include "BluetoothSerial.h"
#include <ArduinoJson.h>

BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("WazeHUD");          // điện thoại kết nối tới tên này
}

void loop() {
  static char line[512];
  static size_t n = 0;
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n') {                  // đã đủ 1 message HLP
      line[n] = 0; n = 0;
      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, line)) continue;   // bỏ dòng hỏng, resync ở '\n' kế
      const char* t = doc["t"] | "";
      if (strcmp(t, "s") == 0) {
        int   spd = doc["spd"] | 0;
        int   lim = doc["lim"] | 0;
        int   trn = doc["trn"] | 0;
        int   dst = doc["dst"] | -1;
        const char* st = doc["st"] | "";
        // TODO: vẽ spd / lim / bitmap mũi tên theo trn / dst / st …
      }
    } else if (n < sizeof(line) - 1) {
      line[n++] = c;
    } else {
      n = 0;                          // tràn → resync
    }
  }
}
```

Bên nhận có thể (tùy chọn) gửi khai báo `dev` để thương lượng rate/fields; nếu im lặng, producer tự
đẩy default hợp lý sau 500 ms (xem `HLP_v1.md §2.5`).

## 6. Trạng thái

- **Producer:** HLP/1 đã có trên Classic SPP bảo mật và BLE GATT.
- **Ví dụ ESP32:** đã có raw receiver BLE/SPP, framing giới hạn, handshake và ping/pong.
- **Firmware sản phẩm:** decoder field, driver màn hình và renderer tùy thuộc thiết bị.

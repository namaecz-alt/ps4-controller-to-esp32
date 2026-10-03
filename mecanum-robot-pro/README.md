# 🤖 Robot Mecanum — BẢN ĐẦY ĐỦ (dùng hết các nút trên tay PS4)

Phiên bản mở rộng của `mecanum-robot`: giữ nguyên phần di chuyển, **thêm chức năng phụ cho các nút còn lại**,
và **LED GPIO2 nháy nhanh / chậm** theo trạng thái kết nối.

> ⚠️ Mở ĐÚNG thư mục này trong VS Code (`File ▸ Open Folder` → `mecanum-robot-pro`), rồi **Upload and Monitor**.

---

## 📖 Mục lục

1. [Nguyên lý hoạt động](#-nguyên-lý-hoạt-động)
2. [Bảng điều khiển đầy đủ](#-bảng-điều-khiển-đầy-đủ)
3. [Sơ đồ nối dây](#-sơ-đồ-nối-dây)
4. [Bản đồ GPIO — các chân còn lại](#-bản-đồ-gpio--các-chân-còn-lại)
5. [Cấu hình](#-cấu-hình)
6. [Kiểm tra & chạy thử](#-kiểm-tra--chạy-thử)
7. [Xử lý sự cố](#-xử-lý-sự-cố)

---

## 🔬 Nguyên lý hoạt động

### Tổng quan — đường đi của một lệnh

```
  ┌──────────┐   Bluetooth Classic (BR/EDR)   ┌──────────────────────────┐
  │  Tay PS4 │ ─────────────────────────────▶ │  ESP32  +  Bluepad32     │
  │  (DS4)   │   ~100 gói tin / giây          │  (radio + stack BT)      │
  └──────────┘   (báo cáo HID 0x11)           └────────────┬─────────────┘
                                                           │
                                          uni_hid_parser_ds4.c giải mã
                                          (Bluepad32 nội bộ)
                                                           │
                                                           ▼
                                              đối tượng Controller (C++)
                                          ctl->dpad()  ctl->buttons() ...
                                                           │
        ┌──────────────────┬───────────────────────────────┼─────────────────┐
        ▼                  ▼                               ▼                 ▼
   D-Pad ▲▼◀▶        7 nút chức năng                   L2 / R2          Pin / sạc
   L1 / R1            X O [] /\ L3 R3 PS
        │                  │                               │
        ▼                  ▼                               ▼
  ĐỘNG HỌC          BẬT/TẮT ngõ ra                    % tốc độ
  vx, vy, w ──▶ PWM 4 bánh   GPIO 13/21/22/23
        │
        ▼
  RAMP (tăng tốc dần) ──▶ LEDC PWM ──▶ mạch cầu H ──▶ 4 động cơ ──▶ bánh mecanum
```

### 7 giai đoạn chi tiết

#### ① Khởi động — `setup()`

1. Mở `Serial` 115200 baud để in log.
2. Cấu hình **12 chân** cho 4 động cơ (L298N: `IN1`, `IN2`, `EN` mỗi động cơ).
3. Tạo **4 kênh PWM** bằng phần cứng LEDC của ESP32 (`ledcSetup` 5 kHz / 8 bit → duty 0…255)
   rồi gán chân (`ledcAttachPin`). Dùng PWM phần cứng nên **CPU không phải tự đếm xung**.
4. Cấu hình 4 chân chức năng phụ + LED trạng thái, tất cả kéo về mức tắt.
5. *(Tuỳ chọn `MOTOR_TEST_MODE 1`)* quay thử từng động cơ.
6. `BP32.setup(&onConnectedController, &onDisconnectedController)` — đăng ký 2 hàm callback
   và **khởi động Bluetooth**.

#### ② Tự động tìm & kết nối tay cầm

| Lần | Chuyện gì xảy ra |
|---|---|
| **Lần đầu** | ESP32 quét (inquiry). Tay cầm phải ở *chế độ ghép nối*: **giữ SHARE + PS** đến khi đèn nháy trắng. Khoá trao đổi được lưu vào **NVS** (bộ nhớ flash của ESP32). |
| **Các lần sau** | ESP32 đọc khoá cũ từ NVS. Bạn **chỉ cần bấm PS** — không cần giữ SHARE nữa, kể cả khi ESP32 vừa mất điện. |

Khi kết nối xong, Bluepad32 gọi `onConnectedController()` → in thông tin tay cầm → **LED chuyển sang nháy NHANH**.

#### ③ Nhận gói tin — `BP32.update()`

- Mỗi vòng `loop()` gọi `BP32.update()`. Hàm này trả về **`true` chỉ khi có dữ liệu mới**.
- Tay DS4 gửi liên tục khoảng **100 gói/giây** (báo cáo HID `0x11`: hat, 3 byte nút, 4 trục,
  2 trigger, gyro, accel). Bluepad32 giải mã sẵn thành các trường, chương trình chỉ việc đọc.
- Nếu không có dữ liệu mới → bỏ qua phần đọc lệnh, **chỉ làm 2 việc**: đẩy PWM ra động cơ
  (giai đoạn ⑥) và nháy LED. Nhờ vậy vòng lặp chạy rất nhẹ.

#### ④ Đọc lệnh từ tay cầm

| Nhóm | Đọc bằng | Thành |
|---|---|---|
| D-Pad | `ctl->dpad()` | `vx`, `vy` (mỗi chiều ±100) |
| L1 / R1 | `ctl->l1()`, `ctl->r1()` | `w` (±100) |
| L2 / R2 | `ctl->brake()`, `ctl->throttle()` | ép tốc độ chậm / nhanh |
| 7 nút phụ | `ctl->a() b() x() y() thumbL() thumbR() miscSystem()` | bitmask → chức năng |
| SHARE+OPTIONS | `ctl->miscSelect()`, `ctl->miscStart()` | giữ 2 s → ngắt kết nối |

> **⚠️ Ánh xạ nút rất dễ nhầm:** Bluepad32 đặt tên theo kiểu gamepad chung, **không** theo ký hiệu trên mặt DS4:
> `X(chéo) → a()` · `O(tròn) → b()` · `[](vuông) → x()` · `/\(tam giác) → y()`.

**Bắt cạnh lên (edge detection):** các nút dạng *bật/tắt* (đèn, cơ cấu, E-stop, đổi tốc độ,
đảo đầu) được xử lý theo **cạnh lên** — tức chỉ kích hoạt **đúng 1 lần** lúc bạn vừa bấm xuống,
dù bạn có giữ bao lâu. Riêng **còi (O)** xử lý theo **mức**: giữ thì kêu, nhả thì tắt.

#### ⑤ Động học → PWM

```
M1 =  vy + vx + w        (M1 = trước-trái)
M2 =  vy - vx + w        (M2 = sau-trái)
M3 = -vy + vx + w        (M3 = trước-phải)
M4 = -vy - vx + w        (M4 = sau-phải)
```

Sau đó **chuẩn hoá**: chia cả 4 cho giá trị tuyệt đối lớn nhất (nếu >1) rồi mới nhân % tốc độ.
Nhờ vậy bấm chéo (ví dụ ▲+▶) hay vừa tiến vừa xoay đều **giữ đúng hướng và không bao giờ vượt 100 % PWM**.

Kết quả (đã kiểm chứng bằng mô phỏng, `speed = 100 %`):

| Lệnh | M1 | M2 | M3 | M4 |
|---|---|---|---|---|
| TIẾN | +255 | +255 | −255 | −255 |
| LÙI | −255 | −255 | +255 | +255 |
| NGANG PHẢI | +255 | −255 | +255 | −255 |
| NGANG TRÁI | −255 | +255 | −255 | +255 |
| **XOAY PHẢI** | +255 | +255 | +255 | +255 |
| **XOAY TRÁI** | −255 | −255 | −255 | −255 |
| TIẾN + PHẢI | +255 | 0 | 0 | −255 |

#### ⑥ Xuất PWM ra động cơ

1. **RAMP (tăng tốc dần):** PWM hiện tại tiến dần về PWM đích, mỗi `RAMP_INTERVAL_MS` (20 ms)
   thay đổi tối đa `RAMP_STEP` (12). Tránh giật cơ khí và sụt áp nguồn.
2. **`MIN_PWM`:** nếu PWM khác 0 nhưng nhỏ hơn ngưỡng (mặc định 45) thì **nâng lên ngưỡng**
   — vì động cơ DC cần một lực tối thiểu mới quay được, đặc biệt khi có tải.
3. **`setMotor()`:** đặt 2 chân chiều (`IN1/IN2`) và ghi duty vào kênh LEDC.

#### ⑦ LED trạng thái & an toàn

**LED GPIO2 (không dùng `delay`, tính bằng `millis()`):**

| Trạng thái | Nháy | Chu kỳ |
|---|---|---|
| **Đã kết nối** tay cầm | 🔆 **NHANH** | 150 ms sáng / 150 ms tối |
| **Chưa kết nối** | 🔅 **CHẬM** | 700 ms sáng / 700 ms tối |

**An toàn (tự động, không cần can thiệp):**

- Mất kết nối → **dừng 4 động cơ** + **tắt hết chức năng phụ**
- Quá `DATA_TIMEOUT_MS` (500 ms) không có gói tin → **dừng 4 động cơ**
- **X (chéo)** → E-STOP: cắt động cơ, chức năng phụ vẫn hoạt động; bấm lại để chạy tiếp
- Không bấm gì → động cơ dừng

---

## 🎮 Bảng điều khiển đầy đủ

### Di chuyển

| Nút | Robot |
|---|---|
| D-Pad **▲** | TIẾN |
| D-Pad **▼** | LÙI |
| D-Pad **◀** | NGANG TRÁI |
| D-Pad **▶** | NGANG PHẢI |
| D-Pad **chéo** (▲+▶ …) | đi chéo 45° |
| **L1** | XOAY TRÁI tại chỗ |
| **R1** | XOAY PHẢI tại chỗ |
| **L2** (giữ) | chạy chậm 30 % |
| **R2** (giữ) | chạy nhanh 100 % |

### ⭐ Chức năng phụ (phần thêm của bản này)

| Nút | Chức năng | Chân | Kiểu |
|---|---|---|---|
| **X (chéo)** | 🛑 **DỪNG KHẨN CẤP** — bấm lại để chạy tiếp | — | bật/tắt |
| **O (tròn)** | 📢 **CÒI** | GPIO **21** | **giữ để kêu** |
| **[ ] (vuông)** | 💡 **ĐÈN PHA** | GPIO **13** | bật/tắt |
| **/\ (tam giác)** | ⚙️ **CƠ CẤU 1** (relay / bơm / nam châm / tay gắp…) | GPIO **22** | bật/tắt |
| **PS** | ⚙️ **CƠ CẤU 2** (xi lanh / đèn chớp / …) | GPIO **23** | bật/tắt |
| **L3** (ấn cần gạt trái) | 🎚️ đổi cấp tốc độ **CHẬM 35 % / TB 60 % / NHANH 100 %** | — | vòng |
| **R3** (ấn cần gạt phải) | 🔄 **ĐẢO ĐẦU ROBOT** (tiến↔lùi, trái↔phải) | — | bật/tắt |

### Hệ thống

| Thao tác | Kết quả |
|---|---|
| **SHARE + OPTIONS** giữ 2 giây | Ngắt kết nối tay cầm → robot dừng |
| Nhấn **EN** trên board | ESP32 khởi động lại, động cơ dừng |
| Giữ **PS** ~10 giây | Tắt nguồn tay cầm (cách dừng robot bằng phần cứng) |

> **Mẹo "đảo đầu robot" (R3):** robot mecanum thường đối xứng. Khi bạn quay người lại
> hoặc muốn "đuôi" thành "đầu", bấm R3 — D-Pad lên sẽ là phía sau robot. Rất tiện khi lùi vào góc hẹp.

---

## 🔌 Sơ đồ nối dây

### Động cơ — `DRIVER_L298N` *(mặc định)*

| Động cơ | Vị trí | IN1 | IN2 | EN |
|---|---|---|---|---|
| **M1** | Trước – Trái | GPIO 27 | GPIO 26 | GPIO 25 |
| **M2** | Sau – Trái | GPIO 33 | GPIO 32 | GPIO 14 |
| **M3** | Trước – Phải | GPIO 19 | GPIO 18 | GPIO 5 |
| **M4** | Sau – Phải | GPIO 17 | GPIO 16 | GPIO 4 |

### Động cơ — `DRIVER_BTS7960` (đổi `#define MOTOR_DRIVER`)

| Động cơ | RPWM | LPWM |
|---|---|---|
| **M1** | GPIO 27 | GPIO 26 |
| **M2** | GPIO 25 | GPIO 33 |
| **M3** | GPIO 32 | GPIO 14 |
| **M4** | GPIO 19 | GPIO 18 |

> Với BTS7960 phải nối **`R_EN` và `L_EN` lên 5 V**. Ưu điểm: chỉ tốn 8 chân → **chừa lại thêm 4 chân** (4, 5, 16, 17).

### Chức năng phụ

| Chức năng | Chân | Cách nối |
|---|---|---|
| Đèn pha | GPIO **13** | qua điện trở 220 Ω → LED (hoặc transistor/MOSFET nếu dải LED) |
| Còi | GPIO **21** | qua transistor/MOSFET hoặc module relay (còi điện thường 12 V) |
| Cơ cấu 1 | GPIO **22** | module relay, van điện từ, bơm… |
| Cơ cấu 2 | GPIO **23** | module relay, xi lanh… |
| LED trạng thái | GPIO **2** | LED có sẵn trên board DevKit |

> ⚠️ **Đừng cắm relay/còi/động cơ thẳng vào chân ESP32!** Chân ESP32 chỉ chịu ~12 mA và 3.3 V.
> Luôn đi qua transistor/MOSFET hoặc module relay có opto.
>
> Nếu dùng **module relay TQ** (loại phổ biến) — chúng thường **kích ở mức THẤP** →
> đổi `#define AUX_ACTIVE_LOW 1`.

### ⚡ Điện

- **Nguồn động cơ riêng** (7.4 V / 12 V / LiPo), đủ dòng. **Không** lấy 5 V từ ESP32.
- **Bắt buộc nối chung GND**: ESP32 ⟷ nguồn động cơ ⟷ mạch cầu H.
- Nếu cấp nguồn riêng cho L298N → **tháo jumper 5V-EN**.
- Nên có tụ 470–1000 µF sát đầu vào nguồn động cơ.

---

## 🗺️ Bản đồ GPIO — các chân còn lại

Tình trạng với **cấu hình mặc định** (L298N + 4 chức năng phụ + LED):

| Chân | Tình trạng | Ghi chú |
|---|---|---|
| GPIO 0 | 🟡 **TRỐNG** | ⚠️ Chân cấu hình (strapping), phải ở mức CAO khi khởi động. Dùng được nhưng cẩn thận |
| GPIO 1 (TX0) | ⚠️ Serial | Đang dùng cho USB/Serial Monitor — chỉ dùng khi bỏ Serial |
| **GPIO 2** | 🔵 ĐANG DÙNG | **LED trạng thái** (nháy nhanh/chậm) |
| GPIO 3 (RX0) | ⚠️ Serial | Như trên |
| GPIO 4 | 🔵 ĐANG DÙNG | M4 EN |
| GPIO 5 | 🔵 ĐANG DÙNG | M3 EN |
| GPIO 12 | 🟡 **TRỐNG** | ⚠️ Chân cấu hình — chọn điện áp flash, phải để THẤP khi khởi động |
| GPIO 13 | 🔵 ĐANG DÙNG | Đèn pha (VUÔNG) |
| GPIO 14 | 🔵 ĐANG DÙNG | M2 EN |
| GPIO 15 | 🟡 **TRỐNG** | ⚠️ Chân cấu hình — phải để THẤP khi khởi động |
| GPIO 16 | 🔵 ĐANG DÙNG | M4 IN2 |
| GPIO 17 | 🔵 ĐANG DÙNG | M4 IN1 |
| GPIO 18 | 🔵 ĐANG DÙNG | M3 IN2 |
| GPIO 19 | 🔵 ĐANG DÙNG | M3 IN1 |
| GPIO 21 | 🔵 ĐANG DÙNG | Còi (O) |
| GPIO 22 | 🔵 ĐANG DÙNG | Cơ cấu 1 (TAM GIÁC) |
| GPIO 23 | 🔵 ĐANG DÙNG | Cơ cấu 2 (PS) |
| GPIO 25 | 🔵 ĐANG DÙNG | M1 EN |
| GPIO 26 | 🔵 ĐANG DÙNG | M1 IN2 |
| GPIO 27 | 🔵 ĐANG DÙNG | M1 IN1 |
| GPIO 32 | 🔵 ĐANG DÙNG | M2 IN2 |
| GPIO 33 | 🔵 ĐANG DÙNG | M2 IN1 |
| **GPIO 34** | 🟢 **TRỐNG** | **CHỈ ĐỌC** (không xuất được) — cảm biến, nút nhấn, encoder |
| **GPIO 35** | 🟢 **TRỐNG** | **CHỈ ĐỌC** — ADC |
| **GPIO 36 (VP)** | 🟢 **TRỐNG** | **CHỈ ĐỌC** — ADC |
| **GPIO 39 (VN)** | 🟢 **TRỐNG** | **CHỈ ĐỌC** — ADC |
| GPIO 6 – 11 | ❌ KHÔNG DÙNG | Nối với flash bên trong, thường không ra header |

### Tóm tắt chân còn trống

| Loại | Chân |
|---|---|
| 🟢 **Dùng thoải mái (chỉ đọc/ADC)** | **34, 35, 36, 39** |
| 🟡 Dùng được nhưng **phải cẩn thận lúc khởi động** | **0, 12, 15** |
| ❌ Không dùng | 1, 3 (Serial), 6–11 (flash) |

### 💡 Hết chân? 4 cách nới rộng

| Cách | Tốn | Thêm được | Ghi chú |
|---|---|---|---|
| Đổi sang **BTS7960** (`MOTOR_DRIVER`) | 0 | **+4 chân** (4, 5, 16, 17) | Đơn giản nhất |
| **PCA9685** — 16 kênh PWM qua I²C | 2 chân (21 SDA / 22 SCL) | **+16 PWM** | Lái servo & LED rất hợp |
| **PCF8574 / MCP23017** — mở rộng I/O qua I²C | 2 chân | **+8 / +16 ngõ ra** | Rẻ, dễ dùng |
| **74HC595** — thanh ghi dịch | 3 chân | **+8 ngõ ra on/off** | Rẻ nhất, chỉ bật/tắt |

*(Nếu dùng I²C thì nhường lại GPIO 21/22 — đổi `AUX_HORN_PIN` / `AUX_RELAY1_PIN` sang chân khác.)*

---

## ⚙️ Cấu hình (đầu file `src/main.cpp`)

### Động cơ & tốc độ

| `#define` | Mặc định | Ý nghĩa |
|---|---|---|
| `MOTOR_DRIVER` | `DRIVER_L298N` | `DRIVER_L298N` (IN1+IN2+EN) hoặc `DRIVER_BTS7960` (RPWM+LPWM) |
| `MOTOR_PINS[4][3]` | xem trên | Chân GPIO 4 động cơ |
| `INVERT_M1 … M4` | `0` | Đổi `1` để đảo chiều động cơ đó |
| `SPEED_LEVELS[]` | `{35, 60, 100}` | 3 cấp tốc độ đổi bằng **L3** |
| `BOOST_SPEED_PCT` / `SLOW_SPEED_PCT` | `100` / `30` | Giữ **R2** / **L2** |
| `TRIGGER_ON` | `150` | Ngưỡng nhận L2/R2 (0…1020) |
| `MIN_PWM` | `45` | PWM tối thiểu để động cơ quay. Robot nặng → tăng 60–90 |
| `PWM_FREQ` / `PWM_RES` | `5000` / `8` | Tần số & độ phân giải PWM |
| `RAMP_STEP` / `RAMP_INTERVAL_MS` | `12` / `20` | Tăng tốc dần. Giật/sụt áp → giảm `RAMP_STEP` |
| `IDLE_MODE` | `IDLE_COAST` | `IDLE_COAST` thả trôi · `IDLE_BRAKE` phanh gấp |

### Chức năng phụ & LED

| `#define` | Mặc định | Ý nghĩa |
|---|---|---|
| `AUX_LIGHT_PIN` | `13` | Đèn pha (VUÔNG). `-1` = tắt |
| `AUX_HORN_PIN` | `21` | Còi (O, giữ). `-1` = tắt |
| `AUX_RELAY1_PIN` | `22` | Cơ cấu 1 (TAM GIÁC). `-1` = tắt |
| `AUX_RELAY2_PIN` | `23` | Cơ cấu 2 (PS). `-1` = tắt |
| `AUX_ACTIVE_LOW` | `0` | Module relay thường dùng `1` (kích mức THẤP) |
| `STATUS_LED_PIN` | `2` | LED trạng thái. `-1` = tắt |
| `LED_BLINK_FAST_MS` | `150` | Chu kỳ nháy khi **đã kết nối** |
| `LED_BLINK_SLOW_MS` | `700` | Chu kỳ nháy khi **chưa kết nối** |

### Khác

| `#define` | Mặc định | Ý nghĩa |
|---|---|---|
| `USE_ANALOG_STICK` | `0` | `1` = lái bằng cần gạt trái theo tỷ lệ |
| `DATA_TIMEOUT_MS` | `500` | Quá lâu không có gói tin → dừng động cơ |
| `MOTOR_TEST_MODE` | `0` | `1` = quay thử từng động cơ lúc khởi động |
| `PRINT_CHANGES` | `1` | In ra Serial mỗi khi lệnh thay đổi |

---

## ▶️ Kiểm tra & chạy thử

1. **Kê robot lên** (bánh không chạm đất) → đặt `MOTOR_TEST_MODE 1` → nạp.
   Từng động cơ quay 2 giây; bánh nào ngược thì đổi `INVERT_Mx 1` (hoặc hoán 2 dây).
2. Đổi lại `MOTOR_TEST_MODE 0` → nạp.
3. Bật tay cầm (**bấm PS**, lần đầu giữ **SHARE + PS**).
4. **LED trên board nháy NHANH** = đã kết nối. Nháy CHẬM = chưa kết nối.
5. Giữ **L2** (chậm) rồi thử D-Pad, L1/R1, rồi các nút chức năng.

Log mẫu:

```text
[    6231 ms][ROBOT]  [OK] DA KET NOI TAY CAM  (vi tri #0)   -> LED nhay NHANH
[    6231 ms][ROBOT]  DI CHUYEN : D-Pad len/xuong/trai/phai  | L1/R1 xoay | L2 cham | R2 nhanh
[    6231 ms][ROBOT]  X=DUNG KHAN CAP | O(giu)=COI | VUONG=DEN | TAM GIAC=CO CAU 1 | PS=CO CAU 2
[    7115 ms][ROBOT] TIEN       | toc= 60% | vx=  +0 vy=+100 w=  +0 | PWM: M1=+153 M2=+153 M3=-153 M4=-153
[    7420 ms][ROBOT] Den pha: BAT
[    7900 ms][ROBOT] XOAY PHAI  | toc= 60% | vx=  +0 vy=  +0 w=+100 | PWM: M1=+153 M2=+153 M3=+153 M4=+153
[    8150 ms][ROBOT] Cap toc do: NHANH (100%)
[    8600 ms][ROBOT] >>> DUNG KHAN CAP (E-STOP). Nhan X lan nua de chay tiep.
```

---

## 🧰 Xử lý sự cố

| Hiện tượng | Cách sửa |
|---|---|
| LED **không nháy** | `STATUS_LED_PIN` bị để `-1`, hoặc LED trên board khác chân → thử đổi sang `2` (hoặc `5` trên một số board) |
| LED nháy **chậm hoài**, không kết nối | Giữ **SHARE + PS** để vào chế độ ghép nối; xem thêm README ở thư mục gốc |
| Robot **không đi thẳng**, bị vẹo | Đổi `INVERT_Mx` hoặc hoán 2 dây động cơ đó |
| Robot **xoay tại chỗ** khi bấm tiến | Sai thứ tự bánh → kiểm tra M1..M4 đúng vị trí chưa |
| Động cơ kêu rè, không quay | Tăng `MIN_PWM` (60–90); kiểm tra nguồn đủ dòng |
| Động cơ giật / ESP32 reset | Sụt áp: nguồn riêng + tụ 1000 µF + giảm `RAMP_STEP` |
| **Đèn/relay bật ngược** (bấm lại tắt) | Đổi `AUX_ACTIVE_LOW` (0 ↔ 1) |
| Bấm nút nhưng không thấy log | Kiểm tra Serial 115200; `PRINT_CHANGES` phải = 1 |
| Cần thêm chân GPIO | Xem mục [Bản đồ GPIO](#-bản-đồ-gpio--các-chân-còn-lại) |
| Muốn thêm chức năng nữa | Xem hàm `handleAuxButtons()` — thêm 1 bit + 1 nhánh `if (pressed & …)` là xong |

---

## 📁 Các project trong repo

```
ps4-controller-to-esp32/
├── src/, platformio.ini, README.md   ← project gốc: chỉ đọc & in dữ liệu tay PS4
├── mecanum-robot/                    ← robot 4 bánh (chỉ di chuyển)
└── mecanum-robot-pro/                ← ⭐ bản này: di chuyển + chức năng phụ + LED trạng thái
```

Mỗi thư mục là một project PlatformIO **độc lập** — mở riêng từng cái trong VS Code.

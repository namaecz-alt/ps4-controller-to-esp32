# 🤖 Robot 4 bánh Mecanum — điều khiển bằng tay cầm PS4 (DualShock 4)

Project **độc lập**, dùng lại phần kết nối tay cầm PS4 của thư mục gốc, để lái một robot 4 bánh mecanum.

> ⚠️ **Mở ĐÚNG thư mục này** trong VS Code (`File ▸ Open Folder` → chọn `mecanum-robot`),
> rồi mới bấm **Upload and Monitor**. Đừng mở thư mục gốc của repo khi làm việc với project này.

---

## 🎮 Bảng điều khiển

| Nút trên tay PS4 | Robot làm gì |
|---|---|
| **D-Pad ▲** | Đi **TIẾN** |
| **D-Pad ▼** | Đi **LÙI** |
| **D-Pad ◀** | Đi ngang sang **TRÁI** |
| **D-Pad ▶** | Đi ngang sang **PHẢI** |
| **D-Pad ▲ + ▶** (chéo) | Đi chéo 45° **TIẾN-PHẢI** *(đặc sản của bánh mecanum)* |
| **L1** | **XOAY TRÁI** tại chỗ (ngược chiều kim đồng hồ) |
| **R1** | **XOAY PHẢI** tại chỗ (cùng chiều kim đồng hồ) |
| **L2** (giữ) | Chạy **chậm** (`SLOW_SPEED_PCT`, mặc định 30 %) |
| **R2** (giữ) | Chạy **nhanh** (`BOOST_SPEED_PCT`, mặc định 100 %) |
| **SHARE + OPTIONS** (giữ 2 giây) | **Ngắt kết nối** tay cầm → robot **DỪNG** |

**An toàn tự động** (rất quan trọng):

- Mất kết nối tay cầm → **dừng ngay 4 động cơ**
- Quá `DATA_TIMEOUT_MS` (500 ms) không nhận được gói tin → **dừng ngay 4 động cơ**
- Nhấn nút **EN** trên ESP32 → khởi động lại, động cơ dừng
- Không bấm gì → động cơ dừng

---

## 🔌 Sơ đồ nối dây

Có **2 kiểu mạch cầu H** được hỗ trợ, chọn bằng `#define MOTOR_DRIVER` ở đầu `src/main.cpp`.

### Cách 1 — `DRIVER_L298N` *(mặc định)*

Dùng cho **L298N, TB6612FNG, DRV8833** — mỗi động cơ cần 3 chân: `IN1`, `IN2`, `EN(PWM)`.

| Động cơ | Vị trí | IN1 | IN2 | EN (PWM) |
|---|---|---|---|---|
| **M1** | Trước – Trái | GPIO **27** | GPIO **26** | GPIO **25** |
| **M2** | Sau – Trái | GPIO **33** | GPIO **32** | GPIO **14** |
| **M3** | Trước – Phải | GPIO **19** | GPIO **18** | GPIO **5** |
| **M4** | Sau – Phải | GPIO **17** | GPIO **16** | GPIO **4** |

### Cách 2 — `DRIVER_BTS7960`

Dùng cho **BTS7960, Cytron MDD3A/MDD10A** — mỗi động cơ cần 2 chân PWM: `RPWM`, `LPWM`.

| Động cơ | Vị trí | RPWM | LPWM |
|---|---|---|---|
| **M1** | Trước – Trái | GPIO **27** | GPIO **26** |
| **M2** | Sau – Trái | GPIO **25** | GPIO **33** |
| **M3** | Trước – Phải | GPIO **32** | GPIO **14** |
| **M4** | Sau – Phải | GPIO **19** | GPIO **18** |

> Với BTS7960 bạn phải **nối `R_EN` và `L_EN` lên mức cao (5 V)** thì mạch mới chạy.

### ⚡ Điện — đọc kỹ, sai là cháy

| Việc | Làm thế nào |
|---|---|
| **Nguồn động cơ** | Dùng **nguồn riêng** (7.4 V / 12 V / pin LiPo…) đủ dòng — **KHÔNG** lấy 5 V từ chân ESP32 |
| **Chung mass** | Bắt buộc nối **GND của ESP32 ⟷ GND của nguồn động cơ / mạch cầu H** |
| **5 V cho ESP32** | Có thể lấy từ cổng USB, hoặc từ chân `5V` của mạch cầu H **nếu** mạch đó có ra 5 V |
| **Jumper 5V-EN (L298N)** | Nếu bạn **có** cấp nguồn riêng cho mạch, hãy **THÁO jumper** này |
| **Tụ & diode** | Nên có tụ 470–1000 µF sát đầu vào nguồn động cơ để chống sụt áp |

---

## 🧮 Động học — công thức đang dùng

Theo đúng cách lắp bạn mô tả: **bánh 1,2 cùng chiều · bánh 3,4 cùng chiều · động cơ lắp đối xứng**,
nên *đi thẳng* cần (1,2) ngược chiều (3,4), còn *xoay* thì cả 4 cùng một chiều.

Bảng này là **giá trị PWM thực tế** chương trình tính ra (đã kiểm chứng bằng mô phỏng, `speed = 100 %`):

| Lệnh | M1 (trước-trái) | M2 (sau-trái) | M3 (trước-phải) | M4 (sau-phải) | Kiểm tra |
|---|---|---|---|---|---|
| **TIẾN** | `+255` | `+255` | `−255` | `−255` | ✅ 1,2 ngược 3,4 |
| **LÙI** | `−255` | `−255` | `+255` | `+255` | ✅ 1,2 ngược 3,4 |
| **NGANG PHẢI** | `+255` | `−255` | `+255` | `−255` | ✅ 1,3 thuận / 2,4 nghịch |
| **NGANG TRÁI** | `−255` | `+255` | `−255` | `+255` | ✅ ngược lại |
| **XOAY PHẢI** | `+255` | `+255` | `+255` | `+255` | ✅ **cả 4 cùng chiều** |
| **XOAY TRÁI** | `−255` | `−255` | `−255` | `−255` | ✅ **cả 4 cùng chiều** |
| **TIẾN + PHẢI** | `+255` | `0` | `0` | `−255` | ✅ đi chéo 45° |

Công thức tổng quát (với `vx` = ngang phải, `vy` = tiến, `w` = xoay phải, mỗi biến −100…+100):

```
M1 =  vy + vx + w
M2 =  vy - vx + w
M3 = -vy + vx + w
M4 = -vy - vx + w
```

…rồi **chuẩn hoá** theo giá trị lớn nhất (để không bao giờ vượt 100 % PWM) và nhân với % tốc độ.
Nhờ chuẩn hoá mà việc bấm **chéo** hoặc **vừa tiến vừa xoay** vẫn giữ đúng hướng, không bị lệch.

---

## 🧪 Bước 1 — Kiểm tra đấu dây (nên làm trước khi cho robot chạy)

1. **Kê robot lên** cho 4 bánh không chạm đất (hoặc tháo bánh ra).
2. Trong `src/main.cpp` đổi:

   ```cpp
   #define MOTOR_TEST_MODE   1   /* 0 -> 1 */
   ```

3. Nạp chương trình, mở Serial Monitor. Robot sẽ **quay thử từng động cơ trong 2 giây**, in ra:

   ```text
   ########## CHE DO KIEM TRA DONG CO ##########
   [     512 ms][ROBOT] >>> Quay M1 TRUOC-TRAI  (+40%) trong 2000 ms...
   [    2512 ms][ROBOT] >>> Quay M2 SAU-TRAI  (+40%) trong 2000 ms...
   ...
   ########## XONG. Neu banh nao quay nguoc, doi INVERT_Mx = 1 ##########
   ```

4. Xem bánh nào quay **ngược** so với bảng động học ở trên → đổi cờ tương ứng:

   ```cpp
   #define INVERT_M2 1   /* ví dụ: bánh 2 quay ngược thì đổi thành 1 */
   ```

   (Hoặc đơn giản hơn: **hoán đổi 2 dây** của động cơ đó.)
5. Khi đã đúng hết → đổi `#define MOTOR_TEST_MODE 0` lại và nạp lần nữa.

---

## ▶️ Bước 2 — Chạy robot

1. `MOTOR_TEST_MODE` để lại `0`.
2. Nạp chương trình (**Upload and Monitor**).
3. Bật tay cầm: **bấm PS** (lần đầu thì giữ **SHARE + PS** để ghép nối).
4. **Giữ L2** (chế độ chậm) và thử từng nút D-Pad. Quan sát Serial Monitor:

```text
===============================================
[    6231 ms][ROBOT]  [OK] DA KET NOI TAY CAM  (vi tri #0)
[    6231 ms][ROBOT]    Ten        : DualShock 4 (model=34)
[    6231 ms][ROBOT]    Dia chi BT : A0:5A:5D:F9:C5:80
[    6231 ms][ROBOT]    Pin        : 78%
[    6231 ms][ROBOT] -----------------------------------------------
[    6231 ms][ROBOT]  D-Pad LEN/XUONG/TRAI/PHAI : di chuyen   |  L1/R1 : xoay trai/phai
[    6231 ms][ROBOT]  L2 = cham   |  R2 = nhanh  |  SHARE+OPTIONS 2s = ngat ket noi (dung robot)
===============================================
[    7115 ms][ROBOT] TIEN                  | toc= 30% | vx=  +0 vy=+100 w=  +0 | PWM: M1= +77 M2= +77 M3= -77 M4= -77
[    8190 ms][ROBOT] XOAY PHAI             | toc= 30% | vx=  +0 vy=  +0 w=+100 | PWM: M1= +77 M2= +77 M3= +77 M4= +77
[    9402 ms][ROBOT] NGANG TRAI            | toc= 30% | vx=-100 vy=  +0 w=  +0 | PWM: M1= -77 M2= +77 M3= -77 M4= +77
[   10520 ms][ROBOT] DUNG (khong bam gi)   | toc= 30% | vx=  +0 vy=  +0 w=  +0 | PWM: M1=  +0 M2=  +0 M3=  +0 M4=  +0
```

---

## ⚙️ Cấu hình (đầu file `src/main.cpp`)

| `#define` | Mặc định | Ý nghĩa |
|---|---|---|
| `MOTOR_DRIVER` | `DRIVER_L298N` | `DRIVER_L298N` (IN1+IN2+EN) hoặc `DRIVER_BTS7960` (RPWM+LPWM) |
| `MOTOR_PINS[4][3]` | xem trên | Chân GPIO cho 4 động cơ |
| `INVERT_M1 … M4` | `0` | Đổi thành `1` để đảo chiều động cơ đó |
| `BASE_SPEED_PCT` | `60` | Tốc độ mặc định (%) |
| `BOOST_SPEED_PCT` | `100` | Giữ **R2** |
| `SLOW_SPEED_PCT` | `30` | Giữ **L2** |
| `TRIGGER_ON` | `150` | Ngưỡng nhận L2/R2 (0…1020) |
| `MIN_PWM` | `45` | PWM tối thiểu để động cơ đủ lực quay. Robot nặng thì tăng (60–90) |
| `PWM_FREQ` / `PWM_RES` | `5000` / `8` | Tần số & độ phân giải PWM |
| `RAMP_STEP` | `12` | Tăng tốc dần (chống giật/sụt áp). `0` = ăn ngay |
| `RAMP_INTERVAL_MS` | `20` | Chu kỳ tăng tốc |
| `IDLE_MODE` | `IDLE_COAST` | `IDLE_COAST` = thả trôi · `IDLE_BRAKE` = phanh dừng gấp |
| `USE_ANALOG_STICK` | `0` | `1` = lái bằng **cần gạt trái** theo tỷ lệ (gạt nhẹ = chậm) |
| `STICK_DEADBAND` | `40` | Vùng chết của cần gạt |
| `MOTOR_TEST_MODE` | `0` | `1` = quay thử từng động cơ lúc khởi động |
| `DATA_TIMEOUT_MS` | `500` | Quá lâu không có gói tin → dừng động cơ |
| `DISCONNECT_HOLD_MS` | `2000` | Giữ SHARE+OPTIONS bao lâu thì ngắt kết nối |
| `STATUS_LED_PIN` | `2` | LED báo "đang có lệnh chạy" (`-1` = tắt) |

---

## 🧰 Xử lý sự cố

| Hiện tượng | Nguyên nhân & cách sửa |
|---|---|
| Robot **không đi thẳng**, bị vẹo | Một bánh quay ngược → đổi `INVERT_Mx` hoặc hoán đổi 2 dây động cơ đó |
| Robot **xoay tại chỗ** khi bấm tiến | Sai thứ tự bánh. Kiểm tra lại M1..M4 đúng vị trí (trước-trái / sau-trái / trước-phải / sau-phải) chưa |
| Động cơ **kêu rè nhưng không quay** | `MIN_PWM` quá nhỏ → tăng lên 60–90; hoặc nguồn không đủ dòng |
| Động cơ **giật / ESP32 reset** khi khởi động | Nguồn sụt áp. Dùng nguồn động cơ riêng, thêm tụ 1000 µF, tăng `RAMP_STEP` lên chậm hơn (giảm xuống 6) |
| Bấm nút nhưng **động cơ không chạy** | Xem Serial Monitor: nếu có dòng `[ROBOT] TIEN … PWM: M1=+77…` thì phần mềm OK → lỗi ở đấu dây/nguồn. Nếu **không có dòng nào** → tay cầm chưa kết nối |
| Không kết nối được tay cầm | Giữ **SHARE + PS** để vào chế độ ghép nối. Xem thêm README ở thư mục gốc |
| Bánh mecanum trượt khi đi ngang | Giảm tốc độ (`SLOW_SPEED_PCT`), tăng `MIN_PWM`, kiểm tra mặt sàn & tải trọng |
| Muốn dừng gấp thay vì trôi | Đổi `IDLE_MODE` thành `IDLE_BRAKE` |

---

## 📁 File trong thư mục này

```
mecanum-robot/
├── platformio.ini        ← cấu hình PlatformIO (có sẵn Bluepad32)
├── src/
│   └── main.cpp          ← toàn bộ chương trình robot
├── .vscode/
│   └── extensions.json   ← gợi ý cài PlatformIO IDE
└── README.md             ← tài liệu này
```

Project gốc đọc dữ liệu tay cầm (thuần) nằm ở **thư mục trên** (`../ps4-controller-to-esp32`):
`../src/main.cpp`, `../platformio.ini`, `../README.md`.

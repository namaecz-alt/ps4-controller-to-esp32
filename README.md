# Tay cầm PS4 (DualShock 4) ➜ ESP32

Dự án nhỏ giúp **ESP32 tự tìm và tự kết nối với tay cầm PS4 mỗi khi tay cầm được bật lên**, sau đó
**in toàn bộ dữ liệu tay cầm nhận được ra Serial Monitor** (mã nút dạng bitmask, D-Pad, cần gạt, L2/R2,
gyro/accel, pin…) và **in mã lỗi** khi không kết nối được.

Lập trình bằng **VS Code + PlatformIO**, chỉ cần cắm cáp USB vào ESP32 là chạy — không cần dùng
*SixaxisPairTool* hay bất kỳ công cụ đổi MAC nào.

> **Cách hoạt động (ngắn gọn):** Firmware dùng thư viện **Bluepad32**. Khi ESP32 khởi động, Bluepad32
> vào chế độ quét (inquiry) và tự kết nối tới tay cầm đang ở *chế độ ghép nối*. Sau lần ghép đầu tiên,
> khoá Bluetooth được lưu trong bộ nhớ NVS của ESP32, nên **các lần sau chỉ cần bấm nút PS** là ESP32
> tự kết nối lại (kể cả khi ESP32 vừa được cấp nguồn lại).

---

## ⚠️ Yêu cầu phần cứng — đọc trước khi làm

| | |
|---|---|
| ✅ **Dùng được** | ESP32 bản **cổ điển**: module **ESP32-WROOM-32 / WROOM-32E / WROVER / PICO-D4** — gồm các board **ESP32-DevKitC**, **ESP32 DevKit V1 / V4 (DOIT)**, NodeMCU-32S… (có **Bluetooth Classic**) |
| ❌ **Không dùng được** | Board dùng chip **ESP32-S3, ESP32-C3, ESP32-C6, ESP32-S2, ESP32-H2** (cũng hay được bán với tên có chữ *“DevKit”*: ESP32-S3-DevKitC-1, ESP32-C3-DevKitM-1, ESP32-C6-DevKitC-1…) → **không có Bluetooth Classic**, tay PS4 không thể kết nối |
| ✅ Tay cầm | DualShock 4 (PS4) — tay chính hãng Sony. Tay nhái (Datafrog, …) có thể không hoạt động |
| ✅ Phần mềm | VS Code + extension **PlatformIO IDE** |

### ❓ “ESP32 DevKit” của tôi có Bluetooth Classic không?

**Có nhé — miễn là board đó dùng chip ESP32 đời đầu.** “DevKit” chỉ là tên dạng board mạch, không phải tên chip.
`ESP32-DevKitC` (board chính hãng Espressif) và `ESP32 DevKit V1` (bản DOIT bán rất phổ biến) gắn module
**ESP32-WROOM-32** → **có Bluetooth Classic** → dùng bình thường với tay PS4.
Chỉ những board *trùng tên “DevKit”* nhưng dùng chip **S3 / C3 / C6** mới không có.

Theo tài liệu chính thức của Bluepad32: Bluetooth Classic (BR/EDR) **chỉ** có trên ESP32 đời đầu
(ESP32 / ESP32-D0WD / Pico W), **không** có trên ESP32-S3 / C3 / C6 / H2. Và tay PS4 (DualShock 4) chỉ
nói chuyện bằng BR/EDR → nên trên S3/C3 là **không thể** kết nối (bất kể code thế nào).

**4 cách kiểm tra nhanh board của bạn:**

1. **Nhìn module kim loại trên board** (chỗ có in chữ trắng):
   - `ESP32-WROOM-32`, `ESP32-WROOM-32E`, `ESP32-WROVER-B`, `ESP32-PICO-D4` → ✅ dùng được
   - `ESP32-S3-WROOM-1`, `ESP32-C3-MINI-1`, `ESP32-C6-WROOM-1` → ❌ không dùng được
2. **Nhìn hình dáng / số chân:**
   - ESP32 DevKit V1: **30 chân**, 1 cổng micro-USB. ESP32-DevKitC: **38 chân**, micro-USB hoặc USB-C.
     → thường là ESP32 cổ điển ✅
   - ESP32-S3-DevKitC-1: **44 chân** (2 hàng × 22), thường có **2 cổng USB-C** → S3 ❌
   - ESP32-C3 Super Mini / DevKitM: board **rất nhỏ**, 1 cổng USB-C (có khi chỉ ~20 chân) → C3 ❌
3. **Xem log lúc reset** (mở Serial Monitor 115200, bấm nút **EN**):
   - ESP32 cổ điển: `ets Jul 29 2019 ...` rồi `rst:0x1 (POWERON_RESET)...` và `chip is ESP32-D0WD...`
   - ESP32-S3: `ESP-ROM:esp32s3-20210327…`  •  ESP32-C3: `ESP-ROM:esp32c3-…` (tên chip ghi ngay ở dòng ROM)
4. **Hoặc để chương trình này tự báo:** lúc khởi động nó in dòng
   `Chip : ESP32 (co Bluetooth Classic - OK voi tay cam PS4)` — nếu là chip khác nó sẽ in
   `Chip : ESP32-S3 ...` kèm **mã lỗi E21**.

Lần build đầu tiên PlatformIO sẽ tự tải toolchain + framework (khoảng vài trăm MB), cần mạng Internet.

---

## 📁 Các file trong project

```
ps4-controller-to-esp32/
├── platformio.ini      ← cấu hình board, framework + Bluepad32
├── src/
│   └── main.cpp        ← TOÀN BỘ chương trình (có phần cấu hình ở đầu file)
├── .vscode/
│   └── extensions.json ← gợi ý cài extension PlatformIO
└── README.md           ← tài liệu bạn đang đọc
```

---

## 🛠️ Bước 1 — Cài môi trường (làm 1 lần)

1. Cài **VS Code**: <https://code.visualstudio.com/>
2. Mở VS Code → biểu tượng **Extensions** (Ctrl+Shift+X) → tìm **PlatformIO IDE** → **Install**.
3. Đợi PlatformIO cài xong (nó tự cài Python + lõi PlatformIO), sau đó **khởi động lại VS Code**.
4. Cài driver USB – Serial nếu máy tính chưa nhận cổng COM:
   - Board dùng chip **CP2102** → cài *Silicon Labs CP210x VCP driver*.
   - Board dùng chip **CH340** → cài *CH340 driver*.
5. Cắm ESP32 vào máy tính bằng cáp USB **có truyền dữ liệu** (nhiều cáp chỉ sạc, không có dây data).

## 🚀 Bước 2 — Nạp chương trình

1. Trong VS Code: **File → Open Folder…** → chọn thư mục `ps4-controller-to-esp32`.
2. Đợi PlatformIO quét xong project (thanh trạng thái dưới cùng hiện các biểu tượng).
3. Bấm nút **Upload and Monitor** (biểu tượng ✓ + mũi tên ➜ trên thanh PlatformIO), hoặc:
   - Bấm **→ (Upload)** để nạp code,
   - rồi bấm **🔌 (Serial Monitor)** để xem dữ liệu.
4. Nếu không tự chọn được cổng COM: mở `platformio.ini`, thêm dòng `upload_port = COMx` (Windows)
   hoặc `upload_port = /dev/ttyUSB0` (Linux/macOS).

> Nếu gặp lỗi *“Failed to connect to ESP32: Timed out waiting for packet header”*: giữ nút **BOOT**
> trên board trong lúc bấm Upload, nhả ra khi thấy “Connecting….”

## 🎮 Bước 3 — Ghép nối tay cầm với ESP32

**Lần đầu tiên (quan trọng):**

1. Tắt tay cầm (nếu đang bật — giữ nút PS ~10 giây cho tới khi đèn tắt).
2. Giữ đồng thời **SHARE + PS** khoảng **3–5 giây** → đèn tay cầm **nháy trắng liên tục** (chế độ ghép nối).
3. ESP32 (đang chạy firmware này) sẽ **tự tìm thấy và tự kết nối**. Khi thành công, Serial Monitor in ra
   khối thông tin `[OK] DA KET NOI TAY CAM` và đèn lightbar của tay cầm chuyển màu.

**Những lần sau:** chỉ cần **bấm nút PS**. ESP32 đã lưu khoá ghép nối nên tự kết nối lại trong ~1–3 giây.

**Muốn ghép lại từ đầu / tay cầm đang bị “kẹt” khoá cũ:** mở `src/main.cpp`, bỏ dấu `//` ở dòng
`// BP32.forgetBluetoothKeys();` trong `setup()`, nạp lại code, ghép nối lại như lần đầu, sau đó
**comment lại** dòng đó (để ESP32 nhớ tay cầm cho các lần sau).

---

## 📊 Bước 4 — Đọc dữ liệu in ra Serial Monitor

Ví dụ dữ liệu thật in ra khi bạn bấm nút **X** và đẩy cần trái:

```text
[    6231 ms][PS4] ---------------------------------------------------------------------------
[    6231 ms][PS4]   [OK] DA KET NOI TAY CAM  (vi tri #0)
[    6231 ms][PS4] ---------------------------------------------------------------------------
[    6231 ms][PS4]   Ten tay cam : DualShock 4
[    6231 ms][PS4]   Ma loai     : 34  (34 = DualShock 4 / PS4)
[    6231 ms][PS4]   VID : PID   : 0x054C : 0x05C4
[    6231 ms][PS4]   Dia chi BT  : A0:5A:5D:F9:C5:80
[    6231 ms][PS4]   Pin         : 200/255 (78%)
[    6231 ms][PS4]   Tinh nang   : rung=co  den=co  lightbar=co
[    6231 ms][PS4] ---------------------------------------------------------------------------
[    6249 ms][PS4][NUT ] #0 VUA NHAN : X(Cheo)
[    6249 ms][PS4][DATA] #0 btn=0x0001 dpad=0x00 misc=0x00 L=(   0,   0) R=(   0,   0) L2=   0 R2=   0 gyro=(     0,     0,     0) accel=(     0,     0,     0) pin=78%
[    6249 ms][PS4][NUT ] #0 dang nhan: X(Cheo)
[    6266 ms][PS4][NUT ] #0 VUA NHA  : X(Cheo)
[    6266 ms][PS4][DATA] #0 btn=0x0000 dpad=0x00 misc=0x00 L=(   0,   0) R=(   0,   0) L2=   0 R2=   0 gyro=(     0,     0,     0) accel=(     0,     0,     0) pin=78%
[    6266 ms][PS4][NUT ] #0 dang nhan: (khong nhan nut nao)
[    6300 ms][PS4][DATA] #0 btn=0x0000 dpad=0x00 misc=0x00 L=(   0,-508) R=(   0,   0) L2=   0 R2=   0 gyro=(     0,     0,     0) accel=(     0,     0,     0) pin=78%
```

### Ý nghĩa từng cột

| Cột | Ý nghĩa |
|---|---|
| `#0` | vị trí tay cầm (0–3, tối đa 4 tay cầm cùng lúc) |
| `btn=0x....` | **mã nút** dạng bitmask 16 bit (xem bảng bên dưới) |
| `dpad=0x..` | hướng D-Pad: `0x01`=LÊN, `0x02`=XUỐNG, `0x04`=PHẢI, `0x08`=TRÁI (nhấn chéo = OR hai bit) |
| `misc=0x..` | nút phụ: `0x01`=PS, `0x02`=SHARE, `0x04`=OPTIONS |
| `L=(x,y)` / `R=(x,y)` | cần gạt trái/phải, giá trị **−508 … +512** (0 = giữa) |
| `L2=` / `R2=` | lực bấm cò L2/R2, **0 … 1020** (trên DS4; 0 = không bấm) |
| `gyro=(x,y,z)` | con quay hồi chuyển |
| `accel=(x,y,z)` | gia tốc kế |
| `pin=%` | pin tay cầm (0% = không rõ) |

### Bảng mã nút — `btn`

| Bit | Giá trị | Nút trên tay PS4 |
|:---:|:---:|---|
| 0 | `0x0001` | **X** (Cross ✕) |
| 1 | `0x0002` | **O** (Circle ○) |
| 2 | `0x0004` | **□** (Square) |
| 3 | `0x0008` | **△** (Triangle) |
| 4 | `0x0010` | **L1** |
| 5 | `0x0020` | **R1** |
| 6 | `0x0040` | **L2** |
| 7 | `0x0080` | **R2** |
| 8 | `0x0100` | **L3** (ấn cần trái) |
| 9 | `0x0200` | **R3** (ấn cần phải) |

Ví dụ: giữ **L1 + X** thì `btn=0x0011`.

> Touchpad của DS4 (và cảm ứng đa điểm) **không** được thư viện đưa vào dữ liệu tay cầm — chỉ có các nút
> ở bảng trên. Nếu bạn cần touchpad, phải bật “thiết bị chuột ảo” (xem phần *Cấu hình nhanh*).

---

## 🔵 Về địa chỉ MAC bạn tra được: `A0:5A:5D:F9:C5:80`

Đây là **địa chỉ Bluetooth của chính tay cầm** (in ra ở dòng `Dia chi BT` khi kết nối → bạn có thể đối
chiếu để chắc chắn đúng tay cầm của mình).

> **Hỏi: MAC phải viết đúng chữ HOA/thường mới kết nối được à?**
> **Không.** Địa chỉ MAC là số hệ 16 (hex), chữ hoa hay thường **hoàn toàn như nhau**:
> `a0:5a:5d:f9:c5:80` = `A0:5A:5D:F9:C5:80` = `A0:5a:5D:f9:C5:80`. Firmware đọc bằng `sscanf("%x")`
> nên nhận cả hai. Quan trọng hơn: **firmware không dùng MAC để kết nối** — việc tìm & kết nối do
> Bluepad32 tự làm. MAC chỉ dùng để *lọc* (khi `USE_MAC_FILTER 1`) và để bạn *đối chiếu* mà thôi.
> Nếu không kết nối được thì nguyên nhân nằm ở chỗ khác (xem mục *Xử lý sự cố*), không phải hoa/thường.

Điều **quan trọng cần biết**: tay cầm PS4 hoạt động theo kiểu *“tay cầm nhớ địa chỉ của máy chủ”*.
Vì vậy:

* Với **cách này (Bluepad32)**: ESP32 **không cần biết trước MAC** của tay cầm. ESP32 tự quét, tự ghép
  nối (giống như một máy PS4), rồi tự lưu khoá → tự kết nối lại về sau. Bạn chỉ cần giữ **SHARE + PS**
  ở lần ghép đầu tiên.
* Nếu dùng **thư viện `PS4Controller` (aed3/PS4-esp32)** như nhiều video hướng dẫn: hàm `PS4.begin("...")`
  **không phải** nhận MAC của tay cầm, mà nhận **MAC mà tay cầm sẽ đi tìm**. Bạn phải dùng công cụ
  `sixaxispairer`/`SixaxisPairTool` để *ghi* MAC của ESP32 vào tay cầm — và thư viện đó không tự quét,
  không hỗ trợ core Arduino-ESP32 mới (hay lỗi `undefined reference to L2CA_ErtmConnectRsp`). Đó là lý do
  project này dùng Bluepad32.

### Tuỳ chọn: chỉ cho phép đúng tay cầm của bạn (lọc theo MAC)

Trong `src/main.cpp`:

```cpp
#define USE_MAC_FILTER 1                          // 1 = bật lọc theo MAC
#define TARGET_MAC "A0:5A:5D:F9:C5:80"            // MAC tay cầm của bạn
```

* `USE_MAC_FILTER 0` (mặc định): nhận mọi tay cầm — **nên để vậy khi mới bắt đầu**.
* `USE_MAC_FILTER 1`: nếu một tay cầm khác kết nối, ESP32 in mã **E11** và ngắt kết nối ngay.

---

## 🚨 Bảng mã lỗi in ra Serial Monitor

| Mã | Khi nào in | Cách xử lý |
|---|---|---|
| **E10** | Sau 15 giây chưa kết nối được tay cầm nào | Kiểm tra: tay cầm đã bật chưa? có đang ở chế độ ghép nối (giữ **SHARE + PS** tới khi đèn nháy trắng)? còn pin? xa quá 10 m? |
| **E11** | Tay cầm kết nối nhưng **không khớp** `TARGET_MAC` (khi `USE_MAC_FILTER 1`) | Đặt `USE_MAC_FILTER 0`, hoặc sửa lại `TARGET_MAC` cho đúng |
| **E12** | Mất kết nối với tay cầm | Bình thường khi bạn tắt tay cầm. ESP32 vẫn ở chế độ chờ → bấm **PS** để kết nối lại |
| **E13** | Thiết bị vừa kết nối là **bàn phím/chuột Bluetooth** (không phải tay cầm) | Không cần làm gì — ESP32 tự ngắt thiết bị đó. *(Không còn báo nhầm cho tay cầm nữa — xem mục xử lý sự cố bên dưới)* |
| **E14** | Tay cầm báo đã kết nối nhưng **3 giây không gửi dữ liệu** (bị treo) | ESP32 tự ngắt để kết nối lại. Nếu lặp nhiều lần → pin yếu hoặc nhiễu 2.4 GHz |
| **E15** | Đã đủ 4 tay cầm (`BP32_MAX_GAMEPADS`) | Ngắt bớt 1 tay cầm |
| **E20** | `TARGET_MAC` sai định dạng | Sửa về dạng `AA:BB:CC:DD:EE:FF` |
| **E21** | Chip không có **Bluetooth Classic** (ESP32-S3/C3/C6/S2…) | Phải dùng board ESP32 cổ điển |
| **E22** | Mất kết nối **≥ 5 lần liên tiếp** | Sạc pin, tắt/bật lại tay cầm, hoặc đặt `FORGET_BT_KEYS_ON_BOOT 1` rồi nạp lại để xoá khoá Bluetooth cũ |
| **E23** | Kết nối được **3 lần nhưng không lần nào có dữ liệu** → ESP32 **tự xoá khoá Bluetooth** và thử lại | Tắt tay cầm rồi **giữ SHARE + PS** để ghép nối lại từ đầu |

---

## ⚙️ Cấu hình nhanh (đầu file `src/main.cpp`)

| `#define` | Mặc định | Ý nghĩa |
|---|---|---|
| `USE_MAC_FILTER` | `0` | `1` = chỉ cho phép tay cầm có MAC bên dưới |
| `TARGET_MAC` | `"A0:5A:5D:F9:C5:80"` | MAC tay cầm của bạn (chỉ dùng khi bật lọc) |
| `PRINT_INTERVAL_MS` | `100` | Chu kỳ in dòng `[DATA]` (ms). `0` = chỉ in khi dữ liệu thay đổi |
| `WARN_NO_CONTROLLER_SEC` | `15` | Sau bao lâu không kết nối thì in **E10** |
| `WARN_REPEAT_SEC` | `30` | Nhắc lại cảnh báo mỗi bao nhiêu giây |
| `DATA_TIMEOUT_MS` | `3000` | Không có dữ liệu trong bao lâu thì coi là treo (**E14**) |
| `DISCONNECT_WARN_LIMIT` | `5` | Số lần mất kết nối liên tiếp trước khi in **E22** |
| `FORGET_BT_KEYS_ON_BOOT` | `0` | `1` = **xoá hết khoá Bluetooth** đã lưu trong ESP32 mỗi lần khởi động (dùng khi tay cầm kết nối được nhưng bị ngắt liên tục). Nhớ đổi lại `0` sau khi ghép nối xong |
| `FAILED_ATTEMPT_LIMIT` | `3` | Số lần kết nối được nhưng không có dữ liệu trước khi tự xoá khoá Bluetooth (**E23**) |
| `DEMO_LED_PIN` | `2` | Chân LED demo (GPIO2 = LED trên board DevKit). `-1` để tắt demo |

Muốn bật lại “chuột ảo” cho touchpad của DS4 (ESP32 sẽ báo thêm 1 thiết bị chuột khi chạm touchpad):
đổi `BP32.enableVirtualDevice(false);` → `true`.

---

## 💡 Dùng dữ liệu để điều khiển thiết bị khác (LED, relay, động cơ…)

Sửa / thêm vào hàm `loop()` (hoặc viết hàm `handleButtonActions()` rồi gọi trong `loop()`):

```cpp
ControllerPtr ctl = myControllers[0];
if (ctl != nullptr && ctl->isConnected()) {

  // 1) Bật/tắt relay theo nút (ví dụ nút X và nút O)
  digitalWrite(RELAY_1_PIN, ctl->a() ? HIGH : LOW);   // a() = nút X
  digitalWrite(RELAY_2_PIN, ctl->b() ? HIGH : LOW);   // b() = nút O

  // 2) Điều khiển tốc độ 2 động cơ bằng 2 cần gạt (PWM)
  //    Cần trái: axisY() trong khoảng -508..512 (đẩy lên = âm)
  int toc_do_trai  = map(ctl->axisY(),  -508, 512, -255, 255);
  int toc_do_phai  = map(ctl->axisRY(), -508, 512, -255, 255);
  // ledcWrite(MOTOR_LEFT_CH, abs(toc_do_trai)); ... (tuỳ driver động cơ của bạn)

  // 3) Cò L2/R2 dùng làm "ga" (0..1020 trên DS4)
  int ga = ctl->throttle();   // hoặc ctl->brake()
}
```

Các hàm đọc dữ liệu hay dùng: `a() b() x() y()` (nút ✕ ○ □ △), `l1() r1() l2() r2() thumbL() thumbR()`,
`miscSystem() miscSelect() miscStart()` (PS/SHARE/OPTIONS), `dpad()`, `axisX() axisY() axisRX() axisRY()`,
`brake() throttle()`, `gyroX()…`, `accelX()…`, `battery()`, `buttons()` (bitmask), `miscButtons()`.
Ngoài ra có thể điều khiển ngược lại tay cầm: `setColorLED(r,g,b)`, `setPlayerLEDs(bitmask)`,
`playDualRumble(delay_ms, duration_ms, weak, strong)`.

---

## 🧰 Xử lý sự cố

| Hiện tượng | Nguyên nhân thường gặp & cách xử lý |
|---|---|
| Serial Monitor không hiện gì | Sai cổng COM / sai baud (phải là **115200**) / chưa cắm cáp dữ liệu. Bấm nút **EN** trên board để reset |
| Build lỗi tải gói (`HTTPClientError`, timeout) | Mạng bị chặn → thử lại, hoặc dùng mạng khác/VPN. Xoá thư mục `.pio` rồi build lại |
| Upload lỗi `Timed out waiting for packet header` | Giữ nút **BOOT** khi bấm Upload; đổi cáp USB; giảm `upload_speed` trong `platformio.ini` (ví dụ `upload_speed = 115200`) |
| Mãi không thấy tay cầm (E10) | Tay cầm chưa ở chế độ ghép nối (**giữ SHARE + PS** tới khi đèn nháy trắng); pin yếu; tay nhái không hỗ trợ |
| Kết nối được nhưng vài giây lại mất (E12/E14/E22) | Pin yếu; nhiễu 2.4 GHz (WiFi/router gần đó — thử tắt WiFi); tay cầm nhái; thử `forgetBluetoothKeys()` rồi ghép lại |
| Board là ESP32-S3/C3/C6 | Không thể dùng với tay PS4 (xem **E21**) — cần board ESP32 cổ điển |
| Đã ghép với thiết bị khác (PS4, điện thoại, ESP32 khác) | Không sao: giữ **SHARE + PS** để ghép nối lại với ESP32 |
| Log có `DS4: Failed to create virtual device` | **Bình thường, không phải lỗi.** Dòng này do Bluepad32 in ra khi nó không tạo "chuột ảo" cho touchpad — vì firmware đã chủ động tắt tính năng đó (`BP32.enableVirtualDevice(false)`). Tay cầm vẫn hoạt động bình thường |
| Log có `sdp_query_timeout()` rồi tay cầm rớt | ESP32 đang giữ **khoá Bluetooth cũ bị lỗi** nên không đọc được thông tin tay cầm. Cách sửa: đặt `FORGET_BT_KEYS_ON_BOOT 1` trong `src/main.cpp` → nạp lại → **tắt tay cầm, giữ SHARE + PS** để ghép nối lại → đổi lại `0`. (Hoặc nạp với `upload_flags = --erase-all` trong `platformio.ini` để xoá sạch flash) |
| Trước đây thấy lỗi `E13 ... (class=0)` khi tay cầm vừa kết nối | **Đã sửa trong bản này.** Đó là lỗi của firmware cũ: kiểm tra `isGamepad()` ngay lúc tay cầm vừa kết nối, nhưng lúc đó Bluepad32 chưa nhận gói tin đầu tiên nên `class` vẫn = 0 (NONE) → firmware hiểu nhầm và **đá tay cầm ra**. Nay firmware chỉ loại bỏ bàn phím/chuột, còn tay cầm thì chờ dữ liệu đầu tiên |

---

## 📚 Ghi chú kỹ thuật & tài liệu tham khảo

* **Bluepad32** — thư viện mã nguồn mở của Ricardo Quesada, hỗ trợ ESP32 giả lập máy chủ Bluetooth
  (tự quét, tự ghép nối, tự kết nối lại) cho rất nhiều loại tay cầm: <https://github.com/ricardoquesada/bluepad32>
  (tài liệu: <https://bluepad32.readthedocs.io/>).
* `platformio.ini` dùng gói *Arduino-ESP32 + Bluepad32* (bản chính thức của tác giả Bluepad32, đã được
  đổi tên gói cho PlatformIO): <https://github.com/maxgerhardt/pio-framework-bluepad32>
* Ví dụ gốc của thư viện: `Bluepad32_ESP32 → Controller` (nằm trong gói framework ở trên).
* Nếu bạn muốn dùng **Arduino IDE** thay cho VS Code: cài board package
  `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
  rồi chọn board **ESP32 + Bluepad32 Arduino → ESP32 Dev Module**; code trong `src/main.cpp` chạy được
  tương tự (đổi tên file thành `main.ino` nếu cần).
* Nếu bạn muốn dùng **thư viện PS4Controller** (cách cũ, phải ghi MAC bằng `sixaxispairer`, và phải dùng
  Arduino-ESP32 core 1.0.6 để tránh lỗi liên kết) — xem README của aed3/PS4-esp32:
  <https://github.com/aed3/PS4-esp32>. Project này **không** dùng cách đó vì độ ổn định kém hơn.

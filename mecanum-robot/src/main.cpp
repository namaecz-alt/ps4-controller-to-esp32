/*====================================================================================
 *  ROBOT 4 BÁNH MECANUM  -  ĐIỀU KHIỂN BẰNG TAY CẦM PS4 (DualShock 4)
 *  ----------------------------------------------------------------------------------
 *  Phần mềm: Bluepad32 (có sẵn trong framework được khai báo ở platformio.ini)
 *  Phần cứng: ESP32 (bản có Bluetooth Classic) + 4 động cơ DC + 4 mạch cầu H
 *
 *  ĐIỀU KHIỂN:
 *      D-Pad LÊN      -> robot TIẾN
 *      D-Pad XUỐNG    -> robot LÙI
 *      D-Pad TRÁI     -> robot ĐI NGANG SANG TRÁI
 *      D-Pad PHẢI     -> robot ĐI NGANG SANG PHẢI
 *      (bấm chéo 2 hướng -> robot đi chéo 45°, ví dụ LÊN + PHẢI)
 *      L1             -> robot XOAY TẠI CHỖ SANG TRÁI  (ngược chiều kim đồng hồ)
 *      R1             -> robot XOAY TẠI CHỖ SANG PHẢI  (cùng chiều kim đồng hồ)
 *      L2 (giữ)       -> chạy CHẬM (SLOW_SPEED_PCT)
 *      R2 (giữ)       -> chạy NHANH (BOOST_SPEED_PCT)
 *      SHARE + OPTIONS giữ 2 giây -> ngắt kết nối tay cầm (robot DỪNG lại)
 *
 *  AN TOÀN (rất quan trọng với robot):
 *      - Mất kết nối tay cầm            -> DỪNG 4 động cơ ngay lập tức
 *      - Không nhận được gói tin quá lâu -> DỪNG 4 động cơ
 *      - Không bấm gì                   -> 4 động cơ dừng
 *
 *  CÁCH LẮP BÁNH (đúng với mô tả của bạn):
 *      Động cơ 1 = TRƯỚC-TRÁI      Động cơ 2 = SAU-TRÁI
 *      Động cơ 3 = TRƯỚC-PHẢI      Động cơ 4 = SAU-PHẢI
 *      - Bánh 1,2 quay cùng chiều; bánh 3,4 quay cùng chiều (động cơ lắp đối xứng).
 *      - Đi THẲNG : chiều quay của (1,2) NGƯỢC với (3,4)
 *      - XOAY     : cả 4 động cơ cùng 1 chiều
 *====================================================================================*/

#include <Bluepad32.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* =================================================================================
 *                                C Ấ U   H Ì N H
 * ================================================================================= */

/* ---------------- 1. Chọn loại mạch cầu H -------------------------------------- */
#define DRIVER_L298N   1 /* L298N / TB6612FNG / DRV8833 : mỗi động cơ cần IN1 + IN2 + EN(PWM) */
#define DRIVER_BTS7960 2 /* BTS7960 / Cytron MDD3A      : mỗi động cơ cần RPWM + LPWM       */
#define MOTOR_DRIVER   DRIVER_L298N

/* ---------------- 2. Chân GPIO nối vào mạch cầu H ------------------------------
 *  Thứ tự:  [0]=ĐC1 TRƯỚC-TRÁI   [1]=ĐC2 SAU-TRÁI
 *           [2]=ĐC3 TRƯỚC-PHẢI   [3]=ĐC4 SAU-PHẢI
 *
 *  Với MOTOR_DRIVER = DRIVER_L298N   : { IN1, IN2, EN }
 *  Với MOTOR_DRIVER = DRIVER_BTS7960 : { RPWM, LPWM, -1 }
 *
 *  Các chân này đã TRÁNH chân cấu hình (0, 2, 12, 15) và chân flash (6..11).
 *  Bạn đổi tuỳ theo cách đi dây của mình.
 * ------------------------------------------------------------------------------*/
#if MOTOR_DRIVER == DRIVER_L298N
static const int8_t MOTOR_PINS[4][3] = {
    {27, 26, 25}, /* ĐC1 TRƯỚC-TRÁI  : IN1, IN2, EN  */
    {33, 32, 14}, /* ĐC2 SAU-TRÁI    : IN1, IN2, EN  */
    {19, 18,  5}, /* ĐC3 TRƯỚC-PHẢI  : IN1, IN2, EN  */
    {17, 16,  4}, /* ĐC4 SAU-PHẢI    : IN1, IN2, EN  */
};
#else
static const int8_t MOTOR_PINS[4][3] = {
    {27, 26, -1}, /* ĐC1 TRƯỚC-TRÁI  : RPWM, LPWM, (không dùng) */
    {25, 33, -1}, /* ĐC2 SAU-TRÁI    : RPWM, LPWM, (không dùng) */
    {32, 14, -1}, /* ĐC3 TRƯỚC-PHẢI  : RPWM, LPWM, (không dùng) */
    {19, 18, -1}, /* ĐC4 SAU-PHẢI    : RPWM, LPWM, (không dùng) */
};
#endif

/* ---------------- 3. Đảo chiều từng động cơ -----------------------------------
 *  Chạy thử: nếu 1 bánh nào đó quay NGƯỢC so với dự định, đổi cờ tương ứng thành 1.
 *  (Cách khác: hoán đổi 2 dây của động cơ đó.)                                   */
#define INVERT_M1 0 /* ĐC1 TRƯỚC-TRÁI  */
#define INVERT_M2 0 /* ĐC2 SAU-TRÁI    */
#define INVERT_M3 0 /* ĐC3 TRƯỚC-PHẢI  */
#define INVERT_M4 0 /* ĐC4 SAU-PHẢI    */

/* ---------------- 4. Tốc độ ---------------------------------------------------- */
#define BASE_SPEED_PCT  60  /* Tốc độ mặc định (% PWM). 30..70 là dễ thử nhất */
#define BOOST_SPEED_PCT 100 /* Giữ R2 -> chạy nhanh tối đa                    */
#define SLOW_SPEED_PCT  30  /* Giữ L2 -> chạy chậm (dễ chỉnh, an toàn)        */
#define TRIGGER_ON      150 /* Ngưỡng L2/R2 coi như "đang bấm" (0..1020)      */

#define MIN_PWM   45  /* PWM nhỏ nhất để động cơ đủ lực quay (0..255). 0 = tắt */
#define PWM_FREQ  5000  /* Tần số PWM (Hz). 1000..20000                        */
#define PWM_RES   8     /* Độ phân giải PWM (bit) -> duty 0..255               */

/* Tăng tốc từ từ (chống giật, chống sụt áp). PWM thay đổi mỗi RAMP_STEP đơn vị
 * sau mỗi RAMP_INTERVAL_MS. Muốn "ăn ngay" thì đặt RAMP_STEP = 0.               */
#define RAMP_STEP        12
#define RAMP_INTERVAL_MS 20

/* ---------------- 5. Cách dừng khi không bấm gì -------------------------------- */
#define IDLE_COAST 0 /* 0 = THẢ TRÔI (cắt điện, robot lăn tự do rồi đứng) */
#define IDLE_BRAKE 1 /* 1 = PHANH    (nối tắt 2 đầu động cơ, dừng gấp)   */
#define IDLE_MODE  IDLE_COAST

/* ---------------- 6. Tuỳ chọn: cần gạt trái lái theo tỷ lệ ---------------------
 *  0 = chỉ dùng D-Pad (đúng như yêu cầu)
 *  1 = D-Pad VÀ cần gạt trái đều dùng được; cần gạt cho phép điều khiển mượt
 *      theo tỷ lệ (gạt nhẹ = chạy chậm, gạt hết = chạy nhanh)
 * -----------------------------------------------------------------------------*/
#define USE_ANALOG_STICK 0
#define STICK_DEADBAND   40 /* Vùng chết của cần gạt (0..512) */

/* ---------------- 7. Kiểm tra đấu dây (rất nên dùng lần đầu) -------------------
 *  1 = khi khởi động, quay thử TỪNG động cơ 2 giây (có in tên động cơ ra Serial)
 *      -> nhìn xem bánh nào quay, quay chiều nào, rồi chỉnh INVERT_Mx cho đúng.
 *      NHỚ KÊ ROBOT LÊN (bánh không chạm đất) trước khi bật chế độ này!
 *  0 = bỏ qua, chạy bình thường.
 * ------------------------------------------------------------------------------*/
#define MOTOR_TEST_MODE   0
#define MOTOR_TEST_PCT   40  /* Tốc độ khi test (%). Nên để nhỏ: 30..50 */
#define MOTOR_TEST_MS  2000  /* Thời gian quay mỗi động cơ (ms) */

/* ---------------- 8. An toàn & log -------------------------------------------- */
#define DATA_TIMEOUT_MS      500 /* Quá lâu không nhận gói tin -> DỪNG ĐỘNG CƠ */
#define DISCONNECT_HOLD_MS  2000 /* Giữ SHARE + OPTIONS để ngắt kết nối        */
#define STATUS_LED_PIN         2 /* LED báo có lệnh chạy (-1 = không dùng)     */
#define PRINT_PWM_CHANGES      1 /* In ra Serial mỗi khi lệnh chạy thay đổi    */

/* =================================================================================
 *                          K H A I   B Á O   C H U N G
 * ================================================================================= */

#define NUM_MOTORS 4
#define PWM_MAX    ((1 << PWM_RES) - 1) /* 255 với độ phân giải 8 bit */

static ControllerPtr myControllers[BP32_MAX_GAMEPADS];

static const int8_t MOTOR_INVERT[NUM_MOTORS] = {INVERT_M1, INVERT_M2, INVERT_M3, INVERT_M4};
static const char*  MOTOR_NAME[NUM_MOTORS]   = {"M1 TRUOC-TRAI", "M2 SAU-TRAI", "M3 TRUOC-PHAI", "M4 SAU-PHAI"};

static int   gTargetPwm[NUM_MOTORS]; /* PWM đích   (-255..+255) */
static int   gCurPwm[NUM_MOTORS];    /* PWM hiện tại (đã tăng tốc dần) */
static uint32_t gLastRampMs = 0;
static uint32_t gLastRxMs = 0;       /* thời điểm nhận gói tin cuối cùng */
static uint32_t gHoldStartMs = 0;    /* đang giữ SHARE + OPTIONS */
static bool  gConnected = false;

/* Lệnh chạy hiện tại (để so sánh và in khi thay đổi) */
typedef struct {
  int8_t vx;   /* -100..+100  : +100 = đi ngang sang PHẢI  */
  int8_t vy;   /* -100..+100  : +100 = TIẾN                */
  int8_t w;    /* -100..+100  : +100 = XOAY PHẢI           */
  uint8_t speed;
} cmd_t;
static cmd_t gCmd;

/* ============================ H À M   I N   L O G ============================== */

static void logFmt(const char* tag, const char* fmt, ...) {
  char buf[220];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%8lu ms][ROBOT]%s%s\n", (unsigned long)millis(), tag, buf);
}
#define logI(...) logFmt(" ", __VA_ARGS__)
#define logE(code, ...) logFmt("[LOI " code "] ", __VA_ARGS__)

/* ====================== Đ I Ề U   K H I Ể N   Đ Ộ N G   C Ơ ==================== */

/* Ghi 1 giá trị tốc độ (-255..+255) ra động cơ thứ i */
static void setMotor(int i, int speed) {
  if (MOTOR_INVERT[i]) speed = -speed;
  if (speed > PWM_MAX) speed = PWM_MAX;
  if (speed < -PWM_MAX) speed = -PWM_MAX;

  int mag = speed < 0 ? -speed : speed;
  if (mag > 0 && mag < MIN_PWM) mag = MIN_PWM; /* đảm bảo đủ lực để quay */

#if MOTOR_DRIVER == DRIVER_L298N
  /* L298N / TB6612: IN1, IN2 chọn chiều; EN nhận PWM */
  const int8_t* p = MOTOR_PINS[i];
  if (speed > 0) {
    digitalWrite(p[0], HIGH);
    digitalWrite(p[1], LOW);
    ledcWrite((uint8_t)i, (uint32_t)mag);
  } else if (speed < 0) {
    digitalWrite(p[0], LOW);
    digitalWrite(p[1], HIGH);
    ledcWrite((uint8_t)i, (uint32_t)mag);
  } else {
#if IDLE_MODE == IDLE_BRAKE
    digitalWrite(p[0], HIGH);
    digitalWrite(p[1], HIGH);
    ledcWrite((uint8_t)i, PWM_MAX); /* phanh: nối tắt 2 đầu động cơ */
#else
    digitalWrite(p[0], LOW);
    digitalWrite(p[1], LOW);
    ledcWrite((uint8_t)i, 0); /* thả trôi */
#endif
  }

#else
  /* BTS7960 / Cytron: RPWM quay thuận, LPWM quay nghịch */
  uint8_t chF = (uint8_t)(i * 2);
  uint8_t chB = (uint8_t)(i * 2 + 1);
  if (speed > 0) {
    ledcWrite(chF, (uint32_t)mag);
    ledcWrite(chB, 0);
  } else if (speed < 0) {
    ledcWrite(chF, 0);
    ledcWrite(chB, (uint32_t)mag);
  } else {
    ledcWrite(chF, IDLE_MODE == IDLE_BRAKE ? PWM_MAX : 0);
    ledcWrite(chB, IDLE_MODE == IDLE_BRAKE ? PWM_MAX : 0);
  }
#endif
}

/* Dừng toàn bộ (theo chế độ IDLE_MODE) */
static void stopAllMotors(void) {
  for (int i = 0; i < NUM_MOTORS; i++) {
    gTargetPwm[i] = 0;
    gCurPwm[i] = 0;
    setMotor(i, 0);
  }
}

/* Tăng/giảm tốc dần (ramp). Gọi định kỳ trong loop(). */
static void applyRamp(void) {
  if (RAMP_STEP <= 0) {
    for (int i = 0; i < NUM_MOTORS; i++) gCurPwm[i] = gTargetPwm[i];
  } else if (millis() - gLastRampMs >= RAMP_INTERVAL_MS) {
    gLastRampMs = millis();
    for (int i = 0; i < NUM_MOTORS; i++) {
      int d = gTargetPwm[i] - gCurPwm[i];
      if (d > RAMP_STEP) d = RAMP_STEP;
      if (d < -RAMP_STEP) d = -RAMP_STEP;
      gCurPwm[i] += d;
    }
  }
  for (int i = 0; i < NUM_MOTORS; i++) setMotor(i, gCurPwm[i]);
}

/* ============================ Đ Ộ N G   H Ọ C   R O B O T ======================
 *  vx : +100 = đi ngang sang PHẢI,  -100 = sang TRÁI
 *  vy : +100 = TIẾN,                -100 = LÙI
 *  w  : +100 = XOAY PHẢI (cùng chiều kim đồng hồ), -100 = XOAY TRÁI
 *
 *  Theo cách lắp bánh của bạn (1,2 cùng chiều / 3,4 cùng chiều, động cơ đối xứng):
 *      ĐI THẲNG  : (1,2) NGƯỢC chiều (3,4)   ->  [+1, +1, -1, -1]
 *      ĐI NGANG  : 1,3 cùng chiều, ngược 2,4 ->  PHẢI = [+1, -1, +1, -1]
 *      XOAY      : cả 4 cùng 1 chiều          ->  [+1, +1, +1, +1]
 *
 *  Từ 3 trường hợp trên suy ra công thức (đã chuẩn hoá):
 *      M1 =  vy + vx + w
 *      M2 =  vy - vx + w
 *      M3 = -vy + vx + w
 *      M4 = -vy - vx + w
 *  Sau đó chia cho giá trị lớn nhất để không vượt quá 100% (giữ đúng hướng).
 * ==============================================================================*/
static void computeWheelPwm(const cmd_t* c) {
  float vx = (float)c->vx / 100.0f;
  float vy = (float)c->vy / 100.0f;
  float w  = (float)c->w  / 100.0f;

  float m[4];
  m[0] =  vy + vx + w;
  m[1] =  vy - vx + w;
  m[2] = -vy + vx + w;
  m[3] = -vy - vx + w;

  /* Chuẩn hoá: giá trị tuyệt đối lớn nhất = 1 */
  float mx = 0.0f;
  for (int i = 0; i < 4; i++) {
    float a = m[i] < 0 ? -m[i] : m[i];
    if (a > mx) mx = a;
  }
  if (mx > 1.0f) {
    for (int i = 0; i < 4; i++) m[i] /= mx;
  }

  /* Nhân với tốc độ (%) -> PWM */
  float scale = (float)c->speed / 100.0f * (float)PWM_MAX;
  for (int i = 0; i < 4; i++) {
    gTargetPwm[i] = (int)(m[i] * scale + (m[i] >= 0 ? 0.5f : -0.5f));
  }
}

/* ============================ Đ Ọ C   L Ệ N H   T Ừ   T A Y   C Ầ M =========== */

/* Đọc D-Pad + L1/R1 + L2/R2 -> điền vào cmd */
static void readCommand(ControllerPtr ctl, cmd_t* c) {
  c->vx = 0;
  c->vy = 0;
  c->w = 0;

  /* --- D-Pad: hướng di chuyển (hỗ trợ cả bấm chéo) --- */
  uint8_t dpad = ctl->dpad();
  if (dpad & DPAD_UP)    c->vy += 100;
  if (dpad & DPAD_DOWN)  c->vy -= 100;
  if (dpad & DPAD_RIGHT) c->vx += 100;
  if (dpad & DPAD_LEFT)  c->vx -= 100;

  /* --- Tuỳ chọn: cần gạt trái điều khiển theo tỷ lệ --- */
#if USE_ANALOG_STICK
  int ax = ctl->axisX(); /* -512..+512, + = gạt sang PHẢI */
  int ay = ctl->axisY(); /* -512..+512, - = gạt lên TRÊN  */
  if (ax > STICK_DEADBAND || ax < -STICK_DEADBAND) c->vx = (int8_t)(ax * 100 / 512);
  if (ay > STICK_DEADBAND || ay < -STICK_DEADBAND) c->vy = (int8_t)(-ay * 100 / 512);
#endif

  /* --- L1 / R1: xoay tại chỗ --- */
  if (ctl->l1()) c->w -= 100; /* L1 = xoay TRÁI  */
  if (ctl->r1()) c->w += 100; /* R1 = xoay PHẢI  */

  /* --- L2 / R2: chọn tốc độ --- */
  c->speed = BASE_SPEED_PCT;
  if (ctl->brake() > TRIGGER_ON)    c->speed = SLOW_SPEED_PCT;  /* L2 = chậm  */
  if (ctl->throttle() > TRIGGER_ON) c->speed = BOOST_SPEED_PCT; /* R2 = nhanh */
}

/* Tên hành động để in ra (tiếng Việt, không dấu) */
static void commandName(const cmd_t* c, char* buf, size_t size) {
  buf[0] = 0;
  if (c->vx == 0 && c->vy == 0 && c->w == 0) {
    snprintf(buf, size, "DUNG (khong bam gi)");
    return;
  }
  const char* v = "";
  const char* h = "";
  if (c->vy > 0) v = "TIEN";
  if (c->vy < 0) v = "LUI";
  if (c->vx > 0) h = "PHAI";
  if (c->vx < 0) h = "TRAI";

  if (v[0] && h[0]) snprintf(buf, size, "%s-%s", v, h);
  else if (v[0])    snprintf(buf, size, "%s", v);
  else if (h[0])    snprintf(buf, size, "NGANG %s", h);
  else              buf[0] = 0;

  if (c->w != 0) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s%sXOAY %s", buf, buf[0] ? " + " : "", c->w > 0 ? "PHAI" : "TRAI");
    snprintf(buf, size, "%s", tmp);
  }
}

/* ================================== C A L L B A C K =========================== */

void onConnectedController(ControllerPtr ctl) {
  /* LƯU Ý: KHÔNG kiểm tra isGamepad() ở đây — lúc này Bluepad32 chưa nhận gói tin
   * đầu tiên nên class vẫn = 0 (chưa xác định). Chỉ loại bàn phím / chuột. */
  if (ctl->isMouse() || ctl->isKeyboard()) {
    logE("E13", "Thiet bi vua ket noi khong phai tay cam. Da ngat ket noi.");
    ctl->disconnect();
    return;
  }

  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      myControllers[i] = ctl;
      ControllerProperties props = ctl->getProperties();
      logI("===============================================");
      logI(" [OK] DA KET NOI TAY CAM  (vi tri #%d)", i);
      logI("   Ten        : %s (model=%d)", ctl->getModelName().c_str(), ctl->getModel());
      logI("   Dia chi BT : %02X:%02X:%02X:%02X:%02X:%02X", props.btaddr[0], props.btaddr[1], props.btaddr[2],
           props.btaddr[3], props.btaddr[4], props.btaddr[5]);
      logI("   Pin        : %u%%", (unsigned)((uint32_t)ctl->battery() * 100u / 255u));
      logI("-----------------------------------------------");
      logI(" D-Pad LEN/XUONG/TRAI/PHAI : di chuyen   |  L1/R1 : xoay trai/phai");
      logI(" L2 = cham   |  R2 = nhanh  |  SHARE+OPTIONS 2s = ngat ket noi (dung robot)");
      logI("===============================================");
      gConnected = true;
      gLastRxMs = millis();
      gHoldStartMs = 0;
      return;
    }
  }
  logE("E15", "Da 4 tay cam, khong nhan them. Da ngat ket noi.");
  ctl->disconnect();
}

void onDisconnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      myControllers[i] = nullptr;
      logE("E12", "Mat ket noi tay cam #%d  ->  DUNG NGAY 4 DONG CO.", i);
      stopAllMotors();            /* AN TOÀN: dừng robot khi mất kết nối */
      gConnected = false;
      gHoldStartMs = 0;
      memset(&gCmd, 0, sizeof(gCmd));
      logI("Bam nut PS tren tay cam de ket noi lai.");
      return;
    }
  }
}

/* In lệnh chạy + PWM 4 bánh (chỉ khi thay đổi) */
static void printCommand(const cmd_t* c) {
  char name[64];
  commandName(c, name, sizeof(name));
  logI("%-22s | toc=%3u%% | vx=%+4d vy=%+4d w=%+4d | PWM: M1=%+4d M2=%+4d M3=%+4d M4=%+4d", name, c->speed, c->vx,
       c->vy, c->w, gTargetPwm[0], gTargetPwm[1], gTargetPwm[2], gTargetPwm[3]);
}

/* Quay thử từng động cơ để kiểm tra đấu dây (chỉ chạy khi MOTOR_TEST_MODE = 1) */
static void runMotorTest(void) {
  Serial.println();
  Serial.println(F("########## CHE DO KIEM TRA DONG CO ##########"));
  Serial.println(F("#  Nho KE ROBOT LEN cho 4 banh khong cham dat!"));
  Serial.println(F("##############################################"));
  for (int i = 0; i < NUM_MOTORS; i++) {
    int pwm = (int)((long)MOTOR_TEST_PCT * PWM_MAX / 100);
    logI(">>> Quay %s  (+%d%%) trong %d ms...", MOTOR_NAME[i], MOTOR_TEST_PCT, MOTOR_TEST_MS);
    for (int k = 0; k < NUM_MOTORS; k++) setMotor(k, (k == i) ? pwm : 0);
    delay(MOTOR_TEST_MS);
    for (int k = 0; k < NUM_MOTORS; k++) setMotor(k, 0);
    delay(500);
  }
  stopAllMotors();
  Serial.println(F("########## XONG. Neu banh nao quay nguoc, doi INVERT_Mx = 1 ##########"));
  Serial.println();
}

/* ================================== S E T U P ================================= */

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("  ROBOT 4 BANH MECANUM  -  Dieu khien bang PS4  "));
  Serial.println(F("================================================"));

  /* --- Cấu hình chân động cơ --- */
  for (int i = 0; i < NUM_MOTORS; i++) {
    const int8_t* p = MOTOR_PINS[i];
#if MOTOR_DRIVER == DRIVER_L298N
    pinMode(p[0], OUTPUT);
    pinMode(p[1], OUTPUT);
    digitalWrite(p[0], LOW);
    digitalWrite(p[1], LOW);
    ledcSetup((uint8_t)i, PWM_FREQ, PWM_RES);
    ledcAttachPin((uint8_t)p[2], (uint8_t)i);
    ledcWrite((uint8_t)i, 0);
    logI("Dong co %s : IN1=GPIO%d  IN2=GPIO%d  EN(PWM)=GPIO%d", MOTOR_NAME[i], p[0], p[1], p[2]);
#else
    ledcSetup((uint8_t)(i * 2), PWM_FREQ, PWM_RES);
    ledcSetup((uint8_t)(i * 2 + 1), PWM_FREQ, PWM_RES);
    ledcAttachPin((uint8_t)p[0], (uint8_t)(i * 2));
    ledcAttachPin((uint8_t)p[1], (uint8_t)(i * 2 + 1));
    ledcWrite((uint8_t)(i * 2), 0);
    ledcWrite((uint8_t)(i * 2 + 1), 0);
    logI("Dong co %s : RPWM=GPIO%d  LPWM=GPIO%d", MOTOR_NAME[i], p[0], p[1]);
#endif
  }
  stopAllMotors();

#if MOTOR_TEST_MODE
  runMotorTest();
#endif

#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
#endif

  logI("Toc do: mac dinh=%d%%  cham(L2)=%d%%  nhanh(R2)=%d%%  PWM min=%d", BASE_SPEED_PCT, SLOW_SPEED_PCT,
       BOOST_SPEED_PCT, MIN_PWM);

  /* --- Bluepad32 --- */
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.enableNewBluetoothConnections(true);
  BP32.enableVirtualDevice(false); /* không cần chuột ảo của touchpad */

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
  logE("E21", "Chip nay KHONG co Bluetooth Classic => KHONG the dung tay cam PS4. "
              "Hay dung board ESP32 (WROOM-32 / DevKitC).");
#endif

  logI("San sang. Bat tay cam PS4 (lan dau: giu SHARE + PS)...");
}

/* =================================== L O O P ================================== */

void loop() {
  bool updated = BP32.update();

  if (updated) {
    gLastRxMs = millis();
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
      ControllerPtr ctl = myControllers[i];
      if (ctl == nullptr || !ctl->isConnected() || !ctl->hasData()) continue;

      /* --- A) Tổ hợp SHARE + OPTIONS: ngắt kết nối (robot dừng lại) --- */
      if (ctl->miscSelect() && ctl->miscStart()) {
        if (gHoldStartMs == 0) {
          gHoldStartMs = millis();
          logI("Dang giu SHARE + OPTIONS... giu them %d ms de ngat ket noi (robot se dung).", DISCONNECT_HOLD_MS);
        } else if (millis() - gHoldStartMs >= (uint32_t)DISCONNECT_HOLD_MS) {
          gHoldStartMs = 0;
          logI("Ngat ket noi theo yeu cau -> DUNG ROBOT. Bam PS de ket noi lai.");
          stopAllMotors();
          ctl->disconnect();
          continue;
        }
      } else {
        gHoldStartMs = 0;
      }

      /* --- B) Đọc lệnh từ tay cầm --- */
      cmd_t c;
      readCommand(ctl, &c);
      if (memcmp(&c, &gCmd, sizeof(c)) != 0) {
        gCmd = c;
        computeWheelPwm(&gCmd);
#if PRINT_PWM_CHANGES
        printCommand(&gCmd);
#endif
      }
      break; /* dùng tay cầm đầu tiên */
    }
  }

  /* --- C) An toàn: quá lâu không nhận được gói tin -> dừng --- */
  if (gConnected && (millis() - gLastRxMs > DATA_TIMEOUT_MS)) {
    logE("E14", "Khong nhan du lieu tu tay cam trong %lu ms -> DUNG NGAY 4 DONG CO.",
         (unsigned long)(millis() - gLastRxMs));
    memset(&gCmd, 0, sizeof(gCmd));
    stopAllMotors();
    gLastRxMs = millis();
  }

  /* --- D) Đưa PWM ra động cơ (có tăng tốc dần) --- */
  applyRamp();

#if STATUS_LED_PIN >= 0
  bool moving = false;
  for (int i = 0; i < NUM_MOTORS; i++)
    if (gCurPwm[i] != 0) moving = true;
  digitalWrite(STATUS_LED_PIN, moving ? HIGH : LOW);
#endif

  delay(5); /* nhường CPU cho Bluetooth */
}

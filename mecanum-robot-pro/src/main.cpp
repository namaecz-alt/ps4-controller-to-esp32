/*====================================================================================
 *  ROBOT 4 BÁNH MECANUM  -  BẢN ĐẦY ĐỦ (dùng TẤT CẢ các nút trên tay PS4)
 *  ----------------------------------------------------------------------------------
 *  Phần mềm : Bluepad32 (có sẵn trong framework khai báo ở platformio.ini)
 *  Phần cứng: ESP32 (bản có Bluetooth Classic) + 4 động cơ DC + 4 mạch cầu H
 *
 *  ------------------------------------------------------------------
 *  BẢNG ĐIỀU KHIỂN ĐẦY ĐỦ
 *  ------------------------------------------------------------------
 *  DI CHUYỂN
 *    D-Pad ▲ / ▼ / ◀ / ▶      TIẾN / LÙI / NGANG TRÁI / NGANG PHẢI
 *    D-Pad chéo (▲+▶ ...)     đi chéo 45°
 *    L1 / R1                  XOAY TRÁI / XOAY PHẢI tại chỗ
 *    L2 (giữ)                 chạy chậm tạm thời      (SLOW_SPEED_PCT)
 *    R2 (giữ)                 chạy nhanh tạm thời     (BOOST_SPEED_PCT)
 *
 *  CHỨC NĂNG PHỤ (phần thêm của bản này)
 *    X  (CHÉO)     bấm        DỪNG KHẨN CẤP (bấm lại để chạy tiếp)
 *    O  (TRÒN)     GIỮ        CÒI / còi hú  (AUX_HORN_PIN)
 *    VUÔNG         bấm        ĐÈN PHA bật/tắt         (AUX_LIGHT_PIN)
 *    TAM GIÁC      bấm        CƠ CẤU 1 bật/tắt        (AUX_RELAY1_PIN)
 *    PS            bấm        CƠ CẤU 2 bật/tắt        (AUX_RELAY2_PIN)
 *    L3 (ấn cần L) bấm        đổi cấp tốc độ CHẬM / TB / NHANH
 *    R3 (ấn cần R) bấm        ĐẢO ĐẦU ROBOT (tiến<->lùi, trái<->phải)
 *
 *  HỆ THỐNG
 *    SHARE + OPTIONS (giữ 2s) ngắt kết nối tay cầm (robot dừng)
 *    LED GPIO2                NHÁY NHANH = đã kết nối | NHÁY CHẬM = chưa kết nối
 *
 *  ------------------------------------------------------------------
 *  LƯU Ý ÁNH XẠ NÚT (rất dễ nhầm!): Bluepad32 đặt tên theo kiểu chung của gamepad,
 *  KHÔNG theo ký hiệu trên mặt DS4:
 *        X  (chéo)  -> ctl->a()
 *        O  (tròn)  -> ctl->b()
 *        [ ] (vuông)-> ctl->x()
 *        /\ (tam giác) -> ctl->y()
 *====================================================================================*/

#include <Bluepad32.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* =================================================================================
 *                                C Ấ U   H Ì N H
 * ================================================================================= */

/* ---------------- 1. Chọn loại mạch cầu H -------------------------------------- */
#define DRIVER_L298N   1 /* L298N / TB6612FNG / DRV8833 : IN1 + IN2 + EN(PWM) */
#define DRIVER_BTS7960 2 /* BTS7960 / Cytron MDD3A      : RPWM + LPWM        */
#define MOTOR_DRIVER   DRIVER_L298N

/* ---------------- 2. Chân GPIO 4 động cơ ---------------------------------------
 *  Thứ tự: [0]=ĐC1 TRƯỚC-TRÁI   [1]=ĐC2 SAU-TRÁI
 *          [2]=ĐC3 TRƯỚC-PHẢI   [3]=ĐC4 SAU-PHẢI
 *  L298N   : { IN1, IN2, EN }        BTS7960 : { RPWM, LPWM, -1 }
 * ------------------------------------------------------------------------------*/
#if MOTOR_DRIVER == DRIVER_L298N
static const int8_t MOTOR_PINS[4][3] = {
    {27, 26, 25}, /* ĐC1 TRƯỚC-TRÁI  */
    {33, 32, 14}, /* ĐC2 SAU-TRÁI    */
    {19, 18,  5}, /* ĐC3 TRƯỚC-PHẢI  */
    {17, 16,  4}, /* ĐC4 SAU-PHẢI    */
};
#else
static const int8_t MOTOR_PINS[4][3] = {
    {27, 26, -1}, /* ĐC1 TRƯỚC-TRÁI  */
    {25, 33, -1}, /* ĐC2 SAU-TRÁI    */
    {32, 14, -1}, /* ĐC3 TRƯỚC-PHẢI  */
    {19, 18, -1}, /* ĐC4 SAU-PHẢI    */
};
#endif

/* ---------------- 3. Đảo chiều từng động cơ ------------------------------------ */
#define INVERT_M1 0 /* ĐC1 TRƯỚC-TRÁI  */
#define INVERT_M2 0 /* ĐC2 SAU-TRÁI    */
#define INVERT_M3 0 /* ĐC3 TRƯỚC-PHẢI  */
#define INVERT_M4 0 /* ĐC4 SAU-PHẢI    */

/* ---------------- 4. Tốc độ ---------------------------------------------------- */
#define BASE_SPEED_PCT  60  /* Tốc độ mặc định (%)                              */
#define BOOST_SPEED_PCT 100 /* Giữ R2                                           */
#define SLOW_SPEED_PCT  30  /* Giữ L2                                           */
#define TRIGGER_ON      150 /* Ngưỡng nhận L2/R2 (0..1020)                      */

/* Cấp tốc độ đổi bằng L3 (nhấn cần gạt trái) — CHẬM / TRUNG BÌNH / NHANH */
static const uint8_t SPEED_LEVELS[3] = {35, 60, 100};
static const char*   SPEED_NAMES[3]  = {"CHAM", "TRUNG BINH", "NHANH"};

#define MIN_PWM   45  /* PWM nhỏ nhất để động cơ đủ lực quay (0..255)            */
#define PWM_FREQ  5000
#define PWM_RES   8

#define RAMP_STEP        12 /* tăng tốc dần; 0 = ăn ngay                        */
#define RAMP_INTERVAL_MS 20

#define IDLE_COAST 0 /* thả trôi */
#define IDLE_BRAKE 1 /* phanh    */
#define IDLE_MODE  IDLE_COAST

/* ---------------- 5. CHỨC NĂNG PHỤ (nút còn lại) ------------------------------
 *  Đặt -1 để tắt bớt chức năng nào bạn không dùng (chân đó sẽ được giải phóng).
 * ------------------------------------------------------------------------------*/
#define AUX_LIGHT_PIN   13 /* [ ] VUÔNG    : đèn pha, bật/tắt        */
#define AUX_HORN_PIN    21 /* O  TRÒN     : còi, NHẤN GIỮ thì kêu   */
#define AUX_RELAY1_PIN  22 /* /\ TAM GIÁC : cơ cấu 1, bật/tắt       */
#define AUX_RELAY2_PIN  23 /* PS          : cơ cấu 2, bật/tắt       */

/* Đa số module relay TQ kích ở mức THẤP (LOW = đóng).
 * Nếu bạn dùng module relay đổi thành 1; nếu nối LED/qua transistor thì để 0. */
#define AUX_ACTIVE_LOW 0

/* ---------------- 6. LED trạng thái (GPIO2 trên DevKit) ------------------------ */
#define STATUS_LED_PIN    2  /* -1 = không dùng                                  */
#define LED_BLINK_FAST_MS 150 /* ĐÃ kết nối tay cầm  -> nháy NHANH               */
#define LED_BLINK_SLOW_MS 700 /* CHƯA kết nối        -> nháy CHẬM               */

/* ---------------- 7. Tuỳ chọn & an toàn --------------------------------------- */
#define USE_ANALOG_STICK 0
#define STICK_DEADBAND   40
#define DATA_TIMEOUT_MS     500 /* quá lâu không có gói tin -> dừng động cơ     */
#define DISCONNECT_HOLD_MS 2000 /* giữ SHARE+OPTIONS để ngắt kết nối            */
#define MOTOR_TEST_MODE      0
#define MOTOR_TEST_PCT      40
#define MOTOR_TEST_MS     2000
#define PRINT_CHANGES        1  /* in ra Serial mỗi khi lệnh thay đổi           */

/* =================================================================================
 *                          K H A I   B Á O   C H U N G
 * ================================================================================= */

#define NUM_MOTORS 4
#define PWM_MAX    ((1 << PWM_RES) - 1) /* 255 */

/* Bit đánh dấu các nút chức năng */
#define BTN_CROSS    0x01 /* X  chéo   */
#define BTN_CIRCLE   0x02 /* O  tròn   */
#define BTN_SQUARE   0x04 /* [] vuông  */
#define BTN_TRIANGLE 0x08 /* /\ tam giac */
#define BTN_L3       0x10 /* ấn cần trái */
#define BTN_R3       0x20 /* ấn cần phải */
#define BTN_PS       0x40 /* nút PS     */

static ControllerPtr myControllers[BP32_MAX_GAMEPADS];

static const int8_t MOTOR_INVERT[NUM_MOTORS] = {INVERT_M1, INVERT_M2, INVERT_M3, INVERT_M4};
static const char*  MOTOR_NAME[NUM_MOTORS]   = {"M1 TRUOC-TRAI", "M2 SAU-TRAI", "M3 TRUOC-PHAI", "M4 SAU-PHAI"};

static int      gTargetPwm[NUM_MOTORS];
static int      gCurPwm[NUM_MOTORS];
static uint32_t gLastRampMs = 0;
static uint32_t gLastRxMs = 0;
static uint32_t gHoldStartMs = 0;
static bool     gConnected = false;

/* Trạng thái chức năng phụ */
static bool     gEStop = false;      /* dừng khẩn cấp            */
static bool     gReverse = false;    /* đảo đầu robot            */
static uint8_t  gSpeedIdx = 1;       /* cấp tốc độ (mặc định TB) */
static bool     gLight = false;
static bool     gRelay1 = false;
static bool     gRelay2 = false;
static bool     gHorn = false;       /* O đang được giữ          */
static uint16_t gPrevBtn = 0;        /* trạng thái nút lần trước (bắt cạnh lên) */

/* LED nháy */
static uint32_t gLedMs = 0;
static bool     gLedOn = false;

typedef struct {
  int8_t  vx;    /* -100..+100 : +100 = ngang PHẢI */
  int8_t  vy;    /* -100..+100 : +100 = TIẾN       */
  int8_t  w;     /* -100..+100 : +100 = XOAY PHẢI  */
  uint8_t speed; /* % tốc độ                       */
} cmd_t;
static cmd_t gCmd;

/* ============================ H À M   I N   L O G ============================== */

static void logFmt(const char* tag, const char* fmt, ...) {
  char buf[240];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%8lu ms][ROBOT]%s%s\n", (unsigned long)millis(), tag, buf);
}
#define logI(...) logFmt(" ", __VA_ARGS__)
#define logE(code, ...) logFmt("[LOI " code "] ", __VA_ARGS__)

/* ====================== Đ I Ề U   K H I Ể N   N G Õ   R A ====================== */

static void writeAux(int8_t pin, bool on) {
  if (pin < 0) return;
#if AUX_ACTIVE_LOW
  digitalWrite((uint8_t)pin, on ? LOW : HIGH);
#else
  digitalWrite((uint8_t)pin, on ? HIGH : LOW);
#endif
}

static void applyAuxOutputs(void) {
  writeAux(AUX_LIGHT_PIN, gLight);
  writeAux(AUX_RELAY1_PIN, gRelay1);
  writeAux(AUX_RELAY2_PIN, gRelay2);
  writeAux(AUX_HORN_PIN, gHorn);
}

/* Tắt hết chức năng phụ (dùng khi mất kết nối) */
static void allAuxOff(void) {
  gLight = gRelay1 = gRelay2 = gHorn = false;
  gPrevBtn = 0;
  applyAuxOutputs();
}

/* Nháy LED: NHANH khi đã kết nối, CHẬM khi chưa kết nối (không dùng delay) */
static void updateStatusLed(void) {
#if STATUS_LED_PIN >= 0
  uint32_t period = gConnected ? (uint32_t)LED_BLINK_FAST_MS : (uint32_t)LED_BLINK_SLOW_MS;
  if (millis() - gLedMs >= period) {
    gLedMs = millis();
    gLedOn = !gLedOn;
    digitalWrite(STATUS_LED_PIN, gLedOn ? HIGH : LOW);
  }
#endif
}

/* ====================== Đ I Ề U   K H I Ể N   Đ Ộ N G   C Ơ ==================== */

static void setMotor(int i, int speed) {
  if (MOTOR_INVERT[i]) speed = -speed;
  if (speed > PWM_MAX) speed = PWM_MAX;
  if (speed < -PWM_MAX) speed = -PWM_MAX;

  int mag = speed < 0 ? -speed : speed;
  if (mag > 0 && mag < MIN_PWM) mag = MIN_PWM;

#if MOTOR_DRIVER == DRIVER_L298N
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
    ledcWrite((uint8_t)i, PWM_MAX);
#else
    digitalWrite(p[0], LOW);
    digitalWrite(p[1], LOW);
    ledcWrite((uint8_t)i, 0);
#endif
  }
#else
  uint8_t chF = (uint8_t)(i * 2);
  uint8_t chB = (uint8_t)(i * 2 + 1);
  if (speed > 0) {
    ledcWrite(chF, (uint32_t)mag);
    ledcWrite(chB, 0);
  } else if (speed < 0) {
    ledcWrite(chF, 0);
    ledcWrite(chB, (uint32_t)mag);
  } else {
    uint32_t idle = (IDLE_MODE == IDLE_BRAKE) ? (uint32_t)PWM_MAX : 0u;
    ledcWrite(chF, idle);
    ledcWrite(chB, idle);
  }
#endif
}

static void stopAllMotors(void) {
  for (int i = 0; i < NUM_MOTORS; i++) {
    gTargetPwm[i] = 0;
    gCurPwm[i] = 0;
    setMotor(i, 0);
  }
}

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
 *  Theo cách lắp của bạn (1,2 cùng chiều / 3,4 cùng chiều, động cơ đối xứng):
 *      TIEN  -> [+1, +1, -1, -1]      XOAY -> cả 4 cùng dấu
 *  Công thức:  M1 =  vy+vx+w   M2 =  vy-vx+w   M3 = -vy+vx+w   M4 = -vy-vx+w
 *  Chuẩn hoá theo giá trị lớn nhất rồi mới nhân % tốc độ.
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

  float mx = 0.0f;
  for (int i = 0; i < 4; i++) {
    float a = m[i] < 0 ? -m[i] : m[i];
    if (a > mx) mx = a;
  }
  if (mx > 1.0f)
    for (int i = 0; i < 4; i++) m[i] /= mx;

  float scale = (float)c->speed / 100.0f * (float)PWM_MAX;
  for (int i = 0; i < 4; i++) gTargetPwm[i] = (int)(m[i] * scale + (m[i] >= 0 ? 0.5f : -0.5f));
}

/* ============================ Đ Ọ C   L Ệ N H   T Ừ   T A Y   C Ầ M =========== */

static void readCommand(ControllerPtr ctl, cmd_t* c) {
  c->vx = 0;
  c->vy = 0;
  c->w = 0;

  uint8_t dpad = ctl->dpad();
  if (dpad & DPAD_UP)    c->vy += 100;
  if (dpad & DPAD_DOWN)  c->vy -= 100;
  if (dpad & DPAD_RIGHT) c->vx += 100;
  if (dpad & DPAD_LEFT)  c->vx -= 100;

#if USE_ANALOG_STICK
  int ax = ctl->axisX();
  int ay = ctl->axisY();
  if (ax > STICK_DEADBAND || ax < -STICK_DEADBAND) c->vx = (int8_t)(ax * 100 / 512);
  if (ay > STICK_DEADBAND || ay < -STICK_DEADBAND) c->vy = (int8_t)(-ay * 100 / 512);
#endif

  /* Đảo đầu robot (R3): tiến<->lùi, trái<->phải; xoay vẫn giữ nguyên */
  if (gReverse) {
    c->vx = (int8_t)-c->vx;
    c->vy = (int8_t)-c->vy;
  }

  if (ctl->l1()) c->w -= 100; /* L1 = xoay TRÁI */
  if (ctl->r1()) c->w += 100; /* R1 = xoay PHẢI */

  /* Cấp tốc độ do L3 chọn; L2/R2 ép chậm/nhanh tạm thời */
  c->speed = SPEED_LEVELS[gSpeedIdx];
  if (ctl->brake() > TRIGGER_ON)    c->speed = SLOW_SPEED_PCT;  /* L2 */
  if (ctl->throttle() > TRIGGER_ON) c->speed = BOOST_SPEED_PCT; /* R2 */
}

/* Đọc 7 nút chức năng -> trả về bitmask */
static uint16_t readAuxButtons(ControllerPtr ctl) {
  uint16_t b = 0;
  if (ctl->a())       b |= BTN_CROSS;     /* X  chéo     */
  if (ctl->b())       b |= BTN_CIRCLE;    /* O  tròn     */
  if (ctl->x())       b |= BTN_SQUARE;    /* [] vuông    */
  if (ctl->y())       b |= BTN_TRIANGLE;  /* /\ tam giác */
  if (ctl->thumbL())  b |= BTN_L3;
  if (ctl->thumbR())  b |= BTN_R3;
  if (ctl->miscSystem()) b |= BTN_PS;     /* nút PS      */
  return b;
}

/* Xử lý các nút chức năng (chỉ xử lý ở cạnh LÊN = mới bấm) */
static void handleAuxButtons(uint16_t b) {
  uint16_t pressed = (uint16_t)(b & ~gPrevBtn); /* các nút vừa được bấm xuống */

  /* CÒI: theo MỨC (đang giữ O thì kêu) — không phải cạnh */
  gHorn = (b & BTN_CIRCLE) ? true : false;

  if (pressed & BTN_CROSS) { /* X chéo = DỪNG KHẨN CẤP */
    gEStop = !gEStop;
    if (gEStop) {
      stopAllMotors();
      logI(">>> DUNG KHAN CAP (E-STOP). Nhan X lan nua de chay tiep.");
    } else {
      logI(">>> BO DUNG KHAN CAP. Robot san sang chay lai.");
    }
  }
  if (pressed & BTN_SQUARE) { /* [] vuông = ĐÈN */
    gLight = !gLight;
    logI("Den pha: %s", gLight ? "BAT" : "TAT");
  }
  if (pressed & BTN_TRIANGLE) { /* /\ tam giác = CƠ CẤU 1 */
    gRelay1 = !gRelay1;
    logI("Co cau 1 (GPIO%d): %s", AUX_RELAY1_PIN, gRelay1 ? "BAT" : "TAT");
  }
  if (pressed & BTN_PS) { /* PS = CƠ CẤU 2 */
    gRelay2 = !gRelay2;
    logI("Co cau 2 (GPIO%d): %s", AUX_RELAY2_PIN, gRelay2 ? "BAT" : "TAT");
  }
  if (pressed & BTN_L3) { /* ấn cần trái = đổi cấp tốc độ */
    gSpeedIdx = (uint8_t)((gSpeedIdx + 1) % 3);
    logI("Cap toc do: %s (%d%%)", SPEED_NAMES[gSpeedIdx], SPEED_LEVELS[gSpeedIdx]);
  }
  if (pressed & BTN_R3) { /* ấn cần phải = đảo đầu robot */
    gReverse = !gReverse;
    logI("Dao dau robot: %s (D-Pad len = %s)", gReverse ? "BAT" : "TAT",
         gReverse ? "phia SAU robot" : "phia TRUOC robot");
  }

  gPrevBtn = b;
  applyAuxOutputs();
}

/* ============================ T Ê N   &   I N   L Ệ N H ======================== */

static void commandName(const cmd_t* c, char* buf, size_t size) {
  buf[0] = 0;
  if (c->vx == 0 && c->vy == 0 && c->w == 0) {
    snprintf(buf, size, "DUNG");
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
  else              snprintf(buf, size, "NGANG %s", h);

  if (c->w != 0) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s + XOAY %s", buf, c->w > 0 ? "PHAI" : "TRAI");
    snprintf(buf, size, "%s", tmp);
  }
}

static void printCommand(const cmd_t* c) {
  char name[64];
  commandName(c, name, sizeof(name));
  logI("%-22s | toc=%3u%% | vx=%+4d vy=%+4d w=%+4d | PWM: M1=%+4d M2=%+4d M3=%+4d M4=%+4d%s", name, c->speed,
       c->vx, c->vy, c->w, gTargetPwm[0], gTargetPwm[1], gTargetPwm[2], gTargetPwm[3],
       gEStop ? "   [E-STOP DANG BAT]" : "");
}

/* ================================== C A L L B A C K =========================== */

void onConnectedController(ControllerPtr ctl) {
  /* KHÔNG kiểm tra isGamepad() ở đây: lúc này Bluepad32 chưa nhận gói tin đầu tiên
   * nên class vẫn = 0. Chỉ loại bàn phím / chuột. */
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
      logI(" [OK] DA KET NOI TAY CAM  (vi tri #%d)   -> LED nhay NHANH", i);
      logI("   Ten        : %s (model=%d)", ctl->getModelName().c_str(), ctl->getModel());
      logI("   Dia chi BT : %02X:%02X:%02X:%02X:%02X:%02X", props.btaddr[0], props.btaddr[1], props.btaddr[2],
           props.btaddr[3], props.btaddr[4], props.btaddr[5]);
      logI("   Pin        : %u%%", (unsigned)((uint32_t)ctl->battery() * 100u / 255u));
      logI("-----------------------------------------------");
      logI(" DI CHUYEN : D-Pad len/xuong/trai/phai  | L1/R1 xoay | L2 cham | R2 nhanh");
      logI(" X=DUNG KHAN CAP | O(giu)=COI | VUONG=DEN | TAM GIAC=CO CAU 1 | PS=CO CAU 2");
      logI(" L3=doi toc do  | R3=dao dau robot | SHARE+OPTIONS 2s = ngat ket noi");
      logI("===============================================");
      gConnected = true;
      gLastRxMs = millis();
      gHoldStartMs = 0;
      gPrevBtn = 0;
      gEStop = false;
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
      logE("E12", "Mat ket noi tay cam #%d  ->  DUNG 4 DONG CO + TAT CHUC NANG PHU.", i);
      stopAllMotors();
      allAuxOff();
      gConnected = false;
      gHoldStartMs = 0;
      gEStop = false;
      memset(&gCmd, 0, sizeof(gCmd));
      logI("Bam nut PS tren tay cam de ket noi lai (LED se nhay NHANH).");
      return;
    }
  }
}

/* ============================== K I Ể M   T R A   D Â Y ======================= */

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
  Serial.println(F("########## XONG. Neu banh nao quay nguoc: doi INVERT_Mx = 1 ##########"));
  Serial.println();
}

/* ================================== S E T U P ================================= */

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("  ROBOT MECANUM - BAN DAY DU (moi nut 1 viec)  "));
  Serial.println(F("================================================"));

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
    logI("Dong co %s : IN1=GPIO%d  IN2=GPIO%d  EN=GPIO%d", MOTOR_NAME[i], p[0], p[1], p[2]);
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

  /* Chức năng phụ */
  const int8_t auxPins[4] = {AUX_LIGHT_PIN, AUX_HORN_PIN, AUX_RELAY1_PIN, AUX_RELAY2_PIN};
  const char*  auxNames[4] = {"Den pha (VUONG)", "Coi (O, giu)", "Co cau 1 (TAM GIAC)", "Co cau 2 (PS)"};
  for (int k = 0; k < 4; k++) {
    if (auxPins[k] < 0) {
      logI("%-20s : KHONG DUNG (dat -1)", auxNames[k]);
      continue;
    }
    pinMode((uint8_t)auxPins[k], OUTPUT);
    writeAux(auxPins[k], false);
    logI("%-20s : GPIO%d   (kich muc %s)", auxNames[k], auxPins[k], AUX_ACTIVE_LOW ? "THAP" : "CAO");
  }

#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  logI("LED trang thai       : GPIO%d  (nhay NHANH %d ms = da ket noi | nhay CHAM %d ms = chua ket noi)",
       STATUS_LED_PIN, LED_BLINK_FAST_MS, LED_BLINK_SLOW_MS);
#endif

  logI("Cap toc do (L3)      : %d%% / %d%% / %d%%  ->  dang dung %s", SPEED_LEVELS[0], SPEED_LEVELS[1],
       SPEED_LEVELS[2], SPEED_NAMES[gSpeedIdx]);

#if MOTOR_TEST_MODE
  runMotorTest();
#endif

  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.enableNewBluetoothConnections(true);
  BP32.enableVirtualDevice(false);

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
  logE("E21", "Chip nay KHONG co Bluetooth Classic => KHONG the dung tay cam PS4.");
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

      /* A) SHARE + OPTIONS giữ 2 s -> ngắt kết nối (robot dừng) */
      if (ctl->miscSelect() && ctl->miscStart()) {
        if (gHoldStartMs == 0) {
          gHoldStartMs = millis();
          logI("Dang giu SHARE + OPTIONS... giu them %d ms de ngat ket noi.", DISCONNECT_HOLD_MS);
        } else if (millis() - gHoldStartMs >= (uint32_t)DISCONNECT_HOLD_MS) {
          gHoldStartMs = 0;
          logI("Ngat ket noi theo yeu cau -> DUNG ROBOT. Bam PS de ket noi lai.");
          stopAllMotors();
          allAuxOff();
          ctl->disconnect();
          continue;
        }
      } else {
        gHoldStartMs = 0;
      }

      /* B) Các nút chức năng phụ */
      handleAuxButtons(readAuxButtons(ctl));

      /* C) Lệnh di chuyển */
      cmd_t c;
      readCommand(ctl, &c);
      if (gEStop) {
        c.vx = 0;
        c.vy = 0;
        c.w = 0; /* E-STOP: cắt động cơ, chức năng phụ vẫn chạy */
      }
      if (memcmp(&c, &gCmd, sizeof(c)) != 0) {
        gCmd = c;
        computeWheelPwm(&gCmd);
#if PRINT_CHANGES
        printCommand(&gCmd);
#endif
      }
      break; /* dùng tay cầm đầu tiên */
    }
  }

  /* D) An toàn: quá lâu không nhận được gói tin -> dừng */
  if (gConnected && (millis() - gLastRxMs > DATA_TIMEOUT_MS)) {
    logE("E14", "Khong nhan du lieu tu tay cam trong %lu ms -> DUNG 4 DONG CO.",
         (unsigned long)(millis() - gLastRxMs));
    memset(&gCmd, 0, sizeof(gCmd));
    stopAllMotors();
    gLastRxMs = millis();
  }

  /* E) Đưa PWM ra động cơ + nháy LED trạng thái */
  applyRamp();
  updateStatusLed();

  delay(5); /* nhường CPU cho Bluetooth */
}

/*====================================================================================
 *  PS4 (DualShock 4)  ->  ESP32   -   Firmware nhan tin hieu tay cam
 *  ----------------------------------------------------------------------------------
 *  Chuc nang:
 *    1. ESP32 tu bat Bluetooth, tu quet (inquiry) va TU DONG ket noi tay cam.
 *    2. Tu nho khoa ghep noi (luu trong NVS) => lan sau chi can bam nut PS la vao ngay.
 *    3. In ra Serial Monitor toan bo du lieu tay cam nhan duoc:
 *         - ma nhan (bitmask) cua cac nut: btn=0x....
 *         - D-Pad: dpad=0x..
 *         - tay cam trai/phai: L=(x,y)  R=(x,y)
 *         - L2/R2: L2=.. R2=..
 *         - gyro / accel, pin, trang thai sac
 *       kem 1 dong "de doc": ten cac nut dang nhan theo kieu PS4 (X, O, VUONG, TAM GIAC...).
 *    4. In MA LOI (E10, E11, ...) khi khong ket noi duoc / mat ket noi, kem goi y xu ly.
 *
 *  Phan cung:
 *    - ESP32 CO Bluetooth Classic: ESP32-WROOM-32 / ESP32-DevKitC / ESP32-WROVER / ESP32-PICO...
 *    - KHONG dung duoc voi ESP32-S3, ESP32-C3, ESP32-C6, ESP32-S2 (khong co Bluetooth Classic).
 *
 *  Thu vien: Bluepad32 (duoc cung cap san trong goi framework o platformio.ini).
 *====================================================================================*/

#include <Bluepad32.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ============================== C A U   H I N H ================================= */
/* Ban doi cac gia tri nay roi upload lai.                                        */

/* 1 = chi cho phep ket noi dung 1 tay cam co dia chi MAC ben duoi (loc theo MAC).
 * 0 = nhan bat ky tay cam nao (khuyen dung khi moi bat dau / khi chua tung ket noi). */
#define USE_MAC_FILTER 0

/* Dia chi MAC tim duoc cua tay cam PS4 cua ban.
 * Chi co tac dung khi USE_MAC_FILTER = 1. */
#define TARGET_MAC "A0:5A:5D:F9:C5:80"

/* Chu ky in du lieu lien tuc (ms). 0 = chi in khi du lieu thay doi. */
#define PRINT_INTERVAL_MS 100

/* Sau bao nhieu giay khong ket noi duoc thi in canh bao E10 (va nhac lai moi WARN_REPEAT_SEC giay). */
#define WARN_NO_CONTROLLER_SEC 15
#define WARN_REPEAT_SEC 30

/* Neu da ket noi ma khong nhan duoc du lieu trong bao lâu (ms) thi coi nhu treo -> ngat de ket noi lai. */
#define DATA_TIMEOUT_MS 3000

/* So lan mat ket noi lien tiep truoc khi goi y "xoa khoa Bluetooth" (ma E22). */
#define DISCONNECT_WARN_LIMIT 5

/* Chan LED demo tren board (ESP32 DevKit thuong la GPIO2). Dat -1 neu khong dung.
 * Khi co nut tren tay cam duoc nhan -> LED sang. */
#define DEMO_LED_PIN 2

/* ============================== B I E N   T O A N   C U C ======================= */

static ControllerPtr myControllers[BP32_MAX_GAMEPADS];

typedef struct {
  uint16_t buttons;     // ma nut lan truoc
  uint8_t dpad;         // D-Pad lan truoc
  uint8_t misc;         // nut phu (PS/SHARE/OPTIONS) lan truoc
  uint32_t lastRxMs;    // thoi diem nhan du lieu moi nhat (dung cho ma loi E14)
} slot_t;

static slot_t gSlot[BP32_MAX_GAMEPADS];

static uint8_t gTargetMac[6];
static bool gMacFilterOn = (USE_MAC_FILTER != 0);
static bool gTargetMacValid = false;

static uint32_t gBootMs = 0;
static uint32_t gLastWarnMs = 0;
static uint32_t gLastStateLogMs = 0;
static bool gEverConnected = false;
static int gConnectCount = 0;
static int gDisconnectStreak = 0;

/* ============================== H A M   H O   T R O ============================= */

/* Doi chuoi "AA:BB:CC:DD:EE:FF" thanh 6 byte. Tra ve false neu sai dinh dang. */
static bool parseMac(const char* text, uint8_t out[6]) {
  unsigned int v[6];
  if (!text) return false;
  if (sscanf(text, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] > 0xFF) return false;
    out[i] = (uint8_t)v[i];
  }
  return true;
}

static String macToStr(const uint8_t* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

/* In thong tin: [   12345 ms][PS4] ... */
static void logI(const char* fmt, ...) {
  char msg[220];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("[%8lu ms][PS4] %s\n", (unsigned long)millis(), msg);
}

/* In loi    : [   12345 ms][PS4][LOI E10] ... */
static void logE(const char* code, const char* fmt, ...) {
  char msg[220];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("[%8lu ms][PS4][LOI %s] %s\n", (unsigned long)millis(), code, msg);
}

static void appendStr(char* buf, size_t size, const char* text) {
  size_t len = strlen(buf);
  if (len + 1 >= size) return;
  strncat(buf, text, size - len - 1);
}

/* Ten nut theo kieu PS4 (khop voi cach Bluepad32 anh xa DualShock 4). */
static void appendButtonNames(char* buf, size_t size, uint16_t buttons, uint8_t misc) {
  const struct {
    uint16_t bit;
    const char* name;
  } kBtns[] = {
      {BUTTON_A, "X(Cheo)"},
      {BUTTON_B, "O(Tron)"},
      {BUTTON_X, "VUONG"},
      {BUTTON_Y, "TAMGIAC"},
      {BUTTON_SHOULDER_L, "L1"},
      {BUTTON_SHOULDER_R, "R1"},
      {BUTTON_TRIGGER_L, "L2"},
      {BUTTON_TRIGGER_R, "R2"},
      {BUTTON_THUMB_L, "L3"},
      {BUTTON_THUMB_R, "R3"},
  };
  const struct {
    uint8_t bit;
    const char* name;
  } kMisc[] = {
      {MISC_BUTTON_SYSTEM, "PS"},
      {MISC_BUTTON_SELECT, "SHARE"},
      {MISC_BUTTON_START, "OPTIONS"},
      {MISC_BUTTON_CAPTURE, "CAPTURE"},
  };

  bool any = false;
  for (unsigned i = 0; i < sizeof(kBtns) / sizeof(kBtns[0]); i++) {
    if (buttons & kBtns[i].bit) {
      if (any) appendStr(buf, size, "|");
      appendStr(buf, size, kBtns[i].name);
      any = true;
    }
  }
  for (unsigned i = 0; i < sizeof(kMisc) / sizeof(kMisc[0]); i++) {
    if (misc & kMisc[i].bit) {
      if (any) appendStr(buf, size, "|");
      appendStr(buf, size, kMisc[i].name);
      any = true;
    }
  }
  if (!any) appendStr(buf, size, "(khong nhan nut nao)");
}

static void appendDpadNames(char* buf, size_t size, uint8_t dpad) {
  if (dpad == 0 || dpad == 0xFF) {
    appendStr(buf, size, "giua");
    return;
  }
  bool any = false;
  if (dpad & DPAD_UP) { appendStr(buf, size, "LEN"); any = true; }
  if (dpad & DPAD_DOWN) { if (any) appendStr(buf, size, "+"); appendStr(buf, size, "XUONG"); any = true; }
  if (dpad & DPAD_LEFT) { if (any) appendStr(buf, size, "+"); appendStr(buf, size, "TRAI"); any = true; }
  if (dpad & DPAD_RIGHT) { if (any) appendStr(buf, size, "+"); appendStr(buf, size, "PHAI"); any = true; }
}

static const char* batteryToStr(uint8_t battery) {
  if (battery == 0) return "khong ro";
  if (battery <= 1) return "GAN HET";
  return NULL;
}

/* ============================== I N   T H O N G   T I N ========================= */

static void printBootBanner() {
  const uint8_t* localAddr = BP32.localBdAddress();

  Serial.println();
  Serial.println(F("================================================================================"));
  Serial.println(F("  TAY CAM PS4 (DualShock 4)  ->  ESP32"));
  Serial.println(F("  Firmware dung thu vien Bluepad32"));
  Serial.println(F("--------------------------------------------------------------------------------"));
  Serial.printf("  Bluepad32  : %s\n", BP32.firmwareVersion());
#if defined(CONFIG_IDF_TARGET_ESP32)
  Serial.println(F("  Chip       : ESP32 (co Bluetooth Classic - OK voi tay cam PS4)"));
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  Serial.println(F("  Chip       : ESP32-S3 (KHONG co Bluetooth Classic - khong dung duoc voi PS4!)"));
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  Serial.println(F("  Chip       : ESP32-C3 (KHONG co Bluetooth Classic - khong dung duoc voi PS4!)"));
#else
  Serial.println(F("  Chip       : khong xac dinh (hay chac chan la ESP32 co Bluetooth Classic)"));
#endif
  Serial.printf("  MAC BT ESP32: %s\n", macToStr(localAddr).c_str());
  Serial.printf("  Loc theo MAC: %s\n", gMacFilterOn ? "BAT (chi ket noi 1 tay cam)" : "TAT (nhan moi tay cam)");
  Serial.printf("  MAC muc tieu: %s%s\n", TARGET_MAC, gTargetMacValid ? "" : "  <-- SAI DINH DANG, xem ma loi E20");
  Serial.println(F("--------------------------------------------------------------------------------"));
  Serial.println(F("  CACH GHEP NOI LAN DAU:"));
  Serial.println(F("    1) Tat tay cam (neu dang bat)."));
  Serial.println(F("    2) Giu dong thoi 2 nut SHARE + PS khoang 5 giay."));
  Serial.println(F("    3) Den cua tay cam nhay trang lien tuc => ESP32 se tu tim thay va ket noi."));
  Serial.println(F("    Cac lan sau: chi can bam nut PS, ESP32 se tu ket noi lai (da luu khoa)."));
  Serial.println(F("================================================================================"));
  Serial.println();
}

/* In bang thong tin khi moi ket noi duoc tay cam. */
static void printConnectBanner(ControllerPtr ctl, int idx) {
  ControllerProperties props = ctl->getProperties();
  const char* bat = batteryToStr(ctl->battery());

  logI("================================================================================");
  logI("  [OK] DA KET NOI TAY CAM  (vi tri #%d)", idx);
  logI("--------------------------------------------------------------------------------");
  logI("  Ten tay cam : %s", ctl->getModelName().c_str());
  logI("  Ma loai     : %d  (34 = DualShock 4 / PS4)", ctl->getModel());
  logI("  VID : PID   : 0x%04X : 0x%04X", props.vendor_id, props.product_id);
  logI("  Dia chi BT  : %s", macToStr(props.btaddr).c_str());
  logI("  Pin         : %u/255 (%u%%) %s", ctl->battery(), (unsigned)((uint32_t)ctl->battery() * 100u / 255u),
       bat ? bat : "");
  logI("  Tinh nang   : rung=%s  den=%s  lightbar=%s", (props.flags & ARDUINO_PROPERTY_FLAG_RUMBLE) ? "co" : "khong",
       (props.flags & ARDUINO_PROPERTY_FLAG_PLAYER_LEDS) ? "co" : "khong",
       (props.flags & ARDUINO_PROPERTY_FLAG_PLAYER_LIGHTBAR) ? "co" : "khong");
  if (gMacFilterOn) {
    logI("  Loc MAC     : BAT - chi cho phep tay cam co MAC %s", TARGET_MAC);
  } else {
    logI("  Meo         : hay so dia chi BT o tren voi MAC ma ban da tra duoc");
  }
  logI("================================================================================");
}

/* In 1 dong du lieu day du (dinh ky). */
static void printStateLine(ControllerPtr ctl, int idx) {
  char names[160] = "";
  appendButtonNames(names, sizeof(names), ctl->buttons(), (uint8_t)ctl->miscButtons());

  Serial.printf(
      "[%8lu ms][PS4][DATA] #%d btn=0x%04X dpad=0x%02X misc=0x%02X L=(%4d,%4d) R=(%4d,%4d) L2=%4d R2=%4d "
      "gyro=(%6d,%6d,%6d) accel=(%6d,%6d,%6d) pin=%u%%\n",
      (unsigned long)millis(), idx, ctl->buttons(), ctl->dpad(), (uint8_t)ctl->miscButtons(), ctl->axisX(), ctl->axisY(),
      ctl->axisRX(), ctl->axisRY(), ctl->brake(), ctl->throttle(), ctl->gyroX(), ctl->gyroY(), ctl->gyroZ(),
      ctl->accelX(), ctl->accelY(), ctl->accelZ(), (unsigned)((uint32_t)ctl->battery() * 100u / 255u));

  Serial.printf("[%8lu ms][PS4][NUT ] #%d dang nhan: %s\n", (unsigned long)millis(), idx, names);
}

/* In khi trang thai nut / D-Pad thay doi (in ngay lap tuc, khong doi chu ky). */
static bool printChangeLines(int idx, uint16_t newButtons, uint8_t newDpad, uint8_t newMisc) {
  char names[160];
  char dpadNames[64];
  bool printed = false;

  if (newButtons != gSlot[idx].buttons || newMisc != gSlot[idx].misc) {
    uint16_t oldB = gSlot[idx].buttons;
    uint8_t oldM = gSlot[idx].misc;

    /* Cac nut vua duoc nhan */
    uint16_t pressed = (uint16_t)((newButtons & ~oldB) | ((uint16_t)(newMisc & ~oldM)));
    uint16_t released = (uint16_t)((oldB & ~newButtons) | ((uint16_t)(oldM & ~newMisc)));

    if (pressed) {
      names[0] = 0;
      appendButtonNames(names, sizeof(names), (uint16_t)(newButtons & ~oldB), (uint8_t)(newMisc & ~oldM));
      Serial.printf("[%8lu ms][PS4][NUT ] #%d VUA NHAN : %s\n", (unsigned long)millis(), idx, names);
      printed = true;
    }
    if (released) {
      names[0] = 0;
      appendButtonNames(names, sizeof(names), (uint16_t)(oldB & ~newButtons), (uint8_t)(oldM & ~newMisc));
      Serial.printf("[%8lu ms][PS4][NUT ] #%d VUA NHA  : %s\n", (unsigned long)millis(), idx, names);
      printed = true;
    }
  }

  if (newDpad != gSlot[idx].dpad) {
    dpadNames[0] = 0;
    appendDpadNames(dpadNames, sizeof(dpadNames), newDpad);
    Serial.printf("[%8lu ms][PS4][NUT ] #%d D-PAD     : %s (0x%02X)\n", (unsigned long)millis(), idx, dpadNames, newDpad);
    printed = true;
  }
  return printed;
}

/* Vi du dieu khien chan GPIO: co nut duoc nhan thi bat LED demo. */
static void applyDemoOutput(uint16_t buttons) {
#if DEMO_LED_PIN >= 0
  digitalWrite(DEMO_LED_PIN, buttons ? HIGH : LOW);
#endif
}

/* ============================== C A L L B A C K ================================= */

/* Duoc goi khi co 1 thiet bi vua ket noi (tay cam, ban phim, chuot...). */
void onConnectedController(ControllerPtr ctl) {
  ControllerProperties props = ctl->getProperties();

  if (!ctl->isGamepad()) {
    logE("E13", "Thiet bi vua ket noi khong phai tay cam (class=%d). Da ngat ket noi.", (int)ctl->getClass());
    ctl->disconnect();
    return;
  }

  /* ---- Loc theo dia chi MAC (neu bat) ---- */
  if (gMacFilterOn) {
    if (!gTargetMacValid) {
      logE("E20", "MAC muc tieu '%s' sai dinh dang => tam tat loc MAC, van ket noi binh thuong.", TARGET_MAC);
      gMacFilterOn = false;
    } else if (memcmp(props.btaddr, gTargetMac, 6) != 0) {
      logE("E11", "Tay cam %s khong khop MAC %s (loc MAC dang BAT) => da ngat. Doi USE_MAC_FILTER = 0 neu day la tay cam cua ban.",
           macToStr(props.btaddr).c_str(), TARGET_MAC);
      ctl->disconnect();
      return;
    }
  }

  int slot = -1;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    logE("E15", "Da du %d tay cam (BP32_MAX_GAMEPADS). Tay cam moi khong duoc dung.", BP32_MAX_GAMEPADS);
    return;
  }

  myControllers[slot] = ctl;
  gSlot[slot].buttons = 0; /* trang thai "sach" de phat hien dung su kien nhan/nha nut */
  gSlot[slot].dpad = 0;
  gSlot[slot].misc = 0;
  gSlot[slot].lastRxMs = millis();

  gEverConnected = true;
  gConnectCount++;
  gDisconnectStreak = 0;

  printConnectBanner(ctl, slot);

  /* Hieu ung phan hoi: den lightbar mau theo vi tri + rung nhe */
  static const uint8_t kColors[4][3] = {{0, 0, 255}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0}};
  if (props.flags & ARDUINO_PROPERTY_FLAG_PLAYER_LIGHTBAR) {
    ctl->setColorLED(kColors[slot & 3][0], kColors[slot & 3][1], kColors[slot & 3][2]);
  }
  if (props.flags & ARDUINO_PROPERTY_FLAG_PLAYER_LEDS) {
    ctl->setPlayerLEDs((uint8_t)(1 << (slot & 3)));
  }
  if (props.flags & ARDUINO_PROPERTY_FLAG_RUMBLE) {
    ctl->playDualRumble(0 /* delay */, 250 /* ms */, 0x80, 0x40); /* rung nhe 250ms */
  }
}

/* Duoc goi khi tay cam mat ket noi (tat tay cam, het pin, ra xa...). */
void onDisconnectedController(ControllerPtr ctl) {
  int slot = -1;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      slot = i;
      myControllers[i] = nullptr;
      break;
    }
  }
  if (slot < 0) return;

  gDisconnectStreak++;

  logE("E12", "Mat ket noi tay cam #%d. ESP32 dang cho ket noi lai (chi can bam nut PS).", slot);

  if (gDisconnectStreak >= DISCONNECT_WARN_LIMIT) {
    logE("E22", "Mat ket noi %d lan lien tiep. Thu: (1) sac day pin tay cam, (2) tat/mo lai tay cam, "
                "(3) bo comment dong BP32.forgetBluetoothKeys() trong setup() de xoa khoa cu roi ghep noi lai.",
         gDisconnectStreak);
  }
}

/* ============================== S E T U P / L O O P ============================= */

void setup() {
  Serial.begin(115200);
  delay(300); /* cho Serial Monitor on dinh */

  gBootMs = millis();
  gLastWarnMs = millis();
  gLastStateLogMs = millis();

#if DEMO_LED_PIN >= 0
  pinMode(DEMO_LED_PIN, OUTPUT);
  digitalWrite(DEMO_LED_PIN, LOW);
#endif

  /* Kiem tra MAC trong cau hinh truoc khi chay */
  gTargetMacValid = parseMac(TARGET_MAC, gTargetMac);

  /* Dang ky callback: se duoc goi moi khi tay cam ket noi / mat ket noi */
  BP32.setup(&onConnectedController, &onDisconnectedController);

  /* LUU Y QUAN TRONG:
   * - Khong goi BP32.forgetBluetoothKeys() o day, vi nhu vay ESP32 se quen tay cam
   *   => moi lan bat len deu phai ghep noi lai tu dau.
   * - Chi goi ham nay khi ban muon "quen het" tay cam da ghep truoc do (bo comment dong duoi). */
  // BP32.forgetBluetoothKeys();

  /* Cho phep ESP32 tim va nhan ket noi moi (che do mac dinh cua Bluepad32) */
  BP32.enableNewBluetoothConnections(true);

  /* Bat tay cam tuong thich chuot cam ung (DS4 touchpad) thi se tao them 1 "chuot ao".
   * De false cho gon, vi ban dang can doc nut bam. */
  BP32.enableVirtualDevice(false);

  printBootBanner();

  if (!gTargetMacValid) {
    logE("E20", "TARGET_MAC = \"%s\" sai dinh dang (dung dang: AA:BB:CC:DD:EE:FF).", TARGET_MAC);
  }

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
  logE("E21", "Chip nay KHONG co Bluetooth Classic => KHONG the ket noi tay cam PS4/DualShock 4. "
              "Hay dung board ESP32 (ESP32-WROOM-32 / ESP32-DevKitC v1...).");
#endif
  logI("San sang. Hay bat tay cam PS4 (giu SHARE + PS cho lan dau tien)...");
}

void loop() {
  /* 1) Lay du lieu moi tu Bluepad32. Tra ve true neu co du lieu moi. */
  bool updated = BP32.update();

  if (updated) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
      ControllerPtr ctl = myControllers[i];
      if (ctl == nullptr || !ctl->isConnected()) continue;
      if (!ctl->hasData()) continue;

      uint16_t buttons = ctl->buttons();
      uint8_t dpad = ctl->dpad();
      uint8_t misc = (uint8_t)ctl->miscButtons();

      gSlot[i].lastRxMs = millis();

      /* In ngay khi co nut / D-Pad thay doi, kem 1 dong du lieu day du.
       * (Du lieu lien tuc duoc in dinh ky o buoc 2 ben duoi, khong in moi goi tin
       *  de tranh lam nghen Serial Monitor.) */
      if (printChangeLines(i, buttons, dpad, misc)) {
        printStateLine(ctl, i);
        gLastStateLogMs = millis();
      }

      applyDemoOutput(buttons);

      gSlot[i].buttons = buttons;
      gSlot[i].dpad = dpad;
      gSlot[i].misc = misc;
    }
  }

  /* 2) In dinh ky cho de theo doi (khong can bam nut) */
#if PRINT_INTERVAL_MS > 0
  if (millis() - gLastStateLogMs >= PRINT_INTERVAL_MS) {
    gLastStateLogMs = millis();
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
      ControllerPtr ctl = myControllers[i];
      if (ctl == nullptr || !ctl->isConnected() || !ctl->hasData()) continue;
      printStateLine(ctl, i);
    }
  }
#endif

  /* 3) Kiem tra tay cam "treo" (ket noi nhung khong gui du lieu) */
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    ControllerPtr ctl = myControllers[i];
    if (ctl == nullptr || !ctl->isConnected()) continue;
    if (millis() - gSlot[i].lastRxMs > DATA_TIMEOUT_MS) {
      logE("E14", "Tay cam #%d khong gui du lieu trong %lu ms (bi treo). Ngat ket noi de thu lai...", i,
           (unsigned long)(millis() - gSlot[i].lastRxMs));
      ctl->disconnect(); /* callback onDisconnectedController() se duoc goi */
    }
  }

  /* 4) Canh bao khi chua ket noi duoc tay cam nao */
  bool anyConnected = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] != nullptr && myControllers[i]->isConnected()) {
      anyConnected = true;
      break;
    }
  }

  if (!anyConnected && (millis() - gBootMs > (uint32_t)WARN_NO_CONTROLLER_SEC * 1000u) &&
      (millis() - gLastWarnMs > (uint32_t)WARN_REPEAT_SEC * 1000u)) {
    gLastWarnMs = millis();
    if (!gEverConnected) {
      logE("E10", "Chua ket noi duoc tay cam nao (da cho %lu giay). Kiem tra: tay cam da bat? da o che do ghep noi "
                  "(giu SHARE + PS den khi den nhay trang)? con pin? khoang cach < 10 m?",
           (unsigned long)((millis() - gBootMs) / 1000u));
      logI("Neu tay cam dang bat binh thuong: tat tay cam, giu SHARE + PS ~5 giay de vao che do ghep noi.");
      logI("Neu board khong phai ESP32 co Bluetooth Classic (vd. ESP32-S3/C3) thi khong the ket noi PS4.");
    } else {
      logI("Chua co tay cam nao ket noi lai. Bam nut PS tren tay cam de ESP32 tu ket noi lai.");
    }
  }

  /* 5) Nhuong CPU cho cac tac vu khac (Bluetooth, WiFi...). Khong duoc bo dong nay. */
  delay(5);
}

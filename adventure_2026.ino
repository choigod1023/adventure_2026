/*
 * adventure_2026.ino  -  메인 스케치
 * 스몸비 안전 경고 장치 (Arduino UNO R4 WiFi)
 *
 * 4개 모드 (BTN_BUS/SUBWAY/SPAT/SENSOR 버튼으로 직접 선택):
 *   ① MODE_BUS    - 버스 도착 API (data.go.kr)
 *   ② MODE_SUBWAY - 지하철 도착 API (data.seoul.go.kr)
 *   ③ MODE_SPAT   - C-ITS 신호 잔여시간 (t-data.seoul.go.kr, HTTPS)
 *   ④ MODE_SENSOR - PIR + RCWL 오프라인 감지
 *
 * 파일 분할 (Arduino IDE 자동 병합):
 *   adventure_2026.ino  ← 본 파일: 전역 상태, setup/loop, 모드 매니저, 톤 출력
 *   apis.ino            ← WiFi + 3개 API
 *   sensors.ino         ← PIR + RCWL 게이팅
 *   ui.ino              ← OLED 렌더링
 *
 * 라이브러리:
 *   - WiFiS3              (보드패키지)
 *   - ArduinoHttpClient   (라이브러리 매니저)
 *   - ArduinoJson v7      (Benoit Blanchon)
 *   - U8g2lib             (oliver)
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "secrets.h"
#include "config.h"

// ════════════════════════════════════════════════════════════════
//  전역 상태 (모든 .ino 파일에서 공유)
// ════════════════════════════════════════════════════════════════

// ─── 모드 ───
enum Mode {
  MODE_BUS = 0,
  MODE_SUBWAY,
  MODE_SPAT,
  MODE_SENSOR,
  MODE_COUNT
};
Mode currentMode = MODE_BUS;
bool modeJustChanged = true;  // 새 모드 진입 시 즉시 fetch 트리거

// ─── 네트워크 상태 ───
enum NetState { NET_BOOT, NET_WIFI, NET_OK, NET_OFFLINE };
NetState netState = NET_BOOT;

// ─── 표시 데이터 (현재 모드 결과 1건) ───
struct DisplayData {
  bool valid;
  bool danger;
  char line1[40];          // 컨텍스트 (정류장명/역명/교차로/모드명)
  char line2[48];          // 상세 (노선·도착메시지·잔여시간 등)
  unsigned long updatedAt; // 마지막 갱신 시점 (millis)
};
DisplayData displayData = { false, false, "", "", 0 };

// ─── 센서 상태 ───
struct SensorState {
  bool pir_now;
  bool radar_now;
  unsigned long pir_last_high_ms;
  unsigned long radar_last_high_ms;
  int radar_trigger_count;
};
SensorState sensors = { false, false, 0, 0, 0 };

// ─── 위험 상태 (모드 공통) ───
bool dangerActive = false;
unsigned long dangerLastDetectedAt = 0;

// ─── 폴링 타이머 ───
unsigned long lastFetchBus    = 0;
unsigned long lastFetchSubway = 0;
unsigned long lastFetchSpat   = 0;
unsigned long lastDraw        = 0;
const unsigned long DRAW_INTERVAL_MS = 250UL;

// ─── 톤 출력 ───
unsigned long lastToneToggle = 0;
bool tonePhase = false;  // false → 780Hz, true → 2000Hz
const unsigned long TONE_TOGGLE_MIN_MS = 150UL;
const unsigned long TONE_TOGGLE_MAX_MS = 350UL;

// ─── 버튼 디바운스 (4개) ───
struct BtnState {
  bool prev;                       // 직전 reading (PULLUP: HIGH=떨어짐, LOW=눌림)
  unsigned long lastChangeMs;
};
BtnState btnBus    = { HIGH, 0 };
BtnState btnSubway = { HIGH, 0 };
BtnState btnSpat   = { HIGH, 0 };
BtnState btnSensor = { HIGH, 0 };

// ─── OLED ───
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
const int OLED_WIDTH = 128;
const int OLED_MAX_TEXT_WIDTH = 124;

// ─── HTTP 클라이언트 (apis.ino에서 사용) ───
WiFiClient    plainSock;
WiFiSSLClient sslSock;

// ════════════════════════════════════════════════════════════════
//  setup / loop
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);
  Serial.println(F("\n=== adventure_2026 ==="));
  Serial.println(F("스몸비 안전 경고 장치 (4 모드)"));

  // GPIO
  pinMode(PIN_PIR,   INPUT);
#if HAS_RCWL
  pinMode(PIN_RADAR, INPUT);
#endif
  pinMode(PIN_BTN_BUS,    INPUT_PULLUP);
  pinMode(PIN_BTN_SUBWAY, INPUT_PULLUP);
  pinMode(PIN_BTN_SPAT,   INPUT_PULLUP);
  pinMode(PIN_BTN_SENSOR, INPUT_PULLUP);
#if HAS_SPEAKER
  pinMode(PIN_SPK1_780HZ, OUTPUT);
  pinMode(PIN_SPK2_2KHZ,  OUTPUT);
  noTone(PIN_SPK1_780HZ);
  noTone(PIN_SPK2_2KHZ);
#endif

  // OLED
  Wire.begin();
  u8g2.begin();
  drawScreen();   // "부팅 중" 표시

  // WiFi (모드 ①~③에 필요)
  connectWiFi();

  Serial.print(F("[부팅 완료] 시작 모드: "));
  Serial.println(modeShortName(currentMode));
}

void loop() {
  // 1) 입력
  pollButtons();
  pollSensors();   // sensors.ino

  // 2) WiFi 자동 폴백 (API 모드인데 끊겼으면 ④ 강제 전환)
  manageWiFi();    // apis.ino

  // 3) 현재 모드의 데이터 갱신 + 위험 평가
  bool danger = evaluateCurrentMode();

  // 4) 위험 상태머신
  updateDangerState(danger);

  // 5) 출력
  driveTone();

  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawScreen();    // ui.ino
  }
}

// ════════════════════════════════════════════════════════════════
//  버튼 → 모드 직접 선택
// ════════════════════════════════════════════════════════════════
bool buttonFell(int pin, BtnState &st) {
  bool now = digitalRead(pin);
  bool fell = false;
  if (now != st.prev) {
    if (millis() - st.lastChangeMs > DEBOUNCE_MS) {
      st.lastChangeMs = millis();
      st.prev = now;
      if (now == LOW) fell = true;   // 눌림
    }
  }
  return fell;
}

void setMode(Mode m) {
  if (currentMode == m) return;
  currentMode = m;
  modeJustChanged = true;
  displayData.valid = false;
  displayData.danger = false;
  Serial.print(F("[모드 전환] → "));
  Serial.println(modeShortName(m));
}

void pollButtons() {
  if (buttonFell(PIN_BTN_BUS,    btnBus))    setMode(MODE_BUS);
  if (buttonFell(PIN_BTN_SUBWAY, btnSubway)) setMode(MODE_SUBWAY);
  if (buttonFell(PIN_BTN_SPAT,   btnSpat))   setMode(MODE_SPAT);
  if (buttonFell(PIN_BTN_SENSOR, btnSensor)) setMode(MODE_SENSOR);
}

const char* modeShortName(Mode m) {
  switch (m) {
    case MODE_BUS:    return "BUS";
    case MODE_SUBWAY: return "SUBWAY";
    case MODE_SPAT:   return "CITS";
    case MODE_SENSOR: return "SENSOR";
    default:          return "?";
  }
}

// ════════════════════════════════════════════════════════════════
//  현재 모드 평가 → danger 반환
// ════════════════════════════════════════════════════════════════
bool evaluateCurrentMode() {
  switch (currentMode) {
    case MODE_BUS:    return evaluateBus();      // apis.ino
    case MODE_SUBWAY: return evaluateSubway();   // apis.ino
    case MODE_SPAT:   return evaluateSpat();     // apis.ino
    case MODE_SENSOR: return evaluateSensor();   // sensors.ino
    default:          return false;
  }
}

// ════════════════════════════════════════════════════════════════
//  위험 상태머신 (히스테리시스: 트리거 시 WARN_DURATION_MS 유지)
// ════════════════════════════════════════════════════════════════
void updateDangerState(bool detected) {
  if (detected) {
    dangerLastDetectedAt = millis();
    if (!dangerActive) {
      dangerActive = true;
      Serial.println(F("[WARN] 위험 트리거 ON"));
    }
  } else if (dangerActive &&
             (millis() - dangerLastDetectedAt) > WARN_DURATION_MS) {
    dangerActive = false;
    Serial.println(F("[WARN] 위험 해제"));
  }
}

// ════════════════════════════════════════════════════════════════
//  톤 출력
//  - 위험 아닐 때: 무음
//  - 위험: 780Hz / 2000Hz 를 불규칙 주기로 교차 (ANC 습관화 방지)
// ════════════════════════════════════════════════════════════════
void driveTone() {
#if !HAS_SPEAKER
  (void)lastToneToggle; (void)tonePhase;   // 미사용 경고 회피
  return;                                  // 스피커 없음: no-op
#else
  if (!dangerActive) {
    noTone(PIN_SPK1_780HZ);
    noTone(PIN_SPK2_2KHZ);
    return;
  }

  unsigned long now = millis();
  if (now - lastToneToggle <
      random(TONE_TOGGLE_MIN_MS, TONE_TOGGLE_MAX_MS)) {
    return;
  }
  lastToneToggle = now;
  tonePhase = !tonePhase;

  if (tonePhase) {
    noTone(PIN_SPK1_780HZ);
    tone(PIN_SPK2_2KHZ, TONE_FREQ_SECONDARY);
  } else {
    noTone(PIN_SPK2_2KHZ);
    tone(PIN_SPK1_780HZ, TONE_FREQ_PRIMARY);
  }
#endif
}

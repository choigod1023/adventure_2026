/*
 * adventure_2026.ino  -  메인 스케치
 * 스몸비 안전 경고 장치 (Arduino UNO R4 WiFi)
 *
 * 모드 구성 (실측 PCB 기준):
 *   스위치 D8 (PIN_MODE_SW):
 *     HIGH = API 모드 (위) — ①버스 / ②지하철 / ③C-ITS 자동 순환
 *     LOW  = 센서 모드 (아래) — ④ RCWL + PIR 오프라인 감지
 *
 *   API 모드 안에서 API_ROTATE_INTERVAL_MS 마다 다음 API 로 자동 전환.
 *   현재 활성 모드의 위험 트리거만 경고 발생.
 *
 * 오디오 (DuoBell — 한 스피커에 두 톤 동시 출력):
 *   A0 (DAC) = 780Hz 사인파 (저음, ANC 취약점)
 *   D6 (PWM) = 2kHz   사각파 (고음, 습관화 방지)
 *   하드웨어 저항 합산으로 모노 스피커 1개에 믹싱.
 *
 * 파일 분할 (Arduino IDE 자동 병합):
 *   adventure_2026.ino  ← 본 파일: 전역 상태, setup/loop, 모드 매니저, 오디오
 *   apis.ino            ← WiFi + 3개 API
 *   sensors.ino         ← PIR + RCWL 게이팅
 *   ui.ino              ← OLED 렌더링
 *
 * 라이브러리:
 *   - WiFiS3              (보드패키지)
 *   - ArduinoHttpClient   (라이브러리 매니저)
 *   - ArduinoJson v7      (Benoit Blanchon)
 *   - U8g2lib             (oliver)
 *   - analogWave          (UNO R4 보드패키지 기본 포함)
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "analogWave.h"

#include "secrets.h"
#include "config.h"

// ════════════════════════════════════════════════════════════════
//  전역 상태
// ════════════════════════════════════════════════════════════════

// ─── 모드 ───
//   ApiMode: API 모드 안에서의 자동 순환 인덱스
//   SENSOR  모드는 별도 플래그 (스위치 LOW 일 때)
enum ApiMode {
  API_BUS = 0,
  API_SUBWAY,
  API_SPAT,
  API_COUNT
};
ApiMode currentApiMode = API_BUS;
bool sensorMode = false;            // true = 스위치 LOW (센서 모드)
bool modeJustChanged = true;        // 새 모드 진입 시 즉시 fetch
unsigned long lastApiRotateMs = 0;

// ─── 네트워크 상태 ───
enum NetState { NET_BOOT, NET_WIFI, NET_OK, NET_OFFLINE };
NetState netState = NET_BOOT;

// ─── 표시 데이터 ───
struct DisplayData {
  bool valid;
  bool danger;
  char line1[40];          // 컨텍스트 (정류장명/역명/교차로/모드명)
  char line2[48];          // 상세
  unsigned long updatedAt;
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

// ─── 위험 상태머신 ───
bool dangerActive = false;
unsigned long dangerLastDetectedAt = 0;

// ─── 폴링 타이머 ───
unsigned long lastFetchBus    = 0;
unsigned long lastFetchSubway = 0;
unsigned long lastFetchSpat   = 0;
unsigned long lastDraw        = 0;
const unsigned long DRAW_INTERVAL_MS = 250UL;

// ─── 스위치 디바운스 ───
bool prevSwitchReading = HIGH;
unsigned long switchLastChangeMs = 0;

// ─── OLED ───
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
const int OLED_WIDTH = 128;
const int OLED_MAX_TEXT_WIDTH = 124;

// ─── HTTP ───
WiFiClient    plainSock;
WiFiSSLClient sslSock;

// ─── 오디오 (DuoBell) ───
analogWave wave(DAC);   // A0 = DAC, 저음 사인
bool warnAudioOn = false;

// ════════════════════════════════════════════════════════════════
//  setup / loop
// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);
  Serial.println(F("\n=== adventure_2026 ==="));
  Serial.println(F("스몸비 안전 경고 장치 (자동 순환 API + 센서)"));

  // GPIO
  pinMode(PIN_PIR,       INPUT);
#if HAS_RCWL
  pinMode(PIN_RADAR,     INPUT);
#endif
  pinMode(PIN_MODE_SW,   INPUT_PULLUP);
  pinMode(PIN_SPK_PWM,   OUTPUT);

  // 오디오: 780Hz 사인 준비 (진폭 0 → 무음 상태)
  wave.sine(TONE_FREQ_PRIMARY);
  wave.amplitude(0.0);
  noTone(PIN_SPK_PWM);

  // OLED
  Wire.begin();
  u8g2.begin();
  drawScreen();

  // 스위치 초기 상태 읽어 모드 결정 (디바운스 무시, 부팅 직후 1회)
  sensorMode = (digitalRead(PIN_MODE_SW) == LOW);
  prevSwitchReading = digitalRead(PIN_MODE_SW);
  Serial.print(F("[부팅 모드] "));
  Serial.println(sensorMode ? F("SENSOR (스위치 LOW)") : F("API (스위치 HIGH)"));

  // WiFi (API 모드일 때만 의미 있음 — 센서 모드여도 미리 연결)
  connectWiFi();

  lastApiRotateMs = millis();
  Serial.println(F("[부팅 완료]"));
}

void loop() {
  // 1) 입력
  pollSwitch();
  pollSensors();   // sensors.ino

  // 2) WiFi 자동 폴백 (API 모드인데 끊겼으면 센서 모드로)
  manageWiFi();    // apis.ino

  // 3) API 모드 자동 순환
  if (!sensorMode && (millis() - lastApiRotateMs >= API_ROTATE_INTERVAL_MS)) {
    lastApiRotateMs = millis();
    rotateApiMode();
  }

  // 4) 현재 모드의 위험 평가
  bool danger;
  if (sensorMode) {
    danger = evaluateSensor();        // sensors.ino
  } else {
    switch (currentApiMode) {
      case API_BUS:    danger = evaluateBus();    break;  // apis.ino
      case API_SUBWAY: danger = evaluateSubway(); break;  // apis.ino
      case API_SPAT:   danger = evaluateSpat();   break;  // apis.ino
      default:         danger = false;            break;
    }
  }

  // 5) 위험 상태머신
  updateDangerState(danger);

  // 6) 출력
  driveAudio();
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawScreen();    // ui.ino
  }
}

// ════════════════════════════════════════════════════════════════
//  스위치 → 모드 카테고리 전환
// ════════════════════════════════════════════════════════════════
void pollSwitch() {
  bool now = digitalRead(PIN_MODE_SW);
  if (now == prevSwitchReading) return;
  if (millis() - switchLastChangeMs < DEBOUNCE_MS) return;
  switchLastChangeMs = millis();
  prevSwitchReading = now;

  bool newSensorMode = (now == LOW);
  if (newSensorMode == sensorMode) return;

  sensorMode = newSensorMode;
  modeJustChanged = true;
  displayData.valid = false;
  displayData.danger = false;
  lastApiRotateMs = millis();

  Serial.print(F("[모드 전환] → "));
  Serial.println(sensorMode ? F("SENSOR") : F("API"));
}

void rotateApiMode() {
  currentApiMode = (ApiMode)((currentApiMode + 1) % API_COUNT);
  modeJustChanged = true;
  displayData.valid = false;
  displayData.danger = false;
  Serial.print(F("[API 자동 순환] → "));
  Serial.println(apiModeName(currentApiMode));
}

const char* apiModeName(ApiMode m) {
  switch (m) {
    case API_BUS:    return "BUS";
    case API_SUBWAY: return "SUBWAY";
    case API_SPAT:   return "CITS";
    default:         return "?";
  }
}

const char* currentModeLabel() {
  return sensorMode ? "SENSOR" : apiModeName(currentApiMode);
}

// ════════════════════════════════════════════════════════════════
//  위험 상태머신 (히스테리시스: WARN_DURATION_MS 유지)
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
//  오디오 (DuoBell — 780Hz DAC + 2kHz PWM 동시 출력)
// ════════════════════════════════════════════════════════════════
void warnOn() {
  if (warnAudioOn) return;
  wave.amplitude(0.8);                       // 저음 ON
  tone(PIN_SPK_PWM, TONE_FREQ_SECONDARY);    // 고음 ON
  warnAudioOn = true;
}

void warnOff() {
  if (!warnAudioOn) return;
  wave.amplitude(0.0);                       // 저음 OFF (freq 유지, 진폭만 0)
  noTone(PIN_SPK_PWM);                       // 고음 OFF
  warnAudioOn = false;
}

void driveAudio() {
  if (dangerActive) warnOn();
  else              warnOff();
}

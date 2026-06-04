/*
 * adventure_2026.ino  -  메인 스케치
 * 스몸비 안전 경고 장치 (Arduino UNO R4 WiFi)
 *
 * 모드 구성 (실측 PCB 기준) — 전부 모멘터리 푸시 버튼:
 *   D9  (PIN_BTN_SUBWAY) : A → ①지하철 API 모드
 *   D10 (PIN_BTN_BUS)    : B → ②버스 API 모드
 *   D11 (PIN_BTN_SPAT)   : C → ③C-ITS API 모드
 *   D12 (PIN_BTN_MODE)   : D → API 카테고리 ↔ SENSOR 모드 토글
 *   네 버튼 모두 OLED 모듈 내장. A/B/C 는 SENSOR 상태였어도 누르면 해당 API 모드로 복귀.
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
//   ApiMode: API 카테고리 안에서 선택된 모드 (D9~D11 버튼으로 직접 선택)
//   SENSOR  모드는 별도 플래그 (D12 버튼 토글)
enum ApiMode {
  API_BUS = 0,
  API_SUBWAY,
  API_SPAT,
  API_COUNT
};
ApiMode currentApiMode = API_BUS;   // 마지막으로 선택된 API 모드
bool sensorMode = false;            // true = SENSOR 모드 (D12 토글)
bool modeJustChanged = true;        // 새 모드 진입 시 즉시 fetch

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

// ─── SPAT(CITS) 신호 페이즈 + 로컬 카운트다운 스냅샷 ───
//  매 폴링 응답 시 secAtSnapshot/snapshotMs 갱신, 그 사이는 millis() 로 보간.
//  → 화면 카운트다운이 5초 폴링 사이에도 매끄럽게 줄어듦.
enum SpatPhase {
  SPAT_PHASE_NONE = 0,   // 미운영/데이터 없음
  SPAT_PHASE_PED_GREEN,  // 보행 GREEN (잔여시간 줄어드는 중, 0 이 되기 직전이 위험)
  SPAT_PHASE_PED_RED     // 보행 RED  (차량 GREEN 활성 → 건너면 위험)
};
struct SpatState {
  SpatPhase phase;
  float secAtSnapshot;
  unsigned long snapshotMs;
};
SpatState spatState = { SPAT_PHASE_NONE, 0.0f, 0 };

// SPAT 현재 잔여시간 (스냅샷 기준 로컬 보간).
float spatLiveSec() {
  if (spatState.phase == SPAT_PHASE_NONE) return 0.0f;
  float elapsed = (millis() - spatState.snapshotMs) / 1000.0f;
  float s = spatState.secAtSnapshot - elapsed;
  return s < 0 ? 0 : s;
}

// 카운트다운 0 도달 시 phase 로컬 토글. 매 loop 호출 안전 (이미 토글된 상태는 재토글 안 함).
//   가드: secAtSnapshot > 0 인 경우만 토글 → 토글 후 secAtSnapshot=0 으로 두면 다음 폴링 전엔 재진입 안 됨.
void spatTickLocal() {
  if (spatState.phase == SPAT_PHASE_NONE) return;
  if (spatState.secAtSnapshot <= 0.0f) return;  // 이미 토글됨 → 다음 폴링 동기화 대기
  float elapsed = (millis() - spatState.snapshotMs) / 1000.0f;
  if (elapsed < spatState.secAtSnapshot) return;

  spatState.phase = (spatState.phase == SPAT_PHASE_PED_GREEN)
                    ? SPAT_PHASE_PED_RED
                    : SPAT_PHASE_PED_GREEN;
  spatState.secAtSnapshot = 0.0f;   // 다음 폴링 올 때까지 0.0s 로 표시
  spatState.snapshotMs = millis();
  Serial.print(F("  [로컬 토글] → "));
  Serial.println(spatState.phase == SPAT_PHASE_PED_GREEN ? F("보행 GREEN") : F("보행 RED"));
}

// ─── 센서 상태 ───
struct SensorState {
  bool pir_now;
  bool radar_now;
  unsigned long radar_last_high_ms;
  int radar_trigger_count;
};
SensorState sensors = { false, false, 0, 0 };

// ─── 위험 상태머신 ───
bool dangerActive = false;
unsigned long dangerLastDetectedAt = 0;  // 마지막 감지 시각 (조용해지면 해제용)
unsigned long dangerStartedAt = 0;       // 위험 ON 된 시각 (최대 지속시간 캡용)

// ─── 폴링 타이머 ───
unsigned long lastFetchBus    = 0;
unsigned long lastFetchSubway = 0;
unsigned long lastFetchSpat   = 0;
unsigned long lastDraw        = 0;
const unsigned long DRAW_INTERVAL_MS = 250UL;

// ─── 버튼 디바운스 (struct Btn 정의는 config.h — IDE 자동 prototype 회피용) ───
Btn btnSubway = { PIN_BTN_SUBWAY, HIGH, 0 };  // D9  (A) : → SUBWAY
Btn btnBus    = { PIN_BTN_BUS,    HIGH, 0 };  // D10 (B) : → BUS
Btn btnSpat   = { PIN_BTN_SPAT,   HIGH, 0 };  // D11 (C) : → CITS
Btn btnMode   = { PIN_BTN_MODE,   HIGH, 0 };  // D12 (D) : API ↔ SENSOR 토글

// ─── OLED ─── (소프트웨어 I2C: SCL/SDA 비트뱅잉, 핀은 보드 기본 A5/A4)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);
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
  Serial.println(F("스몸비 안전 경고 장치 (버튼 모드 선택)"));

  // GPIO
  pinMode(PIN_PIR,       INPUT);
#if HAS_RCWL
  pinMode(PIN_RADAR,     INPUT);
#endif
  pinMode(PIN_BTN_MODE,   INPUT_PULLUP);
  pinMode(PIN_BTN_BUS,    INPUT_PULLUP);
  pinMode(PIN_BTN_SUBWAY, INPUT_PULLUP);
  pinMode(PIN_BTN_SPAT,   INPUT_PULLUP);
  pinMode(PIN_SPK_PWM,   OUTPUT);

  // 오디오: 780Hz 사인 준비 (진폭 0 → 무음 상태)
  wave.sine(TONE_FREQ_PRIMARY);
  wave.amplitude(0.0);
  noTone(PIN_SPK_PWM);

  // OLED
  Wire.begin();
  u8g2.begin();
  drawScreen();

  // 부팅 시작 모드: API / BUS
  sensorMode = false;
  currentApiMode = API_BUS;
  Serial.print(F("[부팅 모드] API / "));
  Serial.println(apiModeName(currentApiMode));

  // WiFi (API 모드일 때 필요 — 센서 모드여도 미리 연결)
  connectWiFi();

  Serial.println(F("[부팅 완료] D12=API/SENSOR, D9~D11=BUS/SUBWAY/CITS"));
}

void loop() {
  // 1) 입력
  pollButtons();
  pollSensors();   // sensors.ino

  // 2) WiFi 자동 폴백 (API 모드인데 끊겼으면 센서 모드로)
  manageWiFi();    // apis.ino

  // 3) 현재 모드의 위험 평가
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

  // 4) 위험 상태머신
  updateDangerState(danger);

  // 5) 출력
  driveAudio();
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawScreen();    // ui.ino
  }
}

// ════════════════════════════════════════════════════════════════
//  버튼 입력 → 모드 선택
//   D12        : API 카테고리 ↔ SENSOR 토글 (OLED 내장 버튼 D)
//   D9/D10/D11 : BUS/SUBWAY/CITS 직접 선택 (자동으로 API 카테고리 진입)
// ════════════════════════════════════════════════════════════════

// 버튼 눌림(HIGH→LOW falling edge) 검출 + 디바운스. 눌리면 true 1회.
bool btnPressed(Btn &b) {
  bool now = digitalRead(b.pin);
  if (now == b.prev) return false;
  if (millis() - b.lastMs < DEBOUNCE_MS) return false;
  b.lastMs = millis();
  b.prev = now;
  return (now == LOW);   // 눌림 에지에서만 true
}

// 모드 진입 공통 처리 (fetch 재트리거 + 화면 초기화)
// drawScreen() 을 즉시 호출해 "로딩 중" 화면을 그린 뒤 fetch 가 시작되도록 한다.
// (그러지 않으면 fetch 블로킹 동안 루프가 멈춰 로딩 화면이 한 번도 안 그려짐 — 특히 CITS HTTPS 2~3초)
void enterMode() {
  modeJustChanged = true;
  displayData.valid = false;
  displayData.danger = false;
  spatState.phase = SPAT_PHASE_NONE;   // 옛 SPAT 스냅샷이 다른 모드 화면에 새지 않게
  drawScreen();
}

void selectApi(ApiMode m) {
  sensorMode = false;
  currentApiMode = m;
  enterMode();
  Serial.print(F("[버튼] API → "));
  Serial.println(apiModeName(m));
}

void pollButtons() {
  // D12 (OLED 버튼 D): API ↔ SENSOR 토글
  if (btnPressed(btnMode)) {
    sensorMode = !sensorMode;
    enterMode();
    Serial.print(F("[버튼] 토글 → "));
    Serial.println(sensorMode ? F("SENSOR") : F("API"));
  }

  // D9~D11: API 모드 직접 선택 (SENSOR 였어도 API로 복귀)
  if (btnPressed(btnBus))    selectApi(API_BUS);
  if (btnPressed(btnSubway)) selectApi(API_SUBWAY);
  if (btnPressed(btnSpat))   selectApi(API_SPAT);
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
//  위험 상태머신
//   - 해제①: 마지막 감지 후 WARN_DURATION_MS 동안 조용 (히스테리시스)
//   - 해제②: 위험 시작 후 WARN_MAX_DURATION_MS 경과 → 계속 감지돼도 강제 해제
//            (억제 래치 없음 → 아직 감지중이면 다음 루프에 곧바로 재트리거)
// ════════════════════════════════════════════════════════════════
void updateDangerState(bool detected) {
  if (dangerActive) {
    if (detected) dangerLastDetectedAt = millis();

    bool quietEnough = (millis() - dangerLastDetectedAt) > WARN_DURATION_MS;
    bool maxedOut    = (millis() - dangerStartedAt)      > WARN_MAX_DURATION_MS;

    if (quietEnough || maxedOut) {
      dangerActive = false;
      Serial.println(F("[WARN] 위험 해제"));
    }
  } else if (detected) {
    dangerActive = true;
    dangerStartedAt = millis();
    dangerLastDetectedAt = millis();
    Serial.println(F("[WARN] 위험 트리거 ON"));
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


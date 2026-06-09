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
 * 오디오 (alert_pcm.h — sound.py 와 '동일한' 합성 경고음을 PCM 그대로 재생):
 *   A0 (DAC) = 12bit PCM, 22050Hz. FspTimer ISR 이 샘플을 한 발(0.15s)씩 흘려보냄.
 *     PCM 안에 cutthrough 스윕 + 벨 배음 + 온셋 클릭 + 750Hz 보험이 이미 믹싱돼 있어
 *     단일 DAC 채널만으로 sound.py 결과물이 그대로 재생된다.
 *   D6 (PWM) = 미사용(예비). 옛 DuoBell 2톤 합산 방식은 PCM 재생으로 대체됨.
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
 *   - FspTimer            (UNO R4 보드패키지 기본 포함)
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "FspTimer.h"     // UNO R4 하드웨어 타이머 → 22050Hz 샘플 ISR

#include "secrets.h"
#include "config.h"
#include "alert_pcm.h"    // sound.py 에서 렌더한 경고음 PCM (12bit DAC, 22050Hz, 0.15s 1발)

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

// ─── 오디오 (PCM 경고음 재생) ───
//   FspTimer 가 ALERT_RATE(22050Hz) 로 ISR 을 돌리며 A0 DAC 에 한 샘플씩 출력.
//   s_pcmActive=true 인 동안만 ALERT_PCM 을 흘려보내고, 끝까지 가면 스스로 false 로.
//   driveAudio() 가 위험 중에 gap 을 두고 다시 arm → sound.py 의 repeat() 펄싱 재현.
FspTimer        audioTimer;
volatile uint32_t s_pcmIdx    = 0;      // 현재 재생 중인 샘플 인덱스
volatile bool     s_pcmActive = false;  // true = 한 발 재생 중
bool          prevPcmActive   = false;  // 펄스 종료 에지 검출용
unsigned long lastPulseEndMs  = 0;      // 마지막 펄스가 끝난 시각 (gap 계산)

// DAC 에 한 샘플 즉시 출력 (12bit, 0..4095). analogWriteResolution(12) 가 전제.
static inline void dacWrite(uint16_t v) { analogWrite(DAC, v); }

// 22050Hz 마다 호출되는 ISR — 다음 PCM 샘플을 DAC 로.
void audioISR(timer_callback_args_t * /*arg*/) {
  if (!s_pcmActive) return;                       // 무음 구간엔 아무것도 안 함
  dacWrite(pgm_read_word(&ALERT_PCM[s_pcmIdx]));
  if (++s_pcmIdx >= (uint32_t)ALERT_LEN) {        // 한 발(0.15s) 끝
    s_pcmIdx = 0;
    s_pcmActive = false;                          // driveAudio 가 gap 후 재arm
  }
}

// FspTimer 를 ALERT_RATE 로 시작 (항상 돌고, s_pcmActive 로 게이팅).
bool audioTimerBegin() {
  uint8_t type;
  int8_t ch = FspTimer::get_available_timer(type);
  if (ch < 0) {                                   // 남는 타이머 없으면 PWM 예약분에서
    FspTimer::force_use_of_pwm_reserved_timer();
    ch = FspTimer::get_available_timer(type, true);
  }
  if (ch < 0) return false;
  if (!audioTimer.begin(TIMER_MODE_PERIODIC, type, ch,
                        (float)ALERT_RATE, 0.0f, audioISR)) return false;
  if (!audioTimer.setup_overflow_irq()) return false;
  if (!audioTimer.open())  return false;
  if (!audioTimer.start()) return false;
  return true;
}

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

  // 오디오: DAC 12bit 모드 + 무음(중앙값)으로 파킹 후 22050Hz 타이머 가동
  analogWriteResolution(12);          // analogWrite(DAC,v) 의 v 를 0..4095 로 직접 매핑
  dacWrite(ALERT_MID);                // DAC 초기화 + DC 중앙값(무음)으로 시작
  if (!audioTimerBegin()) Serial.println(F("[오디오] 타이머 확보 실패 — 무음"));

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
//  오디오 (alert_pcm.h — sound.py 합성음을 PCM 그대로 펄싱 재생)
//    · 한 발 = ALERT_PCM 전체(0.15s). PCM 안에 스윕/벨/온셋/보험이 이미 믹싱됨.
//    · 위험 동안 한 발 재생 → WARN_PULSE_OFF_MS 무음(gap) → 다시 재생.
//      = sound.py 의 repeat(gap_sec) 펄싱을 펌웨어에서 재현 (정적 드론보다 돌출↑).
//    · 실제 샘플 출력은 audioISR(22050Hz) 이 담당. 여기선 arm/gap 만 관리(논블로킹).
// ════════════════════════════════════════════════════════════════
void driveAudio() {
  // 펄스 종료 에지(ISR 이 s_pcmActive 를 내림) 검출 → gap 시작 + DAC 무음 파킹
  if (prevPcmActive && !s_pcmActive) {
    lastPulseEndMs = millis();
    dacWrite(ALERT_MID);                 // 중앙값으로 파킹(DC 험/팝 방지)
  }
  prevPcmActive = s_pcmActive;

  if (!dangerActive) {                    // 위험 아니면 즉시 정지
    if (s_pcmActive) { s_pcmActive = false; dacWrite(ALERT_MID); }
    return;
  }

  if (s_pcmActive) return;                                 // 한 발 재생 중
  if (millis() - lastPulseEndMs < (unsigned long)WARN_PULSE_OFF_MS) return;  // gap 대기

  // 다음 한 발 arm (ISR 이 0 번 샘플부터 흘려보냄)
  s_pcmIdx = 0;
  s_pcmActive = true;
}


/*
 * adventure_2026.ino  -  메인 스케치
 * 스몸비 안전 경고 장치 (Arduino UNO R4 WiFi)
 *
 * 모드 구성 (실측 배선) — 전부 모멘터리 푸시 버튼:
 *   D13 (PIN_BTN_SUBWAY) : A → ①지하철 API 모드  (※ D13=온보드 LED 핀)
 *   D11 (PIN_BTN_BUS)    : B → ②버스 API 모드
 *   D10 (PIN_BTN_SPAT)   : C → ③C-ITS API 모드
 *   D9  (PIN_BTN_MODE)   : D → API 카테고리 ↔ SENSOR 모드 토글
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
#include "FspTimer.h"   // 고음(D6) 소프트 음량용 듀티-PWM 타이머

#include "secrets.h"
#include "config.h"

// ════════════════════════════════════════════════════════════════
//  전역 상태
// ════════════════════════════════════════════════════════════════

// ─── 모드 ───
//   ApiMode: API 카테고리 안에서 선택된 모드 (D13/D11/D10 버튼으로 직접 선택)
//   SENSOR  모드는 별도 플래그 (D9 버튼 토글)
enum ApiMode {
  API_BUS = 0,
  API_SUBWAY,
  API_SPAT,
  API_COUNT
};
ApiMode currentApiMode = API_BUS;   // 마지막으로 선택된 API 모드
bool sensorMode = false;            // true = SENSOR 모드 (D9 토글)
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
bool dangerSuppressed = false;           // 강제 해제 후 새 위험 감지 시까지 재트리거 방지 래치
unsigned long dangerLastDetectedAt = 0;  // 마지막 감지 시각 (조용해지면 해제용)
unsigned long dangerStartedAt = 0;       // 위험 ON 된 시각 (최대 지속시간 캡용)

// ─── 폴링 타이머 ───
unsigned long lastFetchBus    = 0;
unsigned long lastFetchSubway = 0;
unsigned long lastFetchSpat   = 0;
unsigned long lastDraw        = 0;
const unsigned long DRAW_INTERVAL_MS = 250UL;

// ─── 버튼 디바운스 (struct Btn 정의는 config.h — IDE 자동 prototype 회피용) ───
Btn btnSubway = { PIN_BTN_SUBWAY, HIGH, 0 };  // D13 (A) : → SUBWAY
Btn btnBus    = { PIN_BTN_BUS,    HIGH, 0 };  // D11 (B) : → BUS
Btn btnSpat   = { PIN_BTN_SPAT,   HIGH, 0 };  // D10 (C) : → CITS
Btn btnMode   = { PIN_BTN_MODE,   HIGH, 0 };  // D9  (D) : API ↔ SENSOR 토글

// ─── OLED ─── (config.h: OLED_USE_SW_I2C 로 HW(A4/A5) ↔ SW(D8/D7) 전환)
#if OLED_USE_SW_I2C
//  소프트웨어 I2C 폴백 — A4/A5 손상 시. OLED SCL→D8, SDA→D7 로 옮겨 꽂을 것.
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=SCL*/ OLED_SW_SCL_PIN, /* data=SDA*/ OLED_SW_SDA_PIN, /* reset=*/ U8X8_PIN_NONE);
#else
//  하드웨어 I2C (기본) — A4=SDA, A5=SCL. 소프트 비트뱅잉 타이밍 문제 회피.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#endif
const int OLED_WIDTH = 128;
const int OLED_MAX_TEXT_WIDTH = 124;

// ─── HTTP ───
WiFiClient    plainSock;
WiFiSSLClient sslSock;

// ─── 오디오 (DuoBell) ───
analogWave wave(DAC);   // A0 = DAC, 저음 사인 (음량 = wave.amplitude)
bool warnAudioOn = false;

// ─── 고음(D6) 소프트 음량 PWM 엔진 ───
//   tone() 은 고정 진폭 → 음량 조절 불가. 그래서 2kHz 사각파를 FspTimer 듀티-PWM 으로 직접 생성.
//   ISR 을 TONE_FREQ_SECONDARY*HI_RES (=40kHz) 로 돌리며 듀티(=음량)만큼만 HIGH.
//   PAM8302 입력 캡 + 스피커가 평균 → 듀티 깊이가 곧 2kHz 톤의 음량.
#define HI_RES 20                       // 듀티 해상도 (2kHz x 20 = 40kHz ISR)
FspTimer hiTimer;
volatile bool     hiOn   = false;       // 고음 출력 중?
volatile uint16_t hiDuty = 0;           // HIGH 유지 틱수 (0..HI_RES/2 = 0~50% duty)
volatile uint16_t hiCtr  = 0;

void hiISR(timer_callback_args_t *) {
  if (!hiOn) return;                    // 무음 구간(warnOff 가 핀 LOW 로 둠)
  if (++hiCtr >= HI_RES) hiCtr = 0;
  digitalWrite(PIN_SPK_PWM, (hiCtr < hiDuty) ? HIGH : LOW);
}

bool hiPwmBegin() {
  uint8_t type;
  int8_t ch = FspTimer::get_available_timer(type);
  if (ch < 0) { FspTimer::force_use_of_pwm_reserved_timer(); ch = FspTimer::get_available_timer(type, true); }
  if (ch < 0) return false;
  float rate = (float)TONE_FREQ_SECONDARY * HI_RES;
  if (!hiTimer.begin(TIMER_MODE_PERIODIC, type, ch, rate, 0.0f, hiISR)) return false;
  if (!hiTimer.setup_overflow_irq()) return false;
  if (!hiTimer.open())  return false;
  // 평상시엔 정지(start 안 함). 위험 중 hiSet() 에서만 가동 → 비-경고 시 40kHz ISR 0
  // → fetch/NTP 등 WiFi 통신 타이밍 방해 방지.
  hiTimer.stop();
  return true;
}

void hiSet(float vol) {            // 음량 0~1 → 듀티 0~50%
  hiDuty = (uint16_t)(vol * (HI_RES / 2) + 0.5f);
  hiOn = true;
  hiTimer.start();                 // 경고 동안만 ISR 가동
}
void hiStop() {
  hiOn = false;
  hiTimer.stop();                  // 무음 시 타이머 정지 (ISR 0)
  digitalWrite(PIN_SPK_PWM, LOW);
}

// ─── 시간대 음량 (NTP 한 번 받고 millis 로 보간, KST) ───
bool          timeSynced = false;
unsigned long epochAtSync = 0;          // 동기화 시점 KST epoch(초)
unsigned long msAtSync    = 0;

void syncTimeNTP() {
  if (netState != NET_OK) return;
  // WiFiS3(ESP32-S3) 내장 시각 사용. ESP32 가 연결 후 SNTP 로 자동 동기화하므로
  // UDP 포트123(핫스팟에서 자주 차단)을 직접 쓰지 않아 더 안정적. 동기화엔 몇 초 걸릴 수 있어 재시도.
  unsigned long epoch = 0;
  for (int i = 0; i < 12; i++) {
    epoch = WiFi.getTime();
    if (epoch > 1000000000UL) break;   // 유효(2001년 이후)
    delay(500);
  }
  if (epoch <= 1000000000UL) {
    Serial.println(F("[시각] 동기화 실패 → 낮 음량 기본값"));
    return;
  }
  epochAtSync = epoch + NTP_TZ_OFFSET_SEC;   // → KST epoch
  msAtSync    = millis();
  timeSynced  = true;
  unsigned long sod = epochAtSync % 86400UL;
  Serial.print(F("[시각] KST ")); Serial.print(sod / 3600); Serial.print(':');
  Serial.println((sod % 3600) / 60);
}

int minuteOfDay() {
  if (!timeSynced) return -1;
  unsigned long e = epochAtSync + (millis() - msAtSync) / 1000UL;
  return (int)((e % 86400UL) / 60UL);
}

// 현재 음량(0~1). 미동기화 시 낮(크게)로 안전 폴백.
float currentVolume() {
  int m = minuteOfDay();
  bool day = (m < 0) ? true : (m >= VOL_DAY_START_MIN && m < VOL_DAY_END_MIN);
  float v = day ? VOL_DAY : VOL_NIGHT;
  return v > VOL_MAX ? VOL_MAX : v;
}

// ─── 웹 디스플레이 push (Vercel /api/status) ───
//   현재 displayData + 모드 + 위험여부를 JSON 으로 POST. (config.h ENABLE_WEB_PUSH)
#if ENABLE_WEB_PUSH
unsigned long lastWebPush = 0;
unsigned long webPushCooldownUntil = 0;   // 실패 시 잠깐 쉬어 반복 블로킹 방지
char          lastPushSig[100] = "";
void pushStatus() {
  if (netState != NET_OK || !displayData.valid) return;
  if ((long)(millis() - webPushCooldownUntil) < 0) return;   // 쿨다운 중

  // 내용 시그니처(모드|line1|line2|위험). 바뀌면 즉시, 안 바뀌면 하트비트 주기에만 전송.
  char sig[100];
  snprintf(sig, sizeof(sig), "%s|%s|%s|%d",
           currentModeLabel(), displayData.line1, displayData.line2, dangerActive ? 1 : 0);
  bool changed = (strcmp(sig, lastPushSig) != 0);
  if (!changed && (millis() - lastWebPush < WEB_PUSH_MIN_INTERVAL_MS)) return;
  lastWebPush = millis();

  char body[220];   // 한글(UTF-8 멀티바이트)이라 넉넉히
  snprintf(body, sizeof(body),
    "{\"mode\":\"%s\",\"line1\":\"%s\",\"line2\":\"%s\",\"danger\":%s,\"ts\":%lu}",
    currentModeLabel(), displayData.line1, displayData.line2,
    dangerActive ? "true" : "false", millis());

  // SPAT(HTTPS) 와 같은 sslSock 을 공유하므로, 이전 연결 잔재를 끊고 깨끗이 새로 연결.
  // (stale 연결 재사용 → 응답 안 와서 HTTP_ERROR_TIMED_OUT(-3) 의 주원인)
  sslSock.stop();
  HttpClient http(sslSock, WEB_PUSH_HOST, 443);
  http.setHttpResponseTimeout(6000);   // 실패해도 6s 안에 빠짐(30s 행 방지)
  http.beginRequest();
  http.post(WEB_PUSH_PATH);
  http.sendHeader(F("Content-Type"), F("application/json"));
  http.sendHeader(F("Content-Length"), (int)strlen(body));
  http.sendHeader(F("Connection"), F("close"));
  http.beginBody();
  http.print(body);
  http.endRequest();
  int code = http.responseStatusCode();
  http.stop();

  if (code == 200) {
    strncpy(lastPushSig, sig, sizeof(lastPushSig) - 1);
    lastPushSig[sizeof(lastPushSig) - 1] = '\0';
  } else {
    Serial.print(F("[WEB push] 실패 code=")); Serial.println(code);
    webPushCooldownUntil = millis() + 20000UL;   // 20s 쉬고 재시도
  }
}
#endif

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

  // 오디오: 780Hz 사인(DAC) + 2kHz 고음(D6 듀티-PWM, 소프트 음량) — 무음 상태로 준비
  wave.sine(TONE_FREQ_PRIMARY);
  wave.amplitude(0.0);
  digitalWrite(PIN_SPK_PWM, LOW);
  if (!hiPwmBegin()) Serial.println(F("[오디오] 고음 PWM 타이머 확보 실패"));

  // OLED
#if OLED_USE_SW_I2C
  Serial.println(F("[OLED] 소프트웨어 I2C (SCL=D8, SDA=D7)"));
#else
  // 하드웨어 I2C: A4/A5 가 살아있는지 부팅 스캔으로 즉시 진단
  Wire.begin();
  Serial.print(F("[I2C 스캔] "));
  byte found = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("0x")); Serial.print(a, HEX); Serial.print(' ');
      found++;
    }
  }
  if (found) Serial.println(F("발견 (0x3C 보이면 OLED 정상 연결)"));
  else       Serial.println(F("→ 아무것도 없음! A4/A5 핀/결선 사망 의심 → config.h OLED_USE_SW_I2C 1 + OLED 를 D8/D7 로"));
#endif
  u8g2.begin();
  drawScreen();

  // 부팅 시작 모드: API / BUS
  sensorMode = false;
  currentApiMode = API_BUS;
  Serial.print(F("[부팅 모드] API / "));
  Serial.println(apiModeName(currentApiMode));

  // WiFi (API 모드일 때 필요 — 센서 모드여도 미리 연결)
  connectWiFi();
  syncTimeNTP();   // 시간대 음량 스케줄용 현재시각(KST) 동기화

  Serial.println(F("[부팅 완료] D9=API/SENSOR, D13/D11/D10=SUBWAY/BUS/CITS"));
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
#if ENABLE_WEB_PUSH
  pushStatus();      // 큰 화면 웹앱으로 상태 전송 (위험 변화 시 즉시, 그 외 주기적)
#endif
}

// ════════════════════════════════════════════════════════════════
//  버튼 입력 → 모드 선택
//   D9         : API 카테고리 ↔ SENSOR 토글 (OLED 내장 버튼 D)
//   D13/D11/D10: SUBWAY/BUS/CITS 직접 선택 (자동으로 API 카테고리 진입)
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
  // D9 (OLED 버튼 D): API ↔ SENSOR 토글
  if (btnPressed(btnMode)) {
    sensorMode = !sensorMode;
    enterMode();
    Serial.print(F("[버튼] 토글 → "));
    Serial.println(sensorMode ? F("SENSOR") : F("API"));
  }

  // D13/D11/D10: API 모드 직접 선택 (SENSOR 였어도 API로 복귀)
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
//            (강제 해제 시 래치 작동 → 위험 상태가 완전히 꺼지기 전에는 재트리거 방지)
// ════════════════════════════════════════════════════════════════
void updateDangerState(bool detected) {
  if (dangerActive) {
    if (detected) dangerLastDetectedAt = millis();

    bool quietEnough = (millis() - dangerLastDetectedAt) > WARN_DURATION_MS;
    bool maxedOut    = (millis() - dangerStartedAt)      > WARN_MAX_DURATION_MS;

    if (quietEnough || maxedOut) {
      dangerActive = false;
      if (maxedOut) {
        dangerSuppressed = true; // 강제 해제 시 래치 활성화
        Serial.println(F("[WARN] 위험 강제 해제 (최대 시간 초과)"));
      } else {
        Serial.println(F("[WARN] 위험 해제 (감지 종료)"));
      }
    }
  } else {
    if (!detected) {
      dangerSuppressed = false; // 위험 요소가 완전히 사라지면 래치 해제
    }

    if (detected && !dangerSuppressed) {
      dangerActive = true;
      dangerStartedAt = millis();
      dangerLastDetectedAt = millis();
      Serial.println(F("[WARN] 위험 트리거 ON"));
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  오디오 (DuoBell 2채널 — 780Hz DAC 사인 + 2kHz D6 듀티-PWM, 빠른 펄싱)
//    · 정적 드론 대신 짧은 on/off 펄싱 → 돌출↑ / 습관화 방지
//    · 두 채널 모두 currentVolume() 으로 시간대(낮/밤) 음량 적용
//      저음 = wave.amplitude(vol), 고음 = hiSet(vol)(듀티 깊이=음량, PAM8302 구동)
//    · 절대 음량은 PAM8302 트리머(노즐)로 최종 조정 — 소프트 음량과 곱해짐
// ════════════════════════════════════════════════════════════════
void warnOn() {
  float vol = currentVolume();                  // 시간대(낮/밤) 음량 0~1
  wave.amplitude(vol);                          // 저음 780Hz 사인(DAC) — 음량 적용
  hiSet(vol);                                   // 고음 2kHz(D6 듀티-PWM) — 음량 적용
  warnAudioOn = true;
}

void warnOff() {
  if (!warnAudioOn) return;
  wave.amplitude(0.0);                          // 저음 OFF
  hiStop();                                     // 고음 OFF (핀 LOW)
  warnAudioOn = false;
}

void driveAudio() {
  if (!dangerActive) { warnOff(); return; }
  // 위험 동안: 짧은 on/off 펄싱 (정적 드론 대신)
  unsigned long period = (unsigned long)WARN_PULSE_ON_MS + WARN_PULSE_OFF_MS;
  if ((millis() % period) < (unsigned long)WARN_PULSE_ON_MS) warnOn();
  else                                                       warnOff();
}


/*
 * test_oled.ino  -  Phase 1-4 검증
 * SSD1306 128x64 OLED 단독 테스트
 *
 * 변경 사항:
 *   - 한글 폭 제약 (128px / 16px = 8자) 반영해 문구 단축
 *   - drawCenteredTrimmed() 추가: 폭 넘으면 자동 ... 처리
 *     → 실제 API 응답이 길어져도 안전
 *
 * 보드:   Arduino UNO R4 WiFi
 * 연결:   I2C (SCL=A5, SDA=A4)
 * 라이브러리: U8g2
 *
 * 폰트 폭 참고 (u8g2_font_unifont_t_korean1):
 *   - ASCII/숫자/공백: 8px
 *   - 한글:           16px
 *   - 128px 폭에 한글 최대 8자, 또는 한글5자+ASCII몇자 조합
 */

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// 표시 폭 제한 (오른쪽 여백 확보)
const int MAX_TEXT_WIDTH = 124;

// ─── 모드 정의 ───
enum Mode {
  MODE_BUS = 0,
  MODE_SUBWAY,
  MODE_CITS,
  MODE_SENSOR,
  MODE_COUNT
};

// ─── 모드별 문구 (짧게 줄임) ───
const char* modeShortName(Mode m) {
  switch (m) {
    case MODE_BUS:    return "BUS";
    case MODE_SUBWAY: return "SUBWAY";
    case MODE_CITS:   return "CITS";
    case MODE_SENSOR: return "SENSOR";
    default:          return "?";
  }
}
const char* modeContext(Mode m) {
  switch (m) {
    case MODE_BUS:    return "번동사거리";       // 5자
    case MODE_SUBWAY: return "수유역";           // 3자
    case MODE_CITS:   return "A123 교차로";      // 단축
    case MODE_SENSOR: return "센서 모드";        // 단축
    default:          return "";
  }
}
const char* modeSafeInfo(Mode m) {
  switch (m) {
    case MODE_BUS:    return "노원14 3분";       // 단축
    case MODE_SUBWAY: return "4호선 145s";       // s = 초
    case MODE_CITS:   return "북 12s 남 8s";     // 두 방향
    case MODE_SENSOR: return "대기 중";          // 단축
    default:          return "";
  }
}
const char* modeDangerInfo(Mode m) {
  switch (m) {
    case MODE_BUS:    return "노원14 도착";      // ! 제거
    case MODE_SUBWAY: return "4호선 출발";       // 단축
    case MODE_CITS:   return "보행 3.5s";        // 방향 생략 (위험 자체가 핵심)
    case MODE_SENSOR: return "차량 접근";        // 단축
    default:          return "";
  }
}

// ─── 순환 상태 ───
const unsigned long PHASE_MS = 3000;
unsigned long lastChange = 0;
int phase = 0;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n=== [Phase 1-4] OLED 테스트 ==="));

  Wire.begin();
  if (!u8g2.begin()) {
    Serial.println(F("⚠️ OLED 초기화 실패 - I2C 연결 확인"));
  } else {
    Serial.println(F("OLED 초기화 OK"));
  }

  drawScreen(MODE_BUS, false);
  printPhaseLog(MODE_BUS, false);
  lastChange = millis();
}

void loop() {
  if (millis() - lastChange >= PHASE_MS) {
    lastChange = millis();
    phase = (phase + 1) % (MODE_COUNT * 2);

    Mode mode   = (Mode)(phase / 2);
    bool danger = (phase % 2 == 1);

    drawScreen(mode, danger);
    printPhaseLog(mode, danger);
  }
}

void printPhaseLog(Mode mode, bool danger) {
  Serial.print(F("[")); Serial.print(phase); Serial.print(F("] "));
  Serial.print(modeShortName(mode));
  Serial.print(F(" | "));
  Serial.println(danger ? F("위험") : F("안전"));
}

// ═════════════════════════════════════════════════════════════
// 렌더링
// ═════════════════════════════════════════════════════════════
void drawScreen(Mode mode, bool danger) {
  u8g2.clearBuffer();
  drawHeader(mode);
  drawStatus(danger);
  drawDetail(mode, danger);
  u8g2.sendBuffer();
}

// ─── 헤더 (y 0~11) ───
void drawHeader(Mode mode) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);

  u8g2.drawStr(0, 9, modeShortName(mode));

  const char* wifi = "-55 dBm";
  int w = u8g2.getStrWidth(wifi);
  u8g2.drawStr(128 - w, 9, wifi);

  u8g2.drawHLine(0, 11, 128);
}

// ─── 상태 (y 12~33) ───
void drawStatus(bool danger) {
  u8g2.setFont(u8g2_font_unifont_t_korean1);

  if (danger) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 12, 128, 22);
    u8g2.setDrawColor(0);
    drawCenteredTrimmed("!! 위험 !!", 29);
    u8g2.setDrawColor(1);
  } else {
    drawCenteredTrimmed("정상", 29);
  }

  u8g2.drawHLine(0, 34, 128);
}

// ─── 상세 (y 35~63) ───
void drawDetail(Mode mode, bool danger) {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);

  drawCenteredTrimmed(modeContext(mode), 48);
  drawCenteredTrimmed(
    danger ? modeDangerInfo(mode) : modeSafeInfo(mode),
    62
  );
}

// ═════════════════════════════════════════════════════════════
// 헬퍼: UTF-8 가운데 정렬 + 자동 trim
//   - MAX_TEXT_WIDTH 넘으면 끝에 "..."
//   - UTF-8 바이트 경계 존중 (한글 중간에서 안 자름)
// ═════════════════════════════════════════════════════════════
void drawCenteredTrimmed(const char* text, int y) {
  // 폭 측정
  int w = u8g2.getUTF8Width(text);
  if (w <= MAX_TEXT_WIDTH) {
    int x = (128 - w) / 2;
    u8g2.drawUTF8(x, y, text);
    return;
  }

  // trim 필요: 임시 버퍼에 한 글자씩 줄여가며 측정
  static char buf[64];
  int len = strlen(text);
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  strncpy(buf, text, len);
  buf[len] = '\0';

  // "..." 자리 확보 (24px ≈ 3 dots × 8px)
  while (len > 0) {
    // UTF-8 마지막 문자 시작점 찾기
    int cut = len - 1;
    while (cut > 0 && (buf[cut] & 0xC0) == 0x80) cut--;
    buf[cut] = '\0';

    // "..." 붙여서 폭 측정
    char tmp[68];
    snprintf(tmp, sizeof(tmp), "%s...", buf);
    if (u8g2.getUTF8Width(tmp) <= MAX_TEXT_WIDTH) {
      int x = (128 - u8g2.getUTF8Width(tmp)) / 2;
      u8g2.drawUTF8(x, y, tmp);
      return;
    }
    len = cut;
  }

  // 너무 길어서 한 글자도 못 넣음 → "..."만
  u8g2.drawUTF8(56, y, "...");
}

/*
 * test_radar_oled.ino  -  Phase 1 RCWL + OLED 검증
 * RCWL-0516 마이크로파 도플러 레이다 단독 테스트
 *
 * 목적:
 *   - RCWL 결선/극성 확인
 *   - 디지털 출력 → OLED 표시 패턴 검증
 *   - 트리거 횟수/지속시간/주기 관찰
 *     → NOTES_RCWL_FALSEPOSITIVE.md 분석 자료
 *
 * 결선:
 *   RCWL-0516       UNO R4 WiFi
 *   ─────────────────────────────
 *   VIN     ──→     5V          (4-28V 입력)
 *   GND     ──→     GND
 *   OUT     ──→     D3          (config.h: PIN_RADAR)
 *
 *   OLED            UNO R4 WiFi
 *   ─────────────────────────────
 *   VCC     ──→     3.3V or 5V
 *   GND     ──→     GND
 *   SCL     ──→     SCL (A5)
 *   SDA     ──→     SDA (A4)
 *
 * ⚠️ RCWL 특성:
 *   - 부팅 후 약 1분간 안정화 기간 (이 동안 오감지 가능)
 *   - 출력 HIGH 유지 시간 2-3초 (감지 후 풀리는데 시간 걸림)
 *   - 사람/차량 구분 불가 (도플러는 움직임만 봄)
 *   - 벽 투과됨 (방 옆방 움직임도 감지 가능)
 */

#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"

// ─── OLED ───
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
const int MAX_TEXT_WIDTH = 124;

// ─── RCWL 상태 ───
struct RadarData {
  bool detecting;                  // 현재 HIGH 상태
  int  triggerCount;               // 부팅 후 LOW→HIGH 카운트
  unsigned long lastTriggerMs;     // 마지막 트리거 시점 (millis)
  unsigned long currentHighSince;  // 현재 HIGH 진입 시점
  unsigned long lastHighDuration;  // 직전 HIGH 지속 시간 (ms)
  unsigned long bootedAt;          // 부팅 시점 (안정화 표시용)
};
RadarData radar = { false, 0, 0, 0, 0, 0 };

const unsigned long WARMUP_MS       = 60000UL;  // 1분 안정화 권장
const unsigned long DRAW_INTERVAL_MS = 200UL;   // 200ms 반응성
unsigned long lastDraw = 0;
bool prevState = false;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);
  Serial.println(F("\n=== [Phase 1] RCWL-0516 + OLED ==="));
  Serial.print(F("RCWL Pin: D")); Serial.println(PIN_RADAR);
  Serial.println(F("⚠️ 1분 안정화 권장 - 그 동안 오감지 가능"));

  pinMode(PIN_RADAR, INPUT);
  radar.bootedAt = millis();

  Wire.begin();
  u8g2.begin();

  drawScreen();
}

// ═════════════════════════════════════════════════════════════
void loop() {
  // ── RCWL 읽기 (매 loop) ──
  bool nowState = (digitalRead(PIN_RADAR) == HIGH);

  // 상승 에지: 새 감지 시작
  if (nowState && !prevState) {
    radar.triggerCount++;
    radar.lastTriggerMs = millis();
    radar.currentHighSince = millis();
    Serial.print(F("[감지 ↑] #"));
    Serial.print(radar.triggerCount);
    Serial.print(F("  t="));
    Serial.print(millis() / 1000);
    Serial.println(F("s"));
  }
  // 하강 에지: 감지 종료
  if (!nowState && prevState) {
    radar.lastHighDuration = millis() - radar.currentHighSince;
    Serial.print(F("[감지 ↓] 지속 "));
    Serial.print(radar.lastHighDuration);
    Serial.println(F(" ms"));
  }
  radar.detecting = nowState;
  prevState = nowState;

  // ── OLED 주기 갱신 ──
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawScreen();
  }
}

// ═════════════════════════════════════════════════════════════
// OLED 렌더링
// ═════════════════════════════════════════════════════════════
void drawScreen() {
  u8g2.clearBuffer();
  drawHeader();
  drawStatus();
  drawDetail();
  u8g2.sendBuffer();
}

// ─── 헤더 ───
void drawHeader() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);
  u8g2.drawStr(0, 9, "RCWL");

  // 오른쪽: 핀 번호 + 원시 상태
  char info[20];
  snprintf(info, sizeof(info), "D%d %s",
           PIN_RADAR, radar.detecting ? "HIGH" : "LOW ");
  int w = u8g2.getStrWidth(info);
  u8g2.drawStr(128 - w, 9, info);

  u8g2.drawHLine(0, 11, 128);
}

// ─── 상태 (감지 / 대기 / 안정화) ───
void drawStatus() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);

  // 안정화 기간엔 별도 표시
  bool warming = (millis() - radar.bootedAt) < WARMUP_MS;

  if (radar.detecting) {
    // 인버트 (시선 강제)
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 12, 128, 22);
    u8g2.setDrawColor(0);
    drawCenteredTrimmed("!! 감지 !!", 29);
    u8g2.setDrawColor(1);
  } else if (warming) {
    drawCenteredTrimmed("안정화 중", 29);
  } else {
    drawCenteredTrimmed("대기 중", 29);
  }

  u8g2.drawHLine(0, 34, 128);
}

// ─── 상세 (감지 횟수 + 시간) ───
void drawDetail() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);

  // 줄 1: 누적 감지 횟수
  char line1[32];
  snprintf(line1, sizeof(line1), "감지 %d회", radar.triggerCount);
  drawCenteredTrimmed(line1, 48);

  // 줄 2: 시간 정보
  //   - 감지 중이면: 현재 HIGH 지속 시간
  //   - 대기 중이면: 마지막 감지로부터 경과
  //   - 부팅 후 한 번도 감지 없으면: "아직 없음"
  char line2[32];
  if (radar.detecting && radar.currentHighSince > 0) {
    unsigned long dur = millis() - radar.currentHighSince;
    snprintf(line2, sizeof(line2), "지속 %lu.%lus",
             dur / 1000, (dur % 1000) / 100);
  } else if (radar.lastTriggerMs > 0) {
    unsigned long ago = (millis() - radar.lastTriggerMs) / 1000;
    if (ago < 60) {
      snprintf(line2, sizeof(line2), "%lus 전", ago);
    } else if (ago < 3600) {
      snprintf(line2, sizeof(line2), "%lum 전", ago / 60);
    } else {
      snprintf(line2, sizeof(line2), "%luh 전", ago / 3600);
    }
  } else {
    strcpy(line2, "아직 없음");
  }
  drawCenteredTrimmed(line2, 62);
}

// ═════════════════════════════════════════════════════════════
// UTF-8 가운데정렬 + 자동 trim (다른 테스트와 동일)
// ═════════════════════════════════════════════════════════════
void drawCenteredTrimmed(const char* text, int y) {
  int w = u8g2.getUTF8Width(text);
  if (w <= MAX_TEXT_WIDTH) {
    int x = (128 - w) / 2;
    u8g2.drawUTF8(x, y, text);
    return;
  }

  static char buf[64];
  int len = strlen(text);
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  strncpy(buf, text, len);
  buf[len] = '\0';

  while (len > 0) {
    int cut = len - 1;
    while (cut > 0 && (buf[cut] & 0xC0) == 0x80) cut--;
    buf[cut] = '\0';

    char tmp[68];
    snprintf(tmp, sizeof(tmp), "%s...", buf);
    if (u8g2.getUTF8Width(tmp) <= MAX_TEXT_WIDTH) {
      int x = (128 - u8g2.getUTF8Width(tmp)) / 2;
      u8g2.drawUTF8(x, y, tmp);
      return;
    }
    len = cut;
  }
  u8g2.drawUTF8(56, y, "...");
}

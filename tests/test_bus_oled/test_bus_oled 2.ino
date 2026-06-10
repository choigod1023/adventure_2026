/*
 * test_bus_oled.ino  -  Phase 1 마지막 통합 검증
 * 버스 API 응답을 OLED에 실시간 표시
 *
 * 목적:
 *   - "API에서 받은 데이터를 OLED에 출력" 패턴 확정
 *   - 위험 판정 → 화면 상태 전환 (정상/위험) 검증
 *   - 네트워크/API 에러 시 폴백 UI 검증
 *   - 이 코드 패턴이 검증되면 SUBWAY/CITS/SENSOR 모드는 거의 복붙
 *
 * 보드:   Arduino UNO R4 WiFi
 * 연결:
 *   - OLED: I2C (SCL=A5, SDA=A4, VCC, GND)
 * 라이브러리: WiFiS3 / ArduinoHttpClient / ArduinoJson v7 / U8g2
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "secrets.h"
#include "config.h"

// ─────────────────────────────────────────────────────────────
// OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
const int MAX_TEXT_WIDTH = 124;

// API
const char HOST[]     = "ws.bus.go.kr";
const int  PORT       = 80;
const char API_PATH[] = "/api/rest/stationinfo/getStationByUid";

WiFiClient sock;
HttpClient http(sock, HOST, PORT);

// ─── 네트워크 상태 ───
enum NetState { NET_BOOT, NET_WIFI, NET_OK, NET_OFFLINE };
NetState netState = NET_BOOT;

// ─── 버스 데이터 ───
struct BusData {
  bool valid;
  bool danger;
  unsigned long updatedAt;
  char station[32];      // 정류장명
  char route[16];        // 표시할 노선 번호
  char msg[40];          // arrmsg1 (도착 메시지)
  int  totalBuses;       // 응답에 들어온 버스 수
};
BusData busData = { false, false, 0, "", "", "", 0 };

// ─── 폴링 / 렌더 주기 ───
const unsigned long FETCH_INTERVAL_MS = 30000UL;   // 30초 (일일 한도 고려)
const unsigned long DRAW_INTERVAL_MS  = 1000UL;    // 1초 (RSSI 갱신 등)
unsigned long lastFetch = 0;
unsigned long lastDraw  = 0;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);
  Serial.println(F("\n=== [Phase 1] 버스 API + OLED 통합 ==="));
  Serial.print(F("ARS ID: ")); Serial.println(BUS_ARS_ID);

  Wire.begin();
  u8g2.begin();

  // 첫 화면 (부팅 중)
  drawScreen();
  delay(300);

  connectWiFi();
}

// ═════════════════════════════════════════════════════════════
void loop() {
  // WiFi 끊김 감지
  if (WiFi.status() != WL_CONNECTED) {
    if (netState != NET_OFFLINE) {
      netState = NET_OFFLINE;
      drawScreen();
    }
    connectWiFi();
    return;
  }

  // 주기적 API 호출
  if (lastFetch == 0 || millis() - lastFetch >= FETCH_INTERVAL_MS) {
    lastFetch = millis();
    fetchBus();
    drawScreen();   // 호출 직후 즉시 반영
  }

  // 주기적 화면 갱신 (RSSI/시계 등)
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    drawScreen();
  }
}

// ═════════════════════════════════════════════════════════════
// WiFi
// ═════════════════════════════════════════════════════════════
void connectWiFi() {
  netState = NET_WIFI;
  drawScreen();

  Serial.print(F("WiFi 연결 중 "));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n실패"));
    netState = NET_OFFLINE;
    drawScreen();
    delay(3000);
    return;
  }

  // IP 대기
  for (int i = 0; i < 20; i++) {
    if (WiFi.localIP()[0] != 0) break;
    delay(500);
  }
  netState = NET_OK;
  Serial.print(F("\n연결 OK | IP: ")); Serial.println(WiFi.localIP());
  drawScreen();
}

// ═════════════════════════════════════════════════════════════
// 버스 API
// ═════════════════════════════════════════════════════════════
void fetchBus() {
  Serial.println(F("\n[API 호출]"));

  String path = String(API_PATH)
              + "?serviceKey=" + BUS_API_KEY
              + "&arsId="      + BUS_ARS_ID
              + "&resultType=json";

  http.beginRequest();
  http.get(path);
  http.sendHeader(F("Accept"), F("application/json"));
  http.sendHeader(F("Connection"), F("close"));
  http.endRequest();

  int statusCode = http.responseStatusCode();
  Serial.print(F("HTTP: ")); Serial.println(statusCode);

  if (statusCode != 200) {
    http.stop();
    return;
  }

  String body = http.responseBody();
  http.stop();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println(F("JSON 파싱 실패"));
    return;
  }

  const char* hdrCd = doc["msgHeader"]["headerCd"] | "";
  if (strcmp(hdrCd, "0") != 0) {
    Serial.print(F("API 에러 헤더: ")); Serial.println(hdrCd);
    return;
  }

  JsonVariant items = doc["msgBody"]["itemList"];
  if (items.isNull()) {
    Serial.println(F("itemList 없음"));
    return;
  }

  // ─── 위험/표시 대상 선정 ───
  // 규칙:
  //   1) 위험 버스(곧 도착/출발)가 있으면 → 그 버스 표시 + danger=true
  //   2) 없으면 → 첫 번째 버스 표시 + danger=false
  busData.danger = false;
  busData.totalBuses = 0;
  bool urgentFound = false;
  bool firstStored = false;

  if (items.is<JsonArray>()) {
    for (JsonObject item : items.as<JsonArray>()) {
      processItem(item, urgentFound, firstStored);
    }
  } else if (items.is<JsonObject>()) {
    processItem(items.as<JsonObject>(), urgentFound, firstStored);
  }

  busData.valid = true;
  busData.updatedAt = millis();

  Serial.print(F("정류장   : ")); Serial.println(busData.station);
  Serial.print(F("표시 버스 : ")); Serial.print(busData.route);
  Serial.print(F(" / "));          Serial.println(busData.msg);
  Serial.print(F("총 버스  : ")); Serial.println(busData.totalBuses);
  Serial.print(F("위험     : ")); Serial.println(busData.danger ? "YES" : "NO");
}

void processItem(JsonObject item, bool &urgentFound, bool &firstStored) {
  busData.totalBuses++;

  const char* stNm    = item["stNm"]    | "";
  const char* rtNm    = item["rtNm"]    | "?";
  const char* arrmsg1 = item["arrmsg1"] | "정보 없음";

  // 정류장명은 한 번만 저장 (전 버스 동일)
  if (busData.station[0] == '\0') {
    strncpy(busData.station, stNm, sizeof(busData.station) - 1);
    busData.station[sizeof(busData.station) - 1] = '\0';
  }

  // 위험 판정
  bool urgent = (strstr(arrmsg1, "곧 도착") != NULL) ||
                (strncmp(arrmsg1, "출발", 4) == 0 && !strstr(arrmsg1, "출발대기"));

  // 위험 발견 시 즉시 덮어쓰기 (가장 먼저 발견된 위험 버스)
  if (urgent && !urgentFound) {
    urgentFound = true;
    busData.danger = true;
    setDisplayBus(rtNm, arrmsg1);
    return;
  }

  // 위험 없을 때 첫 번째 버스 저장
  if (!urgentFound && !firstStored) {
    firstStored = true;
    setDisplayBus(rtNm, arrmsg1);
  }
}

void setDisplayBus(const char* route, const char* msg) {
  strncpy(busData.route, route, sizeof(busData.route) - 1);
  busData.route[sizeof(busData.route) - 1] = '\0';
  strncpy(busData.msg, msg, sizeof(busData.msg) - 1);
  busData.msg[sizeof(busData.msg) - 1] = '\0';
}

// ═════════════════════════════════════════════════════════════
// OLED 렌더링
// ═════════════════════════════════════════════════════════════
void drawScreen() {
  u8g2.clearBuffer();
  drawHeader();

  if (netState == NET_BOOT || netState == NET_WIFI) {
    drawNetMessage();
  } else if (!busData.valid) {
    drawLoading();
  } else {
    // 정상: 상태(안전/위험) + 상세
    drawStatus(busData.danger);
    drawDetail();
  }

  u8g2.sendBuffer();
}

// ─── 헤더 ───
void drawHeader() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);
  u8g2.drawStr(0, 9, "BUS");

  // 오른쪽: WiFi 상태
  char rssi[16];
  if (netState == NET_OFFLINE) {
    strcpy(rssi, "OFFLINE");
  } else if (netState != NET_OK) {
    strcpy(rssi, "...");
  } else {
    snprintf(rssi, sizeof(rssi), "%ddBm", WiFi.RSSI());
  }
  int w = u8g2.getStrWidth(rssi);
  u8g2.drawStr(128 - w, 9, rssi);

  u8g2.drawHLine(0, 11, 128);
}

// ─── 네트워크 진행 메시지 ───
void drawNetMessage() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);

  const char* m1 = "";
  const char* m2 = "";
  if      (netState == NET_BOOT) { m1 = "부팅 중";  m2 = "잠시만요"; }
  else if (netState == NET_WIFI) { m1 = "WiFi 연결"; m2 = WIFI_SSID; }
  drawCenteredTrimmed(m1, 35);
  drawCenteredTrimmed(m2, 55);
}

// ─── 첫 API 응답 전 ───
void drawLoading() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);
  drawCenteredTrimmed("로딩 중", 35);
  char arsInfo[24];
  snprintf(arsInfo, sizeof(arsInfo), "ARS %s", BUS_ARS_ID);
  drawCenteredTrimmed(arsInfo, 55);
}

// ─── 상태 영역 (정상 / 위험) ───
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

// ─── 상세 (정류장명 + 표시 버스) ───
void drawDetail() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);

  // 줄 1: 정류장명
  drawCenteredTrimmed(busData.station, 48);

  // 줄 2: "노선번호 + 도착메시지" (긴 거 trim됨)
  char line[64];
  snprintf(line, sizeof(line), "%s %s", busData.route, busData.msg);
  drawCenteredTrimmed(line, 62);
}

// ═════════════════════════════════════════════════════════════
// UTF-8 가운데정렬 + 자동 trim
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

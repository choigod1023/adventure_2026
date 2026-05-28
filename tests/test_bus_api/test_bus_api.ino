/*
 * test_bus_api.ino  -  Phase 1-2 검증 (JSON, 디버그 강화)
 * 서울특별시 정류소정보조회 (data.go.kr / 15000303)
 * 오퍼레이션: getStationByUid
 *
 * 변경 사항:
 *   - WiFi IP 받을 때까지 대기 (이전: 0.0.0.0로 출력되던 문제)
 *   - ArduinoJson Filter 제거 (응답 ~3KB라 SRAM 부담 없음)
 *   - 응답 raw 디버그 (앞부분 200바이트 출력)
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "config.h"

// ────────────────────────────────────────────────────────────
const char HOST[]     = "ws.bus.go.kr";
const int  PORT       = 80;
const char API_PATH[] = "/api/rest/stationinfo/getStationByUid";

const unsigned long INTERVAL_MS = 30000UL;
unsigned long lastFetch = 0;

WiFiClient sock;
HttpClient http(sock, HOST, PORT);

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println(F("\n=== [Phase 1-2] 정류소 버스 도착 API (JSON) ==="));
  Serial.print(F("정류장 ARS ID: ")); Serial.println(BUS_ARS_ID);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }

  if (lastFetch == 0 || millis() - lastFetch >= INTERVAL_MS) {
    lastFetch = millis();
    fetchStation();
  }
}

// ─────────────────────────────────────────────────────────────
// WiFi 연결 + 실제 IP 받을 때까지 대기
void connectWiFi() {
  Serial.print(F("WiFi 연결 중 "));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 1단계: WL_CONNECTED 까지
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nWiFi 연결 실패. 5초 후 재시도."));
    Serial.println(F("⚠️ UNO R4 WiFi는 2.4GHz만 지원! SSID가 5GHz면 연결 불가"));
    delay(5000);
    return;
  }

  // 2단계: DHCP로 실제 IP 받을 때까지 대기 (0.0.0.0 방지)
  Serial.print(F(" / IP 대기 "));
  for (int i = 0; i < 20; i++) {
    IPAddress ip = WiFi.localIP();
    if (ip[0] != 0) {
      Serial.print(F("\n연결 OK | IP: ")); Serial.println(ip);
      Serial.print(F("RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
      return;
    }
    delay(500); Serial.print('.');
  }
  Serial.println(F("\n⚠️ IP 할당 실패 (DHCP 문제)"));
}

// ─────────────────────────────────────────────────────────────
void fetchStation() {
  Serial.println(F("\n──────────────────────────────"));
  Serial.println(F("[정류소 버스 도착정보 요청]"));

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
  Serial.print(F("HTTP 상태: ")); Serial.println(statusCode);

  if (statusCode != 200) {
    Serial.println(F("[오류] 요청 실패"));
    http.stop();
    return;
  }

  // Content-Length 확인 (디버그)
  long contentLen = http.contentLength();
  Serial.print(F("Content-Length: ")); Serial.println(contentLen);

  // ── 응답 raw 디버그: 앞 200바이트 출력 후 다시 파싱 ──
  //    Stream의 peek는 1바이트만 가능하니, 일단 String으로 한 번에 받기
  String body = http.responseBody();
  http.stop();

  Serial.print(F("응답 길이: ")); Serial.println(body.length());
  Serial.println(F("--- 응답 앞부분 ---"));
  Serial.println(body.substring(0, min((int)body.length(), 250)));
  Serial.println(F("---"));

  if (body.length() == 0) {
    Serial.println(F("[오류] 응답 본문 비어있음"));
    return;
  }

  // ── JSON 파싱 (Filter 없이 풀 파싱, 3KB 정도라 OK) ──
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.print(F("[JSON 파싱 오류] ")); Serial.println(err.c_str());
    return;
  }

  // API 헤더 확인
  const char* headerCd  = doc["msgHeader"]["headerCd"]  | "";
  const char* headerMsg = doc["msgHeader"]["headerMsg"] | "";
  Serial.print(F("API headerCd: '")); Serial.print(headerCd);
  Serial.print(F("' / msg: ")); Serial.println(headerMsg);

  if (strlen(headerCd) == 0) {
    Serial.println(F("[경고] msgHeader.headerCd 못 찾음 - JSON 구조 확인 필요"));
    return;
  }
  if (strcmp(headerCd, "0") != 0) {
    Serial.println(F("[API 에러]"));
    return;
  }

  // ── itemList 순회 ──
  JsonVariant items = doc["msgBody"]["itemList"];
  if (items.isNull()) {
    Serial.println(F("[경고] itemList 없음"));
    return;
  }

  int count = 0;
  bool stationPrinted = false;

  if (items.is<JsonArray>()) {
    for (JsonObject item : items.as<JsonArray>()) {
      printBus(item, stationPrinted);
      count++;
    }
  } else if (items.is<JsonObject>()) {
    printBus(items.as<JsonObject>(), stationPrinted);
    count = 1;
  }

  if (count == 0) {
    Serial.println(F("[경고] 도착 정보 없음 (운행 시간대 확인)"));
  } else {
    Serial.print(F("총 ")); Serial.print(count); Serial.println(F("대"));
  }
}

// ─────────────────────────────────────────────────────────────
void printBus(JsonObject item, bool &stationPrinted) {
  if (!stationPrinted) {
    Serial.print(F("정류장: "));
    Serial.print((const char*)(item["stNm"]  | "?"));
    Serial.print(F("  (ARS "));
    Serial.print((const char*)(item["arsId"] | "?"));
    Serial.println(F(")"));
    Serial.println(F("──────────────────────────────"));
    stationPrinted = true;
  }

  const char* rtNm    = item["rtNm"]    | "?";
  const char* arrmsg1 = item["arrmsg1"] | "(없음)";
  const char* arrmsg2 = item["arrmsg2"] | "(없음)";

  Serial.print(F("  버스 ")); Serial.println(rtNm);
  Serial.print(F("    1번째: ")); Serial.println(arrmsg1);
  Serial.print(F("    2번째: ")); Serial.println(arrmsg2);

  if (strstr(arrmsg1, "곧 도착") ||
      (strncmp(arrmsg1, "출발", 4) == 0 && !strstr(arrmsg1, "출발대기"))) {
    Serial.println(F("    ⚠️  위험 트리거 조건"));
  }
  Serial.println();
}

/*
 * test_subway_api.ino  -  Phase 1-3 검증 (수정)
 * 서울시 지하철 실시간 도착정보 (data.seoul.go.kr / OA-12764)
 *
 * 변경 사항:
 *   - WiFi IP 받을 때까지 대기 (이전: 0.0.0.0로 출력되던 문제)
 *   - errorMessage 처리 버그 수정 (INFO-000은 정상)
 *   - 응답 raw 디버그 (앞부분 250바이트)
 *
 * 보드:  Arduino UNO R4 WiFi
 * 응답:  JSON (HTTP)
 * 라이브러리:
 *   - WiFiS3              (보드패키지)
 *   - ArduinoHttpClient   (라이브러리 매니저)
 *   - ArduinoJson v7      (Benoit Blanchon)
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "config.h"

// 한글 역명 UTF-8 URL 인코딩 → PROGMEM 상수
// "수유" = EC 88 98 / EC 9C A0  →  %EC%88%98%EC%9C%A0
const char STATION_ENC[] = "%EC%88%98%EC%9C%A0";  // 수유

// ────────────────────────────────────────────────────────────
const char HOST[] = "swopenAPI.seoul.go.kr";
const int  PORT   = 80;

const unsigned long INTERVAL_MS = 15000UL;
unsigned long lastFetch = 0;

WiFiClient  sock;
HttpClient  http(sock, HOST, PORT);

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println(F("\n=== [Phase 1-3] 지하철 도착 API 테스트 ==="));
  Serial.println(F("역명: 수유"));
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }

  if (lastFetch == 0 || millis() - lastFetch >= INTERVAL_MS) {
    lastFetch = millis();
    fetchSubway();
  }
}

// ─────────────────────────────────────────────────────────────
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

  // 2단계: DHCP로 실제 IP 받을 때까지 대기
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
void fetchSubway() {
  Serial.println(F("\n──────────────────────────────"));
  Serial.println(F("[지하철 API 요청]"));

  // 경로: /api/subway/{KEY}/json/realtimeStationArrival/{START}/{END}/{STATION}
  String path = String("/api/subway/") + SUBWAY_API_KEY
              + "/json/realtimeStationArrival/0/5/" + STATION_ENC;

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

  // ── 응답 raw 받기 (디버그 + 안정성) ──
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

  // ── JSON 파싱 ──
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.print(F("[JSON 파싱 오류] ")); Serial.println(err.c_str());
    return;
  }

  // ── errorMessage 코드 확인 ──
  // INFO-000 = 정상, INFO-200 = 데이터없음, ERROR-XXX = 에러
  const char* errCode = doc["errorMessage"]["code"] | "";
  const char* errMsg  = doc["errorMessage"]["message"] | "";
  Serial.print(F("API 코드: ")); Serial.print(errCode);
  Serial.print(F(" / ")); Serial.println(errMsg);

  if (strcmp(errCode, "INFO-000") != 0) {
    Serial.println(F("[API 비정상 응답]"));
    return;
  }

  // ── realtimeArrivalList 순회 ──
  JsonArray arr = doc["realtimeArrivalList"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println(F("[경고] realtimeArrivalList 비었음"));
    return;
  }

  Serial.print(F("도착 건수: ")); Serial.println(arr.size());
  Serial.println(F("──────────────────────────────"));

  int idx = 0;
  for (JsonObject item : arr) {
    Serial.print(F("[")); Serial.print(++idx); Serial.println(F("]"));
    Serial.print(F("  호선     : "));
    Serial.println((const char*)(item["subwayId"]    | "?"));
    Serial.print(F("  상하행   : "));
    Serial.println((const char*)(item["updnLine"]    | "?"));
    Serial.print(F("  방면     : "));
    Serial.println((const char*)(item["trainLineNm"] | "?"));
    Serial.print(F("  종착역   : "));
    Serial.println((const char*)(item["bstatnNm"]    | "?"));
    Serial.print(F("  도착메시지: "));
    Serial.println((const char*)(item["arvlMsg2"]    | "?"));
    Serial.print(F("  현재위치 : "));
    Serial.println((const char*)(item["arvlMsg3"]    | "?"));
    Serial.print(F("  도착예정 : "));
    Serial.print((int)(item["barvlDt"] | 0));
    Serial.println(F("초"));

    // 본 프로젝트 위험 판정 예시
    int sec = item["barvlDt"] | 9999;
    const char* msg = item["arvlMsg2"] | "";
    if (sec < 30 || strstr(msg, "곧 도착") || strstr(msg, "전역")) {
      Serial.println(F("  ⚠️  위험 트리거 조건"));
    }
    Serial.println();
  }
}

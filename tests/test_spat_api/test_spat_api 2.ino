/*
 * test_spat_api.ino  -  Phase 1-1 검증
 * 서울시 신호제어기 잔여시간 (C-ITS SPAT / data_id=10120)
 *
 * 보드:  Arduino UNO R4 WiFi
 * 응답:  JSON (HTTPS 443 필수, WiFiSSLClient)
 * 라이브러리:
 *   - WiFiS3              (보드패키지)
 *   - ArduinoHttpClient   (라이브러리 매니저)
 *   - ArduinoJson v7      (Benoit Blanchon)
 *
 * ⚠️ 사전 준비:
 *   1) t-data.seoul.go.kr 가입 + apikey 발급
 *   2) secrets.h 의 SPAT_API_KEY 채우기
 *   3) (선택) config.h 의 SPAT_ITST_ID에 교차로 ID 지정
 *
 * 메모리 주의:
 *   응답이 수십 KB → 반드시 pageNo=1&numOfRows=1 로 1건만 받기
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "config.h"

// ────────────────────────────────────────────────────────────
const char HOST[] = "t-data.seoul.go.kr";
const int  PORT   = 443;   // HTTPS
const char API_PATH[] =
  "/apig/apiman-gateway/tapi/v2xSignalPhaseTimingInformation/1.0";

const unsigned long INTERVAL_MS = 10000UL;
unsigned long lastFetch = 0;

WiFiSSLClient sslClient;
HttpClient    http(sslClient, HOST, PORT);

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println(F("\n=== [Phase 1-1] C-ITS SPAT API 테스트 ==="));
  Serial.print(F("교차로 ID: '"));
  Serial.print(SPAT_ITST_ID);
  Serial.println(F("'  (빈 값=전체)"));
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }

  if (lastFetch == 0 || millis() - lastFetch >= INTERVAL_MS) {
    lastFetch = millis();
    fetchSpat();
  }
}

// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.print(F("WiFi 연결 중 "));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nWiFi 연결 실패. 5초 후 재시도."));
    Serial.println(F("⚠️ UNO R4 WiFi는 2.4GHz만 지원!"));
    delay(5000);
    return;
  }

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
  Serial.println(F("\n⚠️ IP 할당 실패"));
}

// ─────────────────────────────────────────────────────────────
void fetchSpat() {
  Serial.println(F("\n──────────────────────────────"));
  Serial.println(F("[SPAT API 요청]"));

  // ⚠️ numOfRows=1 필수 — 응답 크기 제한 (SRAM 보호)
  String path = String(API_PATH)
              + "?apikey="     + SPAT_API_KEY  // 소문자 apikey
              + "&type=json"
              + "&pageNo=1"
              + "&numOfRows=1";

  if (strlen(SPAT_ITST_ID) > 0) {
    path += "&itstId=";
    path += SPAT_ITST_ID;
  }

  http.beginRequest();
  http.get(path);
  http.sendHeader(F("Accept"), F("application/json"));
  http.sendHeader(F("Connection"), F("close"));
  http.endRequest();

  int statusCode = http.responseStatusCode();
  Serial.print(F("HTTP 상태: ")); Serial.println(statusCode);

  if (statusCode != 200) {
    Serial.println(F("[오류] 요청 실패"));
    Serial.println(F("  → t-data 서버 다운 / apikey 확인 / 회원가입 상태 확인"));
    http.stop();
    return;
  }

  // ── 스트림 직접 파싱 (메모리 절약) ──
  //    raw 디버그도 필요하면 String 방식으로 바꿔도 됨
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http);
  http.stop();

  if (err) {
    Serial.print(F("[JSON 파싱 오류] ")); Serial.println(err.c_str());
    Serial.println(F("  → numOfRows 더 줄이기 / 응답 형식 확인"));
    return;
  }

  // ── 최상위가 JSON 배열인지 확인 ──
  if (!doc.is<JsonArray>()) {
    Serial.println(F("[오류] 응답이 배열 아님"));
    return;
  }

  int count = 0;
  for (JsonObject item : doc.as<JsonArray>()) {
    printItem(item);
    count++;
  }

  if (count == 0) {
    Serial.println(F("[경고] 데이터 없음"));
  } else {
    Serial.print(F("총 ")); Serial.print(count); Serial.println(F("건"));
  }
}

// ─────────────────────────────────────────────────────────────
// 잔여시간 출력 헬퍼
// 단위: 센티초(1/10초) → ÷10 = 초 / null = 해당없음 / 36001 = 비활성
void printVal(const char* label, JsonVariant v) {
  Serial.print(F("  ")); Serial.print(label); Serial.print(F(": "));
  if (v.isNull())                  { Serial.println(F("--"));         return; }
  float cs = v.as<float>();
  if (cs >= SPAT_SENTINEL)         { Serial.println(F("[비활성/점멸]")); return; }
  Serial.print(cs / 10.0f, 1); Serial.println(F("초"));
}

// ─────────────────────────────────────────────────────────────
void printItem(JsonObject o) {
  Serial.println(F("┌─────────────────────────────────────"));
  Serial.print(F("│ 교차로ID : ")); Serial.println((const char*)(o["itstId"] | "N/A"));
  Serial.print(F("│ 장비ID   : ")); Serial.println((const char*)(o["eqmnId"] | "N/A"));

  Serial.print(F("│ 전송시각 : "));
  Serial.print((const char*)(o["trsmYear"] | "????")); Serial.print('-');
  Serial.print((const char*)(o["trsmMt"]   | "??"));   Serial.print('-');
  Serial.print((int)(o["trsmDy"] | 0));                Serial.print(' ');
  Serial.println((const char*)(o["trsmTm"] | "??????"));

  Serial.println(F("│"));
  Serial.println(F("│  방향  신호     잔여시간"));

  // 4방위 × 주요 신호
  printVal("북 직진 ", o["ntStsgRmdrCs"]);
  printVal("북 좌회전", o["ntLtsgRmdrCs"]);
  printVal("북 보행 ", o["ntPdsgRmdrCs"]);
  printVal("동 직진 ", o["etStsgRmdrCs"]);
  printVal("동 좌회전", o["etLtsgRmdrCs"]);
  printVal("동 보행 ", o["etPdsgRmdrCs"]);
  printVal("남 직진 ", o["stStsgRmdrCs"]);
  printVal("남 좌회전", o["stLtsgRmdrCs"]);
  printVal("남 보행 ", o["stPdsgRmdrCs"]);
  printVal("서 직진 ", o["wtStsgRmdrCs"]);
  printVal("서 좌회전", o["wtLtsgRmdrCs"]);
  printVal("서 보행 ", o["wtPdsgRmdrCs"]);

  // 대각 (데이터 있을 때만)
  if (!o["neStsgRmdrCs"].isNull()) printVal("북동 직진", o["neStsgRmdrCs"]);
  if (!o["seStsgRmdrCs"].isNull()) printVal("남동 직진", o["seStsgRmdrCs"]);
  if (!o["swStsgRmdrCs"].isNull()) printVal("남서 직진", o["swStsgRmdrCs"]);
  if (!o["nwStsgRmdrCs"].isNull()) printVal("북서 직진", o["nwStsgRmdrCs"]);

  // 본 프로젝트 위험 판정 예시 — 보행자 신호 잔여 3초 미만
  Serial.println(F("│"));
  checkPedDanger(o, "북", "ntPdsgRmdrCs");
  checkPedDanger(o, "동", "etPdsgRmdrCs");
  checkPedDanger(o, "남", "stPdsgRmdrCs");
  checkPedDanger(o, "서", "wtPdsgRmdrCs");

  Serial.println(F("└─────────────────────────────────────"));
}

void checkPedDanger(JsonObject o, const char* dir, const char* key) {
  JsonVariant v = o[key];
  if (v.isNull()) return;
  float cs = v.as<float>();
  if (cs >= SPAT_SENTINEL) return;
  float sec = cs / 10.0f;
  if (sec < WARN_PEDESTRIAN_SEC) {
    Serial.print(F("│  ⚠️  ")); Serial.print(dir);
    Serial.print(F(" 보행 잔여 ")); Serial.print(sec, 1);
    Serial.println(F("초 - 위험 트리거"));
  }
}

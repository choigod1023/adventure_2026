/*
 * apis.ino  -  WiFi + 3개 외부 API
 *
 * 함수:
 *   connectWiFi()      : 부팅/재연결 (블로킹)
 *   manageWiFi()       : loop마다 WiFi 끊김 감지 + 자동 ④ 폴백
 *   evaluateBus()      : MODE_BUS  - 정류소 도착정보 호출 + 위험 판정
 *   evaluateSubway()   : MODE_SUBWAY - 지하철 도착정보 호출 + 위험 판정
 *   evaluateSpat()     : MODE_SPAT - C-ITS 신호 잔여시간 호출 + 위험 판정
 *
 * 모든 함수는 millis() 기반 인터벌로 동작 (현재 모드일 때만 호출됨).
 * 표시 데이터는 전역 displayData 에 채워둠 → ui.ino가 읽음.
 */

// Forward declarations (사용자 정의/라이브러리 타입 인자 함수 — Arduino IDE 자동 prototype 회피용)
void pickBus(JsonObject item, bool &urgentFound, bool &firstStored,
             char* station, char* route, char* arrmsg);
void setDisplayValid(const char* line1, const char* line2, bool danger);
void setDisplayError(const char* line1, const char* line2);
void fetchBus();
void fetchSubway();
void fetchSpat();

// ════════════════════════════════════════════════════════════════
//  WiFi
// ════════════════════════════════════════════════════════════════
void connectWiFi() {
  netState = NET_WIFI;
  drawScreen();

  Serial.print(F("WiFi 연결 중 "));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n[WiFi 실패] UNO R4 WiFi는 2.4GHz만 지원"));
    netState = NET_OFFLINE;
    drawScreen();
    return;
  }

  // DHCP IP 대기 (0.0.0.0 방지)
  for (int i = 0; i < 20; i++) {
    if (WiFi.localIP()[0] != 0) break;
    delay(500); Serial.print('.');
  }
  netState = NET_OK;
  Serial.print(F("\n연결 OK | IP: ")); Serial.print(WiFi.localIP());
  Serial.print(F(" | RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
}

// 끊김 감지 + API 모드면 ④ 강제 전환
void manageWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (netState != NET_OK) {
      netState = NET_OK;
      Serial.println(F("[WiFi] 복구"));
    }
    return;
  }

  // 끊김
  if (netState != NET_OFFLINE) {
    netState = NET_OFFLINE;
    Serial.println(F("[WiFi] 연결 끊김"));

    // 사용자가 API 모드에 있었다면 → ④ 센서 모드 자동 폴백
    if (currentMode != MODE_SENSOR) {
      Serial.println(F("[자동 폴백] → SENSOR 모드"));
      setMode(MODE_SENSOR);
    }
  }
  // 재연결 시도는 사용자가 API 모드 버튼 다시 누를 때만
  // (loop 막힘 방지)
}

// ════════════════════════════════════════════════════════════════
//  ① 버스 도착 API
// ════════════════════════════════════════════════════════════════
bool evaluateBus() {
  // 모드 진입 직후 즉시 fetch, 이후 INTERVAL 마다
  bool dueFetch = modeJustChanged ||
                  (lastFetchBus == 0) ||
                  (millis() - lastFetchBus >= POLL_INTERVAL_BUS_MS);

  if (dueFetch && netState == NET_OK) {
    fetchBus();
    lastFetchBus = millis();
    modeJustChanged = false;
  }
  return displayData.valid && displayData.danger;
}

void fetchBus() {
  Serial.println(F("\n[BUS API]"));

  HttpClient http(plainSock, "ws.bus.go.kr", 80);
  String path = String("/api/rest/stationinfo/getStationByUid")
              + "?serviceKey=" + BUS_API_KEY
              + "&arsId="      + BUS_ARS_ID
              + "&resultType=json";

  http.beginRequest();
  http.get(path);
  http.sendHeader(F("Accept"), F("application/json"));
  http.sendHeader(F("Connection"), F("close"));
  http.endRequest();

  int statusCode = http.responseStatusCode();
  if (statusCode != 200) {
    Serial.print(F("HTTP "));
    Serial.println(statusCode);
    http.stop();
    setDisplayError("BUS API", "통신 실패");
    return;
  }

  String body = http.responseBody();
  http.stop();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    setDisplayError("BUS", "JSON 오류");
    return;
  }

  const char* hdrCd = doc["msgHeader"]["headerCd"] | "";
  if (strcmp(hdrCd, "0") != 0) {
    setDisplayError("BUS API", "응답 오류");
    return;
  }

  JsonVariant items = doc["msgBody"]["itemList"];
  if (items.isNull()) {
    setDisplayValid("정보 없음", "ARS " BUS_ARS_ID, false);
    return;
  }

  // 위험 버스 우선, 없으면 첫 번째 버스 표시
  bool urgentFound = false;
  bool firstStored = false;
  char station[32] = "";
  char route[16]   = "?";
  char arrmsg[40]  = "정보 없음";

  if (items.is<JsonArray>()) {
    for (JsonObject item : items.as<JsonArray>()) {
      pickBus(item, urgentFound, firstStored, station, route, arrmsg);
      if (urgentFound) break;
    }
  } else if (items.is<JsonObject>()) {
    pickBus(items.as<JsonObject>(), urgentFound, firstStored, station, route, arrmsg);
  }

  char detail[48];
  snprintf(detail, sizeof(detail), "%s %s", route, arrmsg);
  setDisplayValid(station[0] ? station : "정류장", detail, urgentFound);
}

// 한 itemList 처리: 위험 버스가 나오면 그 정보로 저장
void pickBus(JsonObject item, bool &urgentFound, bool &firstStored,
             char* station, char* route, char* arrmsg) {
  const char* stNm    = item["stNm"]    | "";
  const char* rtNm    = item["rtNm"]    | "?";
  const char* msg     = item["arrmsg1"] | "정보 없음";

  if (station[0] == '\0') {
    strncpy(station, stNm, 31); station[31] = '\0';
  }

  bool urgent = strstr(msg, "곧 도착") ||
                (strncmp(msg, "출발", 4) == 0 && !strstr(msg, "출발대기"));

  if (urgent && !urgentFound) {
    urgentFound = true;
    strncpy(route, rtNm, 15);  route[15] = '\0';
    strncpy(arrmsg, msg, 39);  arrmsg[39] = '\0';
    return;
  }
  if (!urgentFound && !firstStored) {
    firstStored = true;
    strncpy(route, rtNm, 15);  route[15] = '\0';
    strncpy(arrmsg, msg, 39);  arrmsg[39] = '\0';
  }
}

// ════════════════════════════════════════════════════════════════
//  ② 지하철 도착 API
// ════════════════════════════════════════════════════════════════
bool evaluateSubway() {
  bool dueFetch = modeJustChanged ||
                  (lastFetchSubway == 0) ||
                  (millis() - lastFetchSubway >= POLL_INTERVAL_SUBWAY_MS);

  if (dueFetch && netState == NET_OK) {
    fetchSubway();
    lastFetchSubway = millis();
    modeJustChanged = false;
  }
  return displayData.valid && displayData.danger;
}

void fetchSubway() {
  Serial.println(F("\n[SUBWAY API]"));

  HttpClient http(plainSock, "swopenAPI.seoul.go.kr", 80);
  String path = String("/api/subway/") + SUBWAY_API_KEY
              + "/json/realtimeStationArrival/0/5/" + SUBWAY_STATION_ENC;

  http.beginRequest();
  http.get(path);
  http.sendHeader(F("Accept"), F("application/json"));
  http.sendHeader(F("Connection"), F("close"));
  http.endRequest();

  int statusCode = http.responseStatusCode();
  if (statusCode != 200) {
    Serial.print(F("HTTP "));
    Serial.println(statusCode);
    http.stop();
    setDisplayError("SUBWAY API", "통신 실패");
    return;
  }

  String body = http.responseBody();
  http.stop();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    setDisplayError("SUBWAY", "JSON 오류");
    return;
  }

  const char* errCode = doc["errorMessage"]["code"] | "";
  if (strcmp(errCode, "INFO-000") != 0) {
    setDisplayError("SUBWAY API", errCode);
    return;
  }

  JsonArray arr = doc["realtimeArrivalList"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    setDisplayValid(SUBWAY_STATION "역", "운행 정보 없음", false);
    return;
  }

  // 가장 빨리 도착하는 차량 1개 선정 (또는 위험 트리거)
  bool urgent = false;
  const char* line = "?";
  const char* msg  = "정보 없음";
  int minSec = 9999;
  const char* pickedMsg = NULL;
  const char* pickedLine = NULL;

  for (JsonObject item : arr) {
    const char* m   = item["arvlMsg2"]    | "";
    const char* sid = item["subwayId"]    | "?";
    int sec         = item["barvlDt"]     | 9999;

    bool nowUrgent = (sec < 30) || strstr(m, "곧 도착") || strstr(m, "전역");
    if (nowUrgent && !urgent) {
      urgent = true;
      pickedMsg  = m;
      pickedLine = sid;
      break;
    }
    if (sec > 0 && sec < minSec) {
      minSec = sec;
      pickedMsg  = m;
      pickedLine = sid;
    }
  }

  if (pickedLine) line = pickedLine;
  if (pickedMsg)  msg  = pickedMsg;

  char detail[48];
  snprintf(detail, sizeof(detail), "%s %s", line, msg);
  setDisplayValid(SUBWAY_STATION "역", detail, urgent);
}

// ════════════════════════════════════════════════════════════════
//  ③ C-ITS 신호 잔여시간 API (HTTPS)
// ════════════════════════════════════════════════════════════════
bool evaluateSpat() {
  bool dueFetch = modeJustChanged ||
                  (lastFetchSpat == 0) ||
                  (millis() - lastFetchSpat >= POLL_INTERVAL_SPAT_MS);

  if (dueFetch && netState == NET_OK) {
    fetchSpat();
    lastFetchSpat = millis();
    modeJustChanged = false;
  }
  return displayData.valid && displayData.danger;
}

void fetchSpat() {
  Serial.println(F("\n[SPAT API]"));

  HttpClient http(sslSock, "t-data.seoul.go.kr", 443);

  // numOfRows=1 필수 (SRAM 보호, 응답 ~수십 KB 가능)
  String path = String("/apig/apiman-gateway/tapi/v2xSignalPhaseTimingInformation/1.0")
              + "?apikey="     + SPAT_API_KEY
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
  if (statusCode != 200) {
    Serial.print(F("HTTP "));
    Serial.println(statusCode);
    http.stop();
    setDisplayError("CITS API", "통신 실패");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http);
  http.stop();

  if (err) {
    setDisplayError("CITS", "JSON 오류");
    return;
  }
  if (!doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) {
    setDisplayError("CITS API", "데이터 없음");
    return;
  }

  JsonObject item = doc.as<JsonArray>()[0];
  const char* itstId = item["itstId"] | "?";

  // 4방위 보행자 신호 잔여 중 최소값을 위험 판정에 사용
  float minPed = SPAT_SENTINEL;
  const char* minDir = "?";

  struct { const char* dir; const char* key; } dirs[] = {
    { "북", "ntPdsgRmdrCs" },
    { "동", "etPdsgRmdrCs" },
    { "남", "stPdsgRmdrCs" },
    { "서", "wtPdsgRmdrCs" },
  };
  for (int i = 0; i < 4; i++) {
    JsonVariant v = item[dirs[i].key];
    if (v.isNull()) continue;
    float cs = v.as<float>();
    if (cs >= SPAT_SENTINEL) continue;
    if (cs < minPed) { minPed = cs; minDir = dirs[i].dir; }
  }

  char context[40];
  snprintf(context, sizeof(context), "%s 교차로", itstId);

  if (minPed >= SPAT_SENTINEL) {
    setDisplayValid(context, "보행 신호 없음", false);
    return;
  }

  float sec = minPed / 10.0f;
  char detail[40];
  snprintf(detail, sizeof(detail), "%s 보행 %d.%ds",
           minDir, (int)sec, (int)(sec * 10) % 10);
  bool danger = (sec < WARN_PEDESTRIAN_SEC);
  setDisplayValid(context, detail, danger);
}

// ════════════════════════════════════════════════════════════════
//  표시 데이터 설정 헬퍼
// ════════════════════════════════════════════════════════════════
void setDisplayValid(const char* line1, const char* line2, bool danger) {
  strncpy(displayData.line1, line1, sizeof(displayData.line1) - 1);
  displayData.line1[sizeof(displayData.line1) - 1] = '\0';
  strncpy(displayData.line2, line2, sizeof(displayData.line2) - 1);
  displayData.line2[sizeof(displayData.line2) - 1] = '\0';
  displayData.danger = danger;
  displayData.valid = true;
  displayData.updatedAt = millis();
}

void setDisplayError(const char* line1, const char* line2) {
  setDisplayValid(line1, line2, false);
}

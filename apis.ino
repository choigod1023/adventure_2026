/*
 * apis.ino  -  WiFi + 3개 외부 API
 *
 *   connectWiFi()      : 부팅/재연결 (블로킹)
 *   manageWiFi()       : loop마다 WiFi 끊김 감지 + 자동 SENSOR 폴백
 *   evaluateBus()      : ①BUS    호출 + 위험 판정
 *   evaluateSubway()   : ②SUBWAY 호출 + 위험 판정
 *   evaluateSpat()     : ③CITS   호출 + 위험 판정
 *
 * 표시 데이터는 전역 displayData 에 채워둠 → ui.ino 가 읽음.
 */

// Forward declarations
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

  // DHCP IP 대기
  for (int i = 0; i < 20; i++) {
    if (WiFi.localIP()[0] != 0) break;
    delay(500); Serial.print('.');
  }
  netState = NET_OK;
  Serial.print(F("\n연결 OK | IP: ")); Serial.print(WiFi.localIP());
  Serial.print(F(" | RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
}

// 끊김 감지. API 모드인데 끊겼으면 스위치는 그대로지만 OLED에 OFFLINE 표시.
// (스위치가 SENSOR가 아닌 한 사용자에게 모드를 강제 바꾸진 않음 — 스위치 상태 = 사용자 의도)
void manageWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (netState != NET_OK) {
      netState = NET_OK;
      Serial.println(F("[WiFi] 복구"));
    }
    return;
  }
  if (netState != NET_OFFLINE) {
    netState = NET_OFFLINE;
    Serial.println(F("[WiFi] 연결 끊김"));
  }
}

// ════════════════════════════════════════════════════════════════
//  ① 버스 도착 API
// ════════════════════════════════════════════════════════════════
bool evaluateBus() {
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
    Serial.print(F("HTTP ")); Serial.println(statusCode);
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
    Serial.print(F("HTTP ")); Serial.println(statusCode);
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

  bool urgent = false;
  const char* pickedMsg = NULL;
  const char* pickedLine = NULL;
  int minSec = 9999;

  for (JsonObject item : arr) {
    const char* m   = item["arvlMsg2"] | "";
    const char* sid = item["subwayId"] | "?";
    int sec         = item["barvlDt"]  | 9999;

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

  char detail[48];
  snprintf(detail, sizeof(detail), "%s %s",
           pickedLine ? pickedLine : "?",
           pickedMsg  ? pickedMsg  : "정보 없음");
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

  // numOfRows=1 필수 — SRAM 보호 (응답이 수십 KB 가능)
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
    Serial.print(F("HTTP ")); Serial.println(statusCode);
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
  const char* itstId = item["itstId"] | "?";   // 문자열 (예: "2691")

  // ── 4방위 보행자 신호(Pdsg) 잔여 중 최소값 사용 ──
  //  값 단위: 센티초(1/10초). null = 해당 방위에 보행 신호 없음.
  //  36001(SPAT_SENTINEL) = 신호 미운영/점멸.
  //
  //  ⚠️ 한계: 이 엔드포인트는 "현재 페이즈의 잔여시간"만 주고,
  //     그 페이즈가 보행 GREEN 인지 RED 인지 알려주는 필드가 없음.
  //     → 잔여 < 임계값을 "곧 신호 바뀜 = 위험"으로 해석 (보수적 경고).
  //     실배포 시 해당 교차로의 보행 페이즈 길이로 임계값 튜닝 권장.
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
    Serial.print(F("  ")); Serial.print(dirs[i].dir); Serial.print(F(" 보행: "));
    if (v.isNull()) { Serial.println(F("--")); continue; }
    float cs = v.as<float>();
    if (cs >= SPAT_SENTINEL) { Serial.println(F("[미운영]")); continue; }
    Serial.print(cs / 10.0f, 1); Serial.println(F("s"));
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
  Serial.print(F("  → 최소 ")); Serial.print(minDir);
  Serial.print(F(" ")); Serial.print(sec, 1);
  Serial.println(danger ? F("s [위험]") : F("s [정상]"));
  setDisplayValid(context, detail, danger);
}

// ════════════════════════════════════════════════════════════════
//  표시 데이터 헬퍼
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

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

    // 위험: 다음 역(=이 역)까지 30초 미만, "곧 도착", "당역 도착/진입".
    // ⚠️ "전역(前驛)" = 앞 역 (1~3분 거리) → 위험 아님. 절대 매칭하지 말 것.
    bool nowUrgent = (sec < 30) || strstr(m, "곧 도착") || strstr(m, "당역");
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
  // 로컬 카운트다운 0 도달 시 phase 토글 (다음 폴링 전에 GREEN↔RED 반영).
  spatTickLocal();

  bool dueFetch = modeJustChanged ||
                  (lastFetchSpat == 0) ||
                  (millis() - lastFetchSpat >= POLL_INTERVAL_SPAT_MS);

  if (dueFetch && netState == NET_OK) {
    fetchSpat();
    lastFetchSpat = millis();
    modeJustChanged = false;
  }
  // 스몸비 경고 모델: 사용자가 *실제로 횡단보도를 건너고 있는* 순간에 시선을 들게 한다.
  //   - 보행 GREEN → 위험 (지금 건너는 중 → 고개 들어!)
  //   - 보행 RED   → 정상 (대기 중, 즉각 위험 아님)
  //   - 미운영      → 정상
  if (!displayData.valid) return false;
  return spatState.phase == SPAT_PHASE_PED_GREEN;
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

  // ⚠️ 응답을 String 으로 통째로 받으면 안 됨 (메모리 고갈 → 보드 행 → 리셋).
  //    헤더를 건너뛴 뒤, '필터' 로 우리가 쓰는 필드만 추출하며 스트림 파싱한다.
  //    → 서버가 큰 응답을 줘도 메모리 사용량이 일정하게 유지됨.
  http.skipResponseHeaders();   // 본문 시작 위치로 이동 (이게 없으면 JSON 오류)

  // 페이즈 판정용으로 보행+차량 두 필드 모두 살림. 나머진 흘려보냄.
  JsonDocument filter;
  filter[0][SPAT_PED_FIELD] = true;
  filter[0][SPAT_CAR_FIELD] = true;

  JsonDocument doc;
  DeserializationError err =
    deserializeJson(doc, http, DeserializationOption::Filter(filter));
  http.stop();

  if (err) {
    Serial.print(F("[JSON 파싱 오류] ")); Serial.println(err.c_str());
    setDisplayError("CITS", "JSON 오류");
    return;
  }
  if (!doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) {
    setDisplayError("CITS API", "데이터 없음");
    return;
  }

  // ── 페이즈 결정 ──
  //  값 단위: 센티초(1/10초). null/36001 = 해당 페이즈 비활성.
  //   PED 필드 유효 → 보행 GREEN, 잔여시간 줄어드는 중
  //   CAR 필드 유효 (PED 는 비활성) → 보행 RED (차량 GREEN 활성)
  //   둘 다 비활성 → 미운영
  JsonObject item = doc.as<JsonArray>()[0];
  JsonVariant ped = item[SPAT_PED_FIELD];
  JsonVariant car = item[SPAT_CAR_FIELD];

  // 🔎 진단 로그: 실제 신호등 색을 보며 두 raw 값과 대조하면 API 의미가 드러남.
  //   해석 단서: "ped=null/36001 그리고 car=값" 일 때 실제로 빨강? 초록?
  Serial.print(F("  [raw] "));
  Serial.print(SPAT_PED_FIELD); Serial.print(F("="));
  if (ped.isNull()) Serial.print(F("null")); else Serial.print(ped.as<float>(), 0);
  Serial.print(F(" "));
  Serial.print(SPAT_CAR_FIELD); Serial.print(F("="));
  if (car.isNull()) Serial.print(F("null")); else Serial.print(car.as<float>(), 0);
  Serial.println();

  bool pedHas = !ped.isNull() && ped.as<float>() < SPAT_SENTINEL;
  bool carHas = !car.isNull() && car.as<float>() < SPAT_SENTINEL;
  float pedSec = pedHas ? ped.as<float>() / 10.0f : 0.0f;
  float carSec = carHas ? car.as<float>() / 10.0f : 0.0f;

  // 활성 phase = RmdrCs 가 더 짧은 쪽 (그 phase 가 곧 종료될 거니까 = 지금 활성).
  // 둘 다 값 있을 때만 비교 가능. 한 쪽만 값 있으면 그쪽이 활성.
  bool pedGreen;  // 보행 GREEN 활성 여부
  float liveSec;  // 화면/위험 판정에 쓸 잔여시간
  if (pedHas && carHas) {
    pedGreen = (pedSec < carSec);
    liveSec  = pedGreen ? pedSec : carSec;
  } else if (pedHas) {
    pedGreen = true;
    liveSec  = pedSec;
  } else if (carHas) {
    pedGreen = false;
    liveSec  = carSec;
  } else {
    spatState.phase = SPAT_PHASE_NONE;
    Serial.println(F("  미운영"));
    setDisplayValid(SPAT_ITST_NAME, "신호 없음", false);
    return;
  }

  spatState.phase = pedGreen ? SPAT_PHASE_PED_GREEN : SPAT_PHASE_PED_RED;
  spatState.secAtSnapshot = liveSec;
  spatState.snapshotMs = millis();

  Serial.print(pedGreen ? F("  보행 GREEN ") : F("  보행 RED   "));
  Serial.print(liveSec, 1);
  Serial.println(pedGreen ? F("s [위험]") : F("s [정상]"));
  setDisplayValid(SPAT_ITST_NAME, pedGreen ? "녹색" : "적색", pedGreen);
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


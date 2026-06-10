/*
 * ui.ino  -  OLED 화면 렌더링 (SSD1306 128×64)
 *
 * drawScreen()           : 메인 진입점. netState/현재 모드/위험 상태에 따라 분기
 * drawCenteredTrimmed()  : UTF-8 가운데 정렬 + 자동 ... trim (한글 안전)
 *
 * 헤더 형식 (왼쪽 = 현재 모드, 오른쪽 = 상태):
 *   API: "BUS  -55dBm" / "SUBWAY -55dBm" / "CITS  -55dBm"  /  OFFLINE
 *   SEN: "SENSOR PIR1 RAD0"
 */

// Forward declarations
void drawHeader();
void drawStatusBand();
void drawDetail();
void drawCenteredTrimmed(const char* text, int y);
const char* modeContextHint();

void drawScreen() {
  u8g2.clearBuffer();
  drawHeader();

  if (netState == NET_BOOT) {
    drawCenteredTrimmed("부팅 중", 35);
    drawCenteredTrimmed("잠시만요", 55);
  } else if (netState == NET_WIFI) {
    drawCenteredTrimmed("WiFi 연결", 35);
    drawCenteredTrimmed(WIFI_SSID, 55);
  } else if (!sensorMode && netState == NET_OFFLINE) {
    drawCenteredTrimmed("OFFLINE", 35);
    drawCenteredTrimmed("스위치 ↓ = SENSOR", 55);
  } else if (!displayData.valid) {
    drawCenteredTrimmed("로딩 중", 35);
    drawCenteredTrimmed(modeContextHint(), 55);
  } else {
    drawStatusBand();
    drawDetail();
  }

  u8g2.sendBuffer();
}

// ─── 헤더 (y 0~11) ───
void drawHeader() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);

  // 왼쪽: 현재 모드명
  u8g2.drawStr(0, 9, currentModeLabel());

  // 오른쪽: 상태
  char info[20];
  if (sensorMode) {
#if HAS_RCWL
    snprintf(info, sizeof(info), "PIR%d RAD%d",
             sensors.pir_now ? 1 : 0,
             sensors.radar_now ? 1 : 0);
#else
    snprintf(info, sizeof(info), "PIR%d RAD-",
             sensors.pir_now ? 1 : 0);
#endif
  } else if (netState == NET_OFFLINE) {
    strcpy(info, "OFFLINE");
  } else if (netState == NET_OK) {
    snprintf(info, sizeof(info), "%ddBm", WiFi.RSSI());
  } else {
    strcpy(info, "...");
  }
  int w = u8g2.getStrWidth(info);
  u8g2.drawStr(OLED_WIDTH - w, 9, info);

  u8g2.drawHLine(0, 11, OLED_WIDTH);
}

// ─── 위험/정상 띠 (y 12~33) ───
void drawStatusBand() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);

  if (dangerActive) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 12, OLED_WIDTH, 22);
    u8g2.setDrawColor(0);
    drawCenteredTrimmed("!! 위험 !!", 29);
    u8g2.setDrawColor(1);
  } else {
    drawCenteredTrimmed("정상", 29);
  }
  u8g2.drawHLine(0, 34, OLED_WIDTH);
}

// ─── 상세 2줄 (y 35~63) ───
void drawDetail() {
  u8g2.setFont(u8g2_font_unifont_t_korean1);
  u8g2.setDrawColor(1);
  drawCenteredTrimmed(displayData.line1, 48);

  // CITS 는 로컬 보간된 잔여시간으로 line2 를 매 프레임 다시 그림 (5초 폴링 사이 매끄러운 카운트다운).
  // 라벨은 unifont_t_korean1 화면 폭(128px)에 항상 들어가는 "녹색"/"적색" 사용.
  //   "녹색 12.3s" = 16+16+8+8*5 = 80px (99.9s 까지 여유)
  if (!sensorMode && currentApiMode == API_SPAT &&
      spatState.phase != SPAT_PHASE_NONE) {
    float sec = spatLiveSec();
    char line2[24];
    snprintf(line2, sizeof(line2), "%s %d.%ds",
             spatState.phase == SPAT_PHASE_PED_GREEN ? "녹색" : "적색",
             (int)sec, (int)(sec * 10) % 10);
    drawCenteredTrimmed(line2, 62);
  } else {
    drawCenteredTrimmed(displayData.line2, 62);
  }
}

// 로딩 중 표시 — 현재 모드의 컨텍스트
const char* modeContextHint() {
  if (sensorMode) return "센서 시작";
  switch (currentApiMode) {
    case API_BUS:    return "ARS " BUS_ARS_ID;
    case API_SUBWAY: return SUBWAY_STATION "역";
    case API_SPAT:   return strlen(SPAT_ITST_ID) > 0 ? SPAT_ITST_ID : "교차로";
    default:         return "";
  }
}

// ════════════════════════════════════════════════════════════════
//  한글 가운데 정렬 — 글자마다 글리프 있는 폰트로 (korean1 우선, 없으면 korean2)
//    · u8g2 의 korean1 폰트는 일부 음절(충/실/메/테…)을 빠뜨려 그 글자가
//      안 그려짐(= "충무로역"→"무로역" 처럼 잘려 보임). korean1∪korean2 로 전부 커버.
//    · ASCII/기호는 korean1(unifont)에 포함됨.
// ════════════════════════════════════════════════════════════════
static const uint8_t* const KFONT1 = u8g2_font_unifont_t_korean1;
static const uint8_t* const KFONT2 = u8g2_font_unifont_t_korean2;

// UTF-8 한 글자 디코드(1~3바이트), 포인터를 다음 글자로 전진
static uint16_t utf8Next(const char** pp) {
  const uint8_t* p = (const uint8_t*)(*pp);
  uint8_t c = *p++;
  uint16_t cp;
  if (c < 0x80) {
    cp = c;
  } else if ((c & 0xE0) == 0xC0) {
    cp = (uint16_t)(c & 0x1F) << 6;
    cp |= (*p++ & 0x3F);
  } else if ((c & 0xF0) == 0xE0) {
    cp = (uint16_t)(c & 0x0F) << 12;
    cp |= (uint16_t)(*p++ & 0x3F) << 6;
    cp |= (*p++ & 0x3F);
  } else {              // 4바이트(이모지 등) 미지원 → '?' 로
    cp = '?';
    p += 3;
  }
  *pp = (const char*)p;
  return cp;
}

// 글자에 글리프가 있는 폰트 선택 (korean1 우선)
static const uint8_t* fontForCp(uint16_t cp) {
  u8g2.setFont(KFONT1);
  if (u8g2_IsGlyph(u8g2.getU8g2(), cp)) return KFONT1;
  return KFONT2;
}

void drawCenteredTrimmed(const char* text, int y) {
  // 1) 글자별 폰트로 전체 폭 측정
  int total = 0;
  for (const char* p = text; *p;) {
    uint16_t cp = utf8Next(&p);
    u8g2.setFont(fontForCp(cp));
    total += u8g2_GetGlyphWidth(u8g2.getU8g2(), cp);
  }
  int x = (OLED_WIDTH - total) / 2;
  if (x < 0) x = 0;            // 너무 길면 왼쪽 정렬(가장자리 클립)

  // 2) 글자별 폰트로 그리기
  for (const char* p = text; *p;) {
    uint16_t cp = utf8Next(&p);
    u8g2.setFont(fontForCp(cp));
    x += u8g2.drawGlyph(x, y, cp);
  }
}


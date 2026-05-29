/*
 * ui.ino  -  OLED 화면 렌더링 (SSD1306 128×64)
 *
 * drawScreen()           : 메인 진입점. netState/현재 모드/위험 상태에 따라 분기
 * drawCenteredTrimmed()  : UTF-8 가운데 정렬 + 자동 ... trim (한글 안전)
 *
 * 폰트:
 *   u8g2_font_6x10_tf            : 헤더용 ASCII
 *   u8g2_font_unifont_t_korean1  : 본문 한글 (16px)
 */

// Forward declarations
void drawHeader();
void drawStatusBand();
void drawDetail();
void drawCenteredTrimmed(const char* text, int y);
const char* modeContextHint(Mode m);

void drawScreen() {
  u8g2.clearBuffer();
  drawHeader();

  if (netState == NET_BOOT) {
    drawCenteredTrimmed("부팅 중", 35);
    drawCenteredTrimmed("잠시만요", 55);
  } else if (netState == NET_WIFI) {
    drawCenteredTrimmed("WiFi 연결", 35);
    drawCenteredTrimmed(WIFI_SSID, 55);
  } else if (currentMode != MODE_SENSOR && netState == NET_OFFLINE) {
    // API 모드인데 오프라인 — manageWiFi가 ④로 폴백시키므로 거의 안 보이지만
    drawCenteredTrimmed("OFFLINE", 35);
    drawCenteredTrimmed("센서 모드 전환", 55);
  } else if (!displayData.valid) {
    drawCenteredTrimmed("로딩 중", 35);
    drawCenteredTrimmed(modeContextHint(currentMode), 55);
  } else {
    drawStatusBand();      // 정상/위험 띠
    drawDetail();          // 상세 2줄
  }

  u8g2.sendBuffer();
}

// ─── 헤더 (y 0~11) ───
void drawHeader() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);

  // 왼쪽: 현재 모드명
  u8g2.drawStr(0, 9, modeShortName(currentMode));

  // 오른쪽: WiFi 상태 (센서 모드 외)
  char info[16];
  if (currentMode == MODE_SENSOR) {
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
  drawCenteredTrimmed(displayData.line2, 62);
}

// 로딩 중 보여줄 모드별 컨텍스트 힌트
const char* modeContextHint(Mode m) {
  switch (m) {
    case MODE_BUS:    return "ARS " BUS_ARS_ID;
    case MODE_SUBWAY: return SUBWAY_STATION "역";
    case MODE_SPAT:   return strlen(SPAT_ITST_ID) > 0 ? SPAT_ITST_ID : "교차로";
    case MODE_SENSOR: return "센서 시작";
    default:          return "";
  }
}

// ════════════════════════════════════════════════════════════════
//  UTF-8 가운데 정렬 + 자동 trim (한글 안전)
// ════════════════════════════════════════════════════════════════
void drawCenteredTrimmed(const char* text, int y) {
  int w = u8g2.getUTF8Width(text);
  if (w <= OLED_MAX_TEXT_WIDTH) {
    int x = (OLED_WIDTH - w) / 2;
    u8g2.drawUTF8(x, y, text);
    return;
  }

  static char buf[64];
  int len = strlen(text);
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  strncpy(buf, text, len);
  buf[len] = '\0';

  while (len > 0) {
    // UTF-8 마지막 문자 시작점 찾기 (continuation byte 건너뛰기)
    int cut = len - 1;
    while (cut > 0 && (buf[cut] & 0xC0) == 0x80) cut--;
    buf[cut] = '\0';

    char tmp[68];
    snprintf(tmp, sizeof(tmp), "%s...", buf);
    if (u8g2.getUTF8Width(tmp) <= OLED_MAX_TEXT_WIDTH) {
      int x = (OLED_WIDTH - u8g2.getUTF8Width(tmp)) / 2;
      u8g2.drawUTF8(x, y, tmp);
      return;
    }
    len = cut;
  }
  u8g2.drawUTF8(56, y, "...");
}

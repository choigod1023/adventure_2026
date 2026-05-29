/*
 * sensors.ino  -  PIR (HC-SR505) + RCWL-0516 차량 감지
 *
 * pollSensors()      : loop 마다 호출. 에지 검출 + 상태 업데이트
 * evaluateSensor()   : ④ MODE_SENSOR 의 위험 평가 (PIR 게이팅 적용)
 *
 * PIR 게이팅 로직 (NOTES_RCWL_FALSEPOSITIVE.md):
 *   - PIR HIGH + RCWL HIGH 동시 → 사용자 본인 움직임 → 무시
 *   - PIR LOW + RCWL HIGH        → 비-사람 움직임 → 차량 가능성 ⭐
 *   - PIR 최근(5초) HIGH + 현재 LOW + RCWL HIGH → 사용자 정지 중 외부 움직임 → 차량 ⭐
 */

// Forward declarations
void updateSensorDisplay();

const unsigned long PIR_RECENT_WINDOW_MS = 5000UL;

void pollSensors() {
  // ── PIR ──
  bool pir_now = (digitalRead(PIN_PIR) == HIGH);
  if (pir_now && !sensors.pir_now) {
    Serial.println(F("[PIR ↑]"));
  }
  if (pir_now) sensors.pir_last_high_ms = millis();
  sensors.pir_now = pir_now;

  // ── RCWL ──
#if HAS_RCWL
  bool radar_now = (digitalRead(PIN_RADAR) == HIGH);
  if (radar_now && !sensors.radar_now) {
    sensors.radar_trigger_count++;
    Serial.print(F("[RADAR ↑] #"));
    Serial.println(sensors.radar_trigger_count);
  }
  if (radar_now) sensors.radar_last_high_ms = millis();
  sensors.radar_now = radar_now;
#else
  // RCWL 미부착: floating pin 노이즈 차단을 위해 항상 LOW 가정
  sensors.radar_now = false;
#endif

  // ④ 모드일 때만 OLED 표시 데이터 항상 갱신 (실시간성 ↑)
  if (currentMode == MODE_SENSOR) {
    updateSensorDisplay();
  }
}

// ════════════════════════════════════════════════════════════════
//  ④ MODE_SENSOR 위험 평가 (NOTES_RCWL_FALSEPOSITIVE.md 의 isVehicleDetected)
// ════════════════════════════════════════════════════════════════
bool evaluateSensor() {
  if (modeJustChanged) {
    modeJustChanged = false;
    updateSensorDisplay();
  }

#if !HAS_RCWL
  // RCWL 미부착 시 ④ 모드는 데모 (PIR만 모니터링, 위험 트리거 없음)
  return false;
#else
  bool userArrivedRecently =
    (sensors.pir_last_high_ms != 0) &&
    ((millis() - sensors.pir_last_high_ms) < PIR_RECENT_WINDOW_MS);

  // 케이스 A: 사용자 정지 중 + RCWL 외부 움직임 → 차량
  if (userArrivedRecently && !sensors.pir_now && sensors.radar_now) {
    return true;
  }
  // 케이스 B: PIR + RCWL 동시 → 본인 움직임 → 무시
  if (sensors.pir_now && sensors.radar_now) {
    return false;
  }
  // 케이스 C: PIR 최근 흔적 없음 + RCWL → 외부 → 차량
  if (!userArrivedRecently && sensors.radar_now) {
    return true;
  }
  return false;
#endif
}

// ④ 모드에서 OLED 에 보여줄 정보 갱신
void updateSensorDisplay() {
  char line1[40];
  char line2[48];

  snprintf(line1, sizeof(line1), "센서 모드");

#if !HAS_RCWL
  // RCWL 미부착: PIR만 모니터링
  if (sensors.pir_now) {
    snprintf(line2, sizeof(line2), "PIR HIGH (RCWL 없음)");
  } else {
    snprintf(line2, sizeof(line2), "PIR 대기 (RCWL 없음)");
  }
#else
  if (sensors.radar_now) {
    snprintf(line2, sizeof(line2), "RCWL HIGH (#%d)",
             sensors.radar_trigger_count);
  } else if (sensors.pir_now) {
    snprintf(line2, sizeof(line2), "PIR HIGH (사용자)");
  } else {
    unsigned long ago = (sensors.radar_last_high_ms == 0)
                          ? 0
                          : (millis() - sensors.radar_last_high_ms) / 1000;
    if (ago == 0) {
      snprintf(line2, sizeof(line2), "대기 중");
    } else {
      snprintf(line2, sizeof(line2), "대기 (%lus 전 감지)", ago);
    }
  }
#endif

  setDisplayValid(line1, line2, false);   // danger 는 updateDangerState가 결정
}

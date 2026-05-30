/*
 * sensors.ino  -  PIR (HC-SR505, D5) + RCWL-0516 (D2) 차량 감지
 *
 * pollSensors()      : loop 마다 호출. 에지 검출 + 상태 업데이트
 * evaluateSensor()   : 스위치 LOW (센서 모드) 의 위험 평가 (PIR 게이팅)
 *
 * PIR 게이팅 (NOTES_RCWL_FALSEPOSITIVE.md):
 *   - PIR HIGH + RCWL HIGH 동시          → 사용자 본인 움직임 → 무시
 *   - PIR LOW  + RCWL HIGH               → 비-사람 움직임 → 차량 가능성 ⭐
 *   - PIR 최근(5s) HIGH + 현재 LOW + RCWL → 사용자 정지 중 외부 움직임 → 차량 ⭐
 */

// Forward declarations
void updateSensorDisplay();

const unsigned long PIR_RECENT_WINDOW_MS = 5000UL;

void pollSensors() {
  // ── PIR (D5) ──
  bool pir_now = (digitalRead(PIN_PIR) == HIGH);
  if (pir_now && !sensors.pir_now) {
    Serial.println(F("[PIR ↑]"));
  }
  if (pir_now) sensors.pir_last_high_ms = millis();
  sensors.pir_now = pir_now;

  // ── RCWL (D2) ──
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
  sensors.radar_now = false;   // 미부착: floating 노이즈 차단
#endif

  if (sensorMode) {
    updateSensorDisplay();
  }
}

bool evaluateSensor() {
  if (modeJustChanged) {
    modeJustChanged = false;
    updateSensorDisplay();
  }

#if !HAS_RCWL
  return false;   // RCWL 미부착 시 데모 모드 (위험 트리거 없음)
#else
  bool userArrivedRecently =
    (sensors.pir_last_high_ms != 0) &&
    ((millis() - sensors.pir_last_high_ms) < PIR_RECENT_WINDOW_MS);

  if (userArrivedRecently && !sensors.pir_now && sensors.radar_now) return true;
  if (sensors.pir_now && sensors.radar_now)                          return false;
  if (!userArrivedRecently && sensors.radar_now)                     return true;
  return false;
#endif
}

void updateSensorDisplay() {
  char line2[48];

#if !HAS_RCWL
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
    if (ago == 0)            snprintf(line2, sizeof(line2), "대기 중");
    else                     snprintf(line2, sizeof(line2), "대기 (%lus 전 감지)", ago);
  }
#endif

  setDisplayValid("센서 모드", line2, false);
}

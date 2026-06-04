/*
 * sensors.ino  -  PIR (HC-SR505, D5) + RCWL-0516 (D2) 차량 감지
 *
 * pollSensors()      : loop 마다 호출. 에지 검출 + 상태 업데이트
 * evaluateSensor()   : 센서 모드의 위험 평가 — PIR 과 RCWL 이 동시에 HIGH 면 위험.
 *                      레벨 기반(AND)이라 어느 쪽이 먼저 떴든 둘 다 HIGH 인 순간 트리거.
 */

// Forward declarations
void updateSensorDisplay();

void pollSensors() {
  // ── PIR (D5) ──
  bool pir_now = (digitalRead(PIN_PIR) == HIGH);
  if (pir_now && !sensors.pir_now) {
    Serial.println(F("[PIR ↑]"));
  }
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

  // PIR 과 RCWL 이 동시에 HIGH(11) 면 위험.
  // 레벨 기반이라 어느 쪽이 먼저 떴든(01→11, 10→11, 00→11) 둘 다 HIGH 인 순간 트리거.
  return sensors.pir_now && sensors.radar_now;
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


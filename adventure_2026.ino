// ============================================================
//  DuoBell 오디오 믹싱 테스트
// ------------------------------------------------------------
//  저음(DAC 사인) + 고음(PWM 사각)을 동시 출력 → 하드웨어
//  저항 합산으로 모노 스피커 1개에서 두 톤이 같이 울리는지 확인.
//
//  배선 (J9):
//    A0 (DAC) ──[ R_a 10k ]──┐
//                            ├──[ C 1uF ]── 스피커(+)   (앰프 권장)
//    D6~(PWM) ──[ R1 ]─[ C1 ]┘             스피커(-) ── GND
// ============================================================

#include "analogWave.h"
#include "config.h"

analogWave wave(DAC); // A0 = DAC, 저음 사인 출력

void warnOn() {
  wave.amplitude(0.8);                    // 저음 ON (DAC)
  tone(PIN_SPK_PWM, TONE_FREQ_SECONDARY); // 고음 ON (PWM)
}

void warnOff() {
  wave.amplitude(0.0); // 저음 OFF (freq 유지, 진폭만 0)
  noTone(PIN_SPK_PWM); // 고음 OFF
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SPK_PWM, OUTPUT);

  wave.sine(TONE_FREQ_PRIMARY); // 780Hz 사인 준비
  wave.amplitude(0.0);          // 시작은 무음

  Serial.print("DuoBell test: ");
  Serial.print(TONE_FREQ_PRIMARY);
  Serial.print("Hz(DAC) + ");
  Serial.print(TONE_FREQ_SECONDARY);
  Serial.println("Hz(PWM)");
}

void loop() {
  warnOn();
  Serial.println("[WARN]  두 톤 동시 출력");
  delay(1000);

  warnOff();
  Serial.println("[idle]  무음");
  delay(1000);
}

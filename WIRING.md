# adventure_2026 - 결선 가이드

본 문서는 [`config.h`](./config.h) 의 핀 매핑 + PCB 실측 스키매틱 기준 결선 방법입니다.
변경할 일이 있으면 **반드시 `config.h` 와 같이** 갱신하세요.

---

## 한눈에 보는 핀 매핑

| 핀 | 연결 대상 | 커넥터 | 방향 | 비고 |
|----|-----------|--------|------|------|
| **D2** | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 |
| **D5** | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| **D6 (PWM)** | (구 DuoBell 고음) 미사용 | J9 | — | PCM 재생으로 대체 → 예비 |
| **D8** | (예비) | — | — | 외부 모드 스위치 제거 → 미사용 |
| **D9** | API 버튼 → BUS | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D10** | API 버튼 → SUBWAY | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D11** | API 버튼 → CITS | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D12** | 모드 버튼 (토글) | J7 | INPUT_PULLUP | OLED 내장 버튼 D — **누르면 API ↔ SENSOR 전환** |
| **A0 (DAC)** | 경고음 PCM 출력 | J9 | OUTPUT (DAC) | sound.py PCM 22050Hz → R/C → 스피커 |
| **A4 (D18)** | OLED SDA | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| **A5 (D19)** | OLED SCL | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| **+5V** | VCC (센서·OLED) | J1 | POWER | |
| **GND** | GND (전체 공통) | J1/J6 | POWER | |

> ⚠️ UNO R4 WiFi 의 진짜 DAC 는 **A0 한 개뿐**. 다른 용도로 쓰지 말 것.

---

## 부품별 결선 상세

### 1. OLED (SSD1306 128×64, I2C 4핀) — J7

```
OLED        UNO R4
─────────────────────
VCC   ──→   3.3V (또는 5V)
GND   ──→   GND
SCL   ──→   A5 / D19
SDA   ──→   A4 / D18
```

- I2C 주소 0x3C 자동 인식 (U8g2 NONAME 드라이버).
- 소프트웨어 I2C(`_SW_I2C`)로 SCL/SDA(=A5/A4) 비트뱅잉. 물리 결선은 하드웨어 I2C와 동일.

### 2. HC-SR505 (PIR, 인체 감지) — J11

```
HC-SR505    UNO R4
─────────────────────
VCC   ──→   5V
GND   ──→   GND
OUT   ──→   D5     (config.h: PIN_PIR)
```

- 부팅 후 **약 1분 안정화** 필요.
- HIGH 유지 ~2.5초, 정지하면 자동 LOW.

### 3. RCWL-0516 (마이크로파 도플러) — J8

```
RCWL-0516   UNO R4
─────────────────────
VIN   ──→   5V       (4~28V 가능)
GND   ──→   GND
OUT   ──→   D2       (config.h: PIN_RADAR)
```

- 부팅 후 **약 1분 안정화** 필요.
- 부품 미부착 시 `config.h` 의 `HAS_RCWL=0` 으로 두면 D2를 INPUT으로 잡지 않아 floating 노이즈 회피.
- 사용자 본인 오감지 방지는 [`NOTES_RCWL_FALSEPOSITIVE.md`](./NOTES_RCWL_FALSEPOSITIVE.md) 참고.

### 4. 모드 버튼들 (OLED 모듈 내장 4 버튼) — J7

네 버튼 모두 OLED 모듈에 내장. `INPUT_PULLUP` 이라 외부 저항 불필요.
누르면 LOW (눌림 에지에서 동작), 떼면 HIGH 복귀. 디바운스 20ms (`config.h: DEBOUNCE_MS`).

```
OLED 모듈 내장 버튼 (J7)   UNO R4
──────────────────────────────
버튼 BUS    ──→ D9  (PIN_BTN_BUS)
버튼 SUBWAY ──→ D10 (PIN_BTN_SUBWAY)
버튼 CITS   ──→ D11 (PIN_BTN_SPAT)
버튼 D      ──→ D12 (PIN_BTN_MODE)   ← API ↔ SENSOR 토글
```

**동작**:
- **D12 (OLED 버튼 D)** 누름 → API 카테고리 ↔ SENSOR 모드 **토글**
- **D9 / D10 / D11** 누름 → 각각 **BUS / SUBWAY / CITS** API 모드로 직접 점프
  (SENSOR 모드 상태였어도 누르면 해당 API 모드로 복귀)
- 외부 모드 스위치(J10, D8)는 제거됨 — OLED 내장 버튼 D(D12)로 대체. D8 은 예비.

### 5. 경고음 오디오 (단일 DAC PCM 재생) — J9

```
A0 (DAC) ──[ R_a 10kΩ ]──[ C 1μF ]── 스피커 (+)
                                       스피커 (-) ── GND
        (D6 는 미사용 — R1 분기는 그대로 둬도 무방, 신호 없음)
```

- **A0 (DAC)**: `alert_pcm.h` 의 12bit PCM 을 **22050Hz** 로 재생 (`FspTimer` ISR + `analogWrite(DAC, …)`).
  `sound.py` 의 합성음(3–5kHz 스윕 + 벨 배음 + 온셋 클릭 + 750Hz 보험)이 PCM 한 채널에 이미 다 섞여 있어 **DAC 한 개만으로** 동일한 소리가 난다.
- **R_a + C**: DAC 계단 출력을 매끈하게 깎는 1차 RC 로우패스(재구성 필터) 겸 DC 차단. 기존 합산망을 그대로 재사용하면 된다.
- **D6 (PWM)**: 더 이상 쓰지 않음. R1 분기는 떼도 되고, 둬도 무방(신호 없음).
- 음량이 부족하면 단일 채널 D급 앰프(PAM8403 등)를 R/C 뒤에 추가.

> **변경 이력**: 과거에는 A0 사인(780Hz) + D6 사각(2kHz) 을 저항 합산하는 "DuoBell" 2톤 방식이었으나, `sound.py` 와 **동일한** 음색을 내기 위해 미리 렌더한 PCM(`alert_pcm.h`)을 DAC 로 그대로 재생하는 방식으로 교체됨. 펄싱 간격만 `config.h: WARN_PULSE_OFF_MS` 로 조절.

---

## 전원 정리

| 부품 | 전원 |
|---|---|
| UNO R4 WiFi | USB 5V (PC 또는 5V 어댑터) |
| OLED / PIR / RCWL | UNO R4 의 5V 레일 |
| 스피커 (DuoBell) | UNO R4 의 DAC/PWM 직결 (앰프 없음) |

> 모든 GND 는 **한 점에서 묶기** (star topology). 톤 출력 노이즈를 줄임.

---

## 결선 순서

1. **UNO R4 만 USB 연결** → 시리얼 모니터 부팅 로그 확인
2. **OLED I2C 연결** (J7) → "부팅 중" 화면 표시되면 OK
3. **OLED 내장 버튼** → 버튼 D(D12) 누르면 `[버튼] 토글 → API/SENSOR`, 버튼 BUS/SUBWAY/CITS(D9~D11) → `[버튼] API → BUS/SUBWAY/CITS` 출력
4. **PIR** (D5, J11) → 시리얼에 `[PIR ↑]` 출력 확인 (1분 안정화 후)
5. **RCWL** (D2, J8) → `[RADAR ↑] #N` 출력 확인 (config.h `HAS_RCWL=1` 로 활성화)
6. **경고음** (A0, J9) → 위험 상태에서 스피커로 `sound.py` 경고음이 펄싱 재생되는지 확인
   - 부팅 직후 DAC 가 `ALERT_MID`(무음 중앙값)로 파킹 → 험/팝 없이 조용한지 먼저 확인
   - 위험 트리거 시 `driveAudio()` 가 PCM 한 발 + gap 을 반복

---

## 트러블슈팅

| 증상 | 원인/해결 |
|---|---|
| OLED 안 켜짐 | I2C 주소(0x3C/0x3D) 확인. SDA/SCL 바뀐 거 아닌지 확인. |
| 버튼 눌러도 모드 안 바뀜 | OLED 내장 버튼(D9/D10/D11/D12) 결선 확인. 시리얼에 `[버튼]` 로그 안 뜨면 결선/디바운스(`DEBOUNCE_MS`) 점검. |
| RCWL 계속 HIGH | 부팅 1분 안. 또는 주변 움직임 많음. 구리테이프 차폐 권장. |
| PIR 반응 늦음 | HC-SR505 자체 ~2.5초 지연. 정상. |
| 경고음 아예 안 남 | A0→R_a→C→스피커 결선 확인. 시리얼에 `[오디오] 타이머 확보 실패` 뜨면 FspTimer 미확보. |
| 소리가 찢어짐/거침 | 정상(거칠음 변조 의도). 심하면 C 키워 고역 깎기(1μF→2.2μF). DAC 험은 ALERT_MID 파킹으로 억제됨. |
| 음량 너무 작음 | DAC 직결로는 한계. R/C 뒤에 단일 채널 D급 앰프(PAM8403 등) 추가. |
| 음색을 바꾸고 싶음 | `sound.py` 파라미터 조정 → 재렌더해 `alert_pcm.h` 교체 (mode=loud 등). |

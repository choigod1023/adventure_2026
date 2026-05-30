# adventure_2026 - 결선 가이드

본 문서는 [`config.h`](./config.h) 의 핀 매핑 + PCB 실측 스키매틱 기준 결선 방법입니다.
변경할 일이 있으면 **반드시 `config.h` 와 같이** 갱신하세요.

---

## 한눈에 보는 핀 매핑

| 핀 | 연결 대상 | 커넥터 | 방향 | 비고 |
|----|-----------|--------|------|------|
| **D2** | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 |
| **D5** | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| **D6 (PWM)** | DuoBell 고음 출력 | J9 | OUTPUT | 2kHz 사각파 → R/C 합산 → 스피커 |
| **D8** | 모드 버튼 (토글) | J10 | INPUT_PULLUP | **누르면 API ↔ SENSOR 전환** |
| **D9** | API 버튼 → BUS | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D10** | API 버튼 → SUBWAY | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D11** | API 버튼 → CITS | J7 | INPUT_PULLUP | OLED 모듈 내장 버튼 |
| **D12** | (예비) | J7 | — | 미사용 |
| **A0 (DAC)** | DuoBell 저음 출력 | J9 | OUTPUT (DAC) | 780Hz 사인파 → R 합산 → 스피커 |
| **A4 (D18)** | OLED SDA | J7 | I2C | 하드웨어 I2C |
| **A5 (D19)** | OLED SCL | J7 | I2C | 하드웨어 I2C |
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

### 4. 모드 버튼들 (모멘터리 푸시) — J10 + J7

모든 버튼은 한쪽 단자를 핀에, 반대쪽을 GND에 연결. `INPUT_PULLUP` 이라 외부 저항 불필요.
누르면 LOW (눌림 에지에서 동작), 떼면 HIGH 복귀. 디바운스 20ms (`config.h: DEBOUNCE_MS`).

```
모드 버튼 (J10)         UNO R4
──────────────────────────────
버튼 한쪽 ──→ D8 (PIN_BTN_MODE)   반대쪽 ──→ GND

OLED 모듈 내장 버튼 (J7)   UNO R4
──────────────────────────────
버튼 BUS    ──→ D9  (PIN_BTN_BUS)
버튼 SUBWAY ──→ D10 (PIN_BTN_SUBWAY)
버튼 CITS   ──→ D11 (PIN_BTN_SPAT)
버튼 (예비) ──→ D12 (미사용)
```

**동작**:
- **D8** 누름 → API 카테고리 ↔ SENSOR 모드 **토글**
- **D9 / D10 / D11** 누름 → 각각 **BUS / SUBWAY / CITS** API 모드로 직접 점프
  (SENSOR 모드 상태였어도 누르면 해당 API 모드로 복귀)
- 토글 스위치(SPDT)는 더 이상 사용 안 함 — 푸시 버튼으로 대체됨.

### 5. DuoBell 오디오 (DAC + PWM 합산) — J9

```
A0 (DAC) ──[ R_a 10kΩ ]──┐
                          ├──[ C 1μF ]── 스피커 (+)
D6~(PWM) ──[ R1 10kΩ ]──┘
                                          스피커 (-) ── GND
```

- **A0 (DAC)**: 780Hz **사인파** 저음. `analogWave` 라이브러리로 출력.
- **D6 (PWM)**: 2kHz **사각파** 고음. `tone()` 함수로 출력.
- 두 신호를 저항(R_a, R1)으로 가산 후 커패시터 C 거쳐 한 스피커에 합쳐서 출력.
- 듀얼 채널 앰프(PAM8610 등) 불필요. 음량이 부족하면 단일 채널 앰프 추가 가능.

> **DuoBell 의미**: 한 스피커에서 두 톤이 동시에 울려서 "더블 벨" 같은 소리. ANC(노이즈 캔슬링) 이어폰 유저에게도 잘 들리는 780Hz 와 귀가 습관화하기 어려운 2kHz 가 함께 출력됩니다.

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
3. **모드 버튼** (D8, J10) → 누르면 `[버튼] 토글 → API/SENSOR`, 디스플레이 버튼(D9~D11) → `[버튼] API → BUS/SUBWAY/CITS` 출력
4. **PIR** (D5, J11) → 시리얼에 `[PIR ↑]` 출력 확인 (1분 안정화 후)
5. **RCWL** (D2, J8) → `[RADAR ↑] #N` 출력 확인 (config.h `HAS_RCWL=1` 로 활성화)
6. **DuoBell** (A0 + D6, J9) → 위험 상태에서 두 톤이 한 스피커로 동시 출력되는지 확인
   - 단독 검증은 main에 있던 [DuoBell 테스트 코드](https://github.com/choigod1023/adventure_2026/blob/main/adventure_2026.ino) 의 `warnOn/warnOff` 패턴 활용

---

## 트러블슈팅

| 증상 | 원인/해결 |
|---|---|
| OLED 안 켜짐 | I2C 주소(0x3C/0x3D) 확인. SDA/SCL 바뀐 거 아닌지 확인. |
| 버튼 눌러도 모드 안 바뀜 | 버튼 한쪽이 핀(D8/D9/D10/D11), 반대쪽이 GND 인지 확인. 시리얼에 `[버튼]` 로그 안 뜨면 결선/디바운스(`DEBOUNCE_MS`) 점검. |
| RCWL 계속 HIGH | 부팅 1분 안. 또는 주변 움직임 많음. 구리테이프 차폐 권장. |
| PIR 반응 늦음 | HC-SR505 자체 ~2.5초 지연. 정상. |
| 톤 한쪽만 들림 (저음만/고음만) | R_a 또는 R1 단선. 합산점에서 양쪽 신호 모두 측정. |
| 톤 노이즈 심함 | C 1μF 가 너무 작거나 큼. 100nF~10μF 범위에서 조정. 또는 GND 공유 확인. |
| 음량 너무 작음 | DAC 직결로는 한계. 단일 채널 D 클래스 앰프(PAM8403 등) 추가. |

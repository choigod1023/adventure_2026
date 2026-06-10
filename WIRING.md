# adventure_2026 - 결선 가이드

본 문서는 [`config.h`](./config.h) 의 핀 매핑 + PCB 실측 스키매틱 기준 결선 방법입니다.
변경할 일이 있으면 **반드시 `config.h` 와 같이** 갱신하세요.

---

## 한눈에 보는 핀 매핑

| 핀 | 연결 대상 | 커넥터 | 방향 | 비고 |
|----|-----------|--------|------|------|
| **D2** | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 |
| **D5** | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| **D6 (PWM)** | 고음 스피커2 출력 | J9 | OUTPUT | 2kHz 사각파 → R/C → 스피커2 |
| **D7** | OLED SDA (SW I2C) | J7 | I2C | config `OLED_USE_SW_I2C=1` |
| **D8** | OLED SCL (SW I2C) | J7 | I2C | config `OLED_USE_SW_I2C=1` |
| **D9** | 버튼 D → 모드 토글 | J7 | INPUT_PULLUP | API ↔ SENSOR 전환 |
| **D10** | 버튼 C → CITS | J7 | INPUT_PULLUP | OLED 모듈 내장 |
| **D11** | 버튼 B → BUS | J7 | INPUT_PULLUP | OLED 모듈 내장 |
| **D12** | (예비) | — | — | 비어있음 |
| **D13** | 버튼 A → SUBWAY | J7 | INPUT_PULLUP | ⚠️ 온보드 LED 핀 (읽기 불안정 가능) |
| **A0 (DAC)** | 저음 스피커1 출력 | J9 | OUTPUT (DAC) | 780Hz 사인파 → R/C → 스피커1 |
| **A4/A5** | (OLED HW I2C 시 SDA/SCL) | J7 | I2C | 현재 SW I2C 사용 중이라 미사용 |
| **+5V** | VCC (센서·OLED) | J1 | POWER | |
| **GND** | GND (전체 공통) | J1/J6 | POWER | |

> ⚠️ UNO R4 WiFi 의 진짜 DAC 는 **A0 한 개뿐**. 다른 용도로 쓰지 말 것.

---

## 부품별 결선 상세

### 1. OLED (SSD1306 128×64, I2C 4핀) — J7

```
OLED        UNO R4 (기본 = 하드웨어 I2C)
─────────────────────
VCC   ──→   3.3V (또는 5V)
GND   ──→   GND
SCL   ──→   A5 / D19
SDA   ──→   A4 / D18
```

- I2C 주소 0x3C 자동 인식 (U8g2 NONAME 드라이버).
- **기본은 하드웨어 I2C**(`config.h: OLED_USE_SW_I2C 0`). 부팅 시 시리얼에 I2C 스캔 결과가 찍힘 — `0x3C` 보이면 OLED 정상 연결, 아무것도 없으면 A4/A5 경로 문제.
- **폴백(소프트웨어 I2C, D8/D7)**: A4/A5 핀이 손상돼 스캔에 아무것도 안 잡히면 `config.h: OLED_USE_SW_I2C 1` 로 바꾸고 OLED 선을 옮긴다:

```
OLED        UNO R4 (폴백 = 소프트웨어 I2C)
─────────────────────
SCL   ──→   D8     (config.h: OLED_SW_SCL_PIN)
SDA   ──→   D7     (config.h: OLED_SW_SDA_PIN)
```

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
버튼 A (SUBWAY) ──→ D13 (PIN_BTN_SUBWAY)   ⚠️ 온보드 LED 핀
버튼 B (BUS)    ──→ D11 (PIN_BTN_BUS)
버튼 C (CITS)   ──→ D10 (PIN_BTN_SPAT)
버튼 D (모드)   ──→ D9  (PIN_BTN_MODE)     ← API ↔ SENSOR 토글
```

> ⚠️ **D13 은 온보드 LED(LED_BUILTIN) 핀**이라 버튼으로 쓰면 LED 회로 영향으로 `INPUT_PULLUP`
> 읽기가 불안정할 수 있음. 안 눌리거나 오작동하면 외부 풀업 추가 또는 빈 핀(D12)로 옮길 것.

**동작**:
- **D9 (OLED 버튼 D)** 누름 → API 카테고리 ↔ SENSOR 모드 **토글**
- **D13 / D11 / D10** 누름 → 각각 **SUBWAY / BUS / CITS** API 모드로 직접 점프
  (SENSOR 모드 상태였어도 누르면 해당 API 모드로 복귀)
- D8 은 OLED SW I2C(SCL) 로 사용 중. D12 가 비어있는 예비 핀.

### 5. DuoBell 오디오 (2채널 분리 — 스피커 2개) — J9

```
A0 (DAC) ──[ R_a 1kΩ ]──[ C 1μF ]── 스피커1 (+)   ; 스피커1 (-) ── GND   (저음 780Hz 사인)
D6~(PWM) ──[ R1 1kΩ ]──[ C 1μF ]── 스피커2 (+)   ; 스피커2 (-) ── GND   (고음 2kHz 사각)
```

- **A0 (DAC) → 스피커1**: 780Hz **사인파** 저음. `analogWave` 라이브러리로 출력. (ANC 골짜기 → 이어폰 침투)
- **D6 (PWM) → 스피커2**: 2kHz **사각파** 고음. `tone()` 함수로 출력. (습관화 방지, `WARN_HF_SWEEP=1` 이면 2.0~3.5kHz 왕복 스윕)
- **두 톤을 각각 별도 스피커로 분리 출력** (한 스피커 합산이 아님). 두 채널이 같은 펄싱 패턴(`WARN_PULSE_*`)으로 동시에 울린다.
- 각 R/C 는 핀별 1차 로우패스 + DC 차단. 합산이 아니므로 R 값은 작게(1kΩ 정도) 둬도 됨.
- 앰프 불필요. 음량이 부족하면 채널별로 단일 채널 앰프(PAM8403 등) 추가.

> **DuoBell 의미**: 두 스피커에서 서로 다른 두 톤이 동시에 울려서 "더블 벨" 같은 소리. ANC(노이즈 캔슬링) 이어폰 유저에게도 잘 들리는 780Hz(저음 스피커) 와 귀가 습관화하기 어려운 2kHz(고음 스피커) 를 분리해 함께 낸다.
>
> ⚠️ 고음 스피커를 **2kHz 고정**으로 쓰려면 `config.h` 의 `WARN_HF_SWEEP` 를 **0** 으로. (기본 1=스윕)

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
3. **OLED 내장 버튼** → 버튼 D(D9) 누르면 `[버튼] 토글 → API/SENSOR`, 버튼 SUBWAY/BUS/CITS(D13/D11/D10) → `[버튼] API → ...` 출력
4. **PIR** (D5, J11) → 시리얼에 `[PIR ↑]` 출력 확인 (1분 안정화 후)
5. **RCWL** (D2, J8) → `[RADAR ↑] #N` 출력 확인 (config.h `HAS_RCWL=1` 로 활성화)
6. **DuoBell** (A0→스피커1, D6→스피커2, J9) → 위험 상태에서 저음(780)·고음(2k) 두 스피커가 같은 펄싱으로 동시에 울리는지 확인
   - 동작은 `adventure_2026.ino` 의 `warnOn/warnOff/driveAudio` 참고

---

## 트러블슈팅

| 증상 | 원인/해결 |
|---|---|
| OLED 안 켜짐 | I2C 주소(0x3C/0x3D) 확인. SDA/SCL 바뀐 거 아닌지 확인. |
| 버튼 눌러도 모드 안 바뀜 | OLED 내장 버튼(D13/D11/D10/D9) 결선 확인. 시리얼에 `[버튼]` 로그 안 뜨면 결선/디바운스(`DEBOUNCE_MS`) 점검. D13(SUBWAY)은 LED핀이라 특히 의심. |
| RCWL 계속 HIGH | 부팅 1분 안. 또는 주변 움직임 많음. 구리테이프 차폐 권장. |
| PIR 반응 늦음 | HC-SR505 자체 ~2.5초 지연. 정상. |
| 톤 한쪽만 들림 (저음만/고음만) | 스피커1(A0)/스피커2(D6) 중 한쪽 결선·R/C 단선 확인. 각 핀 출력 따로 측정. |
| 톤 노이즈 심함 | C 1μF 가 너무 작거나 큼. 100nF~10μF 범위에서 조정. 또는 GND 공유 확인. |
| 음량 너무 작음 | DAC 직결로는 한계. 단일 채널 D 클래스 앰프(PAM8403 등) 추가. |

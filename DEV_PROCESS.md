#line 1 "/Users/jangjunhyeok/Documents/Arduino/adventure_2026/DEV_PROCESS.md"
# BionicGPT - 개발 프로세스

> 센서/스피커 실물이 아직 없으므로 **소프트웨어 우선 → 하드웨어 도착 시 통합** 전략.
> 각 모듈을 **독립 .ino 스케치**로 먼저 검증 후, 마지막에 메인 스케치로 병합.

---

## 전체 마일스톤

```
[Phase 1: 단일 기능 검증]  →  [Phase 2: 통합]  →  [Phase 3: 하드웨어 통합]
   .ino 스케치 5~6개           메인 스케치 1개      센서/스피커 결선
   (현재 단계 ★)
```

---

## Phase 1: 단일 기능 검증 (지금 가능)

각 항목을 **별도 폴더의 별도 .ino**로 만들어서 따로따로 검증합니다. 한 번에 다 합치면 디버깅이 지옥이라 분리하는 게 핵심.

### 1-1. ✅ C-ITS API (완료)
- 폴더: `adventure_2026/` (현재 사용자가 가진 코드)
- 상태: **동작 확인됨**
- 다음: 시리얼 출력 → 후술할 공용 함수로 추출 (`fetchSPAT()`)

### 1-2. 버스 도착 API 검증
- 새 스케치: `tests/test_bus_api/test_bus_api.ino`
- 목표: 노선 1개에 대해 `arrmsg1` 시리얼 출력
- 체크포인트:
  - [ ] `serviceKey` URL 인코딩 처리
  - [ ] XML에서 `<arrmsg1>...</arrmsg1>` 추출 (substring)
  - [ ] "곧 도착" / "N분 후" 정규식 없이 파싱
- 위험요소: 응답 XML이 클 수 있음 → 스트림으로 한 줄씩 읽으며 substring 매칭

### 1-3. 지하철 도착 API 검증
- 새 스케치: `tests/test_subway_api/test_subway_api.ino`
- 목표: 1개 역의 도착 정보 5건 출력
- 체크포인트:
  - [ ] 한글 역명 URL 인코딩 (PROGMEM 상수로 박아두기)
  - [ ] `sample` 키로 먼저 테스트 → 본인 키로 교체
  - [ ] `arvlMsg2`에 "곧 도착" 포함 여부 판단

### 1-4. OLED 표시 검증
- 새 스케치: `tests/test_oled/test_oled.ino`
- 목표: U8G2로 다음 페이지 표시
  - [ ] 페이지 A: WiFi 상태 + IP
  - [ ] 페이지 B: API 응답 1줄 (시리얼 대신 OLED에)
  - [ ] 페이지 C: 모드 표시 (API/센서)
  - [ ] 페이지 D: 디버그 (free SRAM, uptime)
- 기존 사용 코드 활용:
  ```cpp
  U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, SCL, SDA, U8X8_PIN_NONE);
  ```

### 1-5. 4 버튼 입력 (BTN_MODE 모드 순환)
- 새 스케치: `tests/test_input/test_input.ino`
- 목표: 모든 버튼 입력을 시리얼로 로그 + 모드 순환 확인
- 체크포인트:
  - [ ] 4 버튼 디바운싱 (10~20ms)
  - [ ] **BTN_MODE 누를 때마다** 현재 모드가 `버스→지하철→C-ITS→센서→버스…` 순으로 순환
  - [ ] 모드 전환 시 OLED에 1초간 새 모드 이름 표시
  - [ ] 짧게 누름 / 길게 누름 구분 — 짧게: 다음 모드 / 길게: 모드 ① 로 리셋
  - [ ] 나머지 3 버튼: 페이지 전환 / 볼륨 ± / 디버그 토글
- ⚠️ **기존의 모드 스위치(SPDT)는 폐기**. 모드 전환은 버튼만 사용.

### 1-5b. ④ 센서 모드 단독 검증 (센서 도착 후)
- 새 스케치: `tests/test_sensor_mode/test_sensor_mode.ino`
- 목표: 신호등 없는 환경 가정. WiFi/API 없이 PIR + RCWL만으로 위험 판정
- 체크포인트:
  - [ ] PIR(HC-SR505) HIGH/LOW 시리얼 로그
  - [ ] RCWL-0516 HIGH/LOW 시리얼 로그
  - [ ] [`NOTES_RCWL_FALSEPOSITIVE.md`](./NOTES_RCWL_FALSEPOSITIVE.md) 의 `isVehicleDetected()` 로직 적용
  - [ ] 사용자 본인 움직임 → 경고 안 울려야 함
  - [ ] 사용자 정지 + 외부 RCWL 트리거 → 경고 울려야 함

### 1-6. 톤 출력 (스피커 없이 시뮬레이션)
- 새 스케치: `tests/test_tone/test_tone.ino`
- 목표: 디지털 핀에 `tone()` 출력 → **테스터로 신호만 확인** (실제 스피커는 PAM8610 통해 출력)
- 체크포인트:
  - [ ] 780Hz 핀 A, 2000Hz 핀 B
  - [ ] 두 톤 동시 출력 (`tone()`은 1개씩만 가능 → **타이머/PWM 직접 제어** 또는 `Tone` 라이브러리)
  - [ ] 불규칙 패턴 (회의록의 "ANC 습관화 방지") — `random()` 기반 주기 변경

> ⚠️ UNO R4의 `tone()` 동시 출력 제약 확인 필요. 안 되면 ESP32의 LEDC 또는 DAC로 우회.

---

## Phase 2: 통합 (Phase 1 모두 OK 후)

### 2-1. 폴더 구조
```
adventure_2026/
├── adventure_2026.ino       # main loop
├── secrets.h                # WiFi/API키 (절대 공유 X, .gitignore)
├── config.h                 # 핀, 노선ID, 역명 등 설정값
├── mode_manager.h / .cpp    # 4개 모드 순환/현재 모드 상태 관리
├── api_bus.h / .cpp         # ① 버스 API
├── api_subway.h / .cpp      # ② 지하철 API
├── api_spat.h / .cpp        # ③ C-ITS API (현재 코드를 함수로 추출)
├── sensor.h / .cpp          # ④ RCWL+SR505 (PIR 게이팅 포함)
├── oled_ui.h / .cpp         # 화면 페이지 관리 (모드별 화면 분기)
├── input.h / .cpp           # BTN_MODE(모드 순환) + BTN_PAGE 외 3개
├── tone_out.h / .cpp        # 스피커 출력
└── state.h                  # 위험 판단 상태머신
```

### 2-2. 모드 상태 / 위험 상태머신

**모드 상태** (BTN_MODE 누를 때마다 순환):
```
  ┌─→ ① MODE_BUS_API ──→ ② MODE_SUBWAY_API ──→ ③ MODE_SPAT_API ──→ ④ MODE_SENSOR ─┐
  └────────────────────────────────────────────────────────────────────────────────┘
                              (BTN_MODE 클릭)
```

**위험 상태** (모드와 독립적, 모든 모드 공통):
```
        [IDLE]
          │
   현재 모드의 위험 트리거 감지
          ↓
       [WARN]  ──── 위험 해소 + WARN_DURATION_MS ────→ [IDLE]
          │
       톤 출력 (780Hz + 2kHz)
       OLED 빨강 표시
```

### 2-3. 메인 loop 구조 (의사코드)
```cpp
enum Mode { MODE_BUS_API, MODE_SUBWAY_API, MODE_SPAT_API, MODE_SENSOR, MODE_COUNT };
Mode currentMode = MODE_BUS_API;

void loop() {
  // 1. 입력 polling (매 loop)
  readButtons();
  if (btnModePressed()) {
    currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
    oledShowModeChange(currentMode);  // 1초간 모드 이름 표시
  }

  // 2. WiFi 끊김 시 자동 폴백 (API 모드 → 센서 모드)
  if (currentMode != MODE_SENSOR && !wifiConnected()) {
    oledShowOffline();
    currentMode = MODE_SENSOR;  // 자동 폴백
  }

  // 3. 위험 판단 (현재 모드에 따라)
  bool danger = false;
  switch (currentMode) {
    case MODE_BUS_API:    danger = checkBusAPI();    break;  // ①
    case MODE_SUBWAY_API: danger = checkSubwayAPI(); break;  // ②
    case MODE_SPAT_API:   danger = checkSPATAPI();   break;  // ③
    case MODE_SENSOR:     danger = checkSensors();   break;  // ④ PIR 게이팅 + RCWL
  }

  // 4. 상태 전환
  updateState(danger);

  // 5. 출력
  driveTone();
  updateOLED(currentMode, danger);
}
```

**중요**:
- API 호출은 **비동기/타임아웃 짧게**. 동기로 10초씩 막히면 입력/출력 못 받음. → `millis()` 기반 인터벌로 분리.
- **모드 ④는 WiFi/API 호출 없음** → 응답 가장 빠름, 신호등 없는 환경 / 인터넷 장애 시 메인 폴백
- 각 모드의 폴링 주기는 `config.h`의 `POLL_INTERVAL_*_MS` 사용

---

## Phase 3: 하드웨어 통합 (센서 도착 후)

### 3-1. 핀맵 (실측 PCB 스키매틱 기준)

| 핀 | 연결 대상 | 커넥터 | 방향 | 비고 |
|----|-----------|--------|------|------|
| D2 | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 |
| D5~ | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| D6~ | 오디오 출력 → R1 → C1 → GND | J9 | OUTPUT (PWM) | 단일 채널, 780/2kHz 시분할 교대 |
| D12 | 모드 버튼 (OLED 내장 버튼 D) | J7 | INPUT_PULLUP | 누르면 API ↔ SENSOR 토글 |
| D18 (A4) | OLED SDA | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| D19 (A5) | OLED SCL | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| A0 (D14) | AUX (보조) | J9 | — | 예비 |
| +5V | VCC (센서·OLED) | J1 | POWER | |
| GND | GND (전체 공통) | J1/J6 | POWER | |

> ⚠️ 오디오는 PAM8610 듀얼 채널이 아니라 **D6~ 단일 PWM → RC 로우패스(R1/C1) → 스피커** 구조.
> 780Hz / 2kHz 듀얼톤은 1개 핀에서 **시간 분할(교대)** 로 출력한다.

### 3-2. 결선 순서
1. **전원만** 먼저 (UNO R4 + USB 5V)
2. **OLED** (J7, I2C SDA=D18/SCL=D19) → 화면 뜨면 OK
3. **모드 버튼** (OLED 내장 버튼 D, D12) → 시리얼 확인
4. **PIR (HC-SR505)** (J11, D5~) → 1핀, 디지털 입력
5. **레이다 (RCWL-0516)** (J8, D2) → 1핀, 디지털 입력
6. **오디오** (J9, D6~ → R1 → C1 → GND) → 스피커 연결

### 3-2. 통합 테스트 시나리오 (4개 모드)
- [ ] BTN_MODE 한 번씩 눌러서 4개 모드 순환 + OLED 표시 확인
- [ ] **① 버스 API 모드**: 정류장에 곧 도착하는 버스가 있을 때 → 경고
- [ ] **② 지하철 API 모드**: 모니터링 역 "곧 도착" → 경고
- [ ] **③ C-ITS API 모드**: 보행자 신호 잔여 3초 → 경고 ON
- [ ] **④ 센서 모드**:
  - [ ] PIR만 가렸을 때 → 경고 안 울려야 함 (보행자만, 차량 없음)
  - [ ] 사용자 본인이 움직일 때 (PIR+RCWL 동시) → 경고 안 울려야 함
  - [ ] 사용자 정지 + 외부 RCWL → 경고 ON
- [ ] **공통**: WiFi 끊김 → 현재 API 모드여도 OLED에 "OFFLINE → SENSOR" + 자동 ④ 폴백

---

## Phase 0: 지금 당장 해야 할 일 (사용자)

순서대로:

1. **인증키 3종 발급 + `secrets.h` 만들기**
   ```cpp
   // secrets.h (절대 GitHub에 올리지 말 것)
   #define WIFI_SSID     "..."
   #define WIFI_PASS     "..."
   #define BUS_API_KEY   "..."   // data.go.kr
   #define SUBWAY_API_KEY "..."  // data.seoul.go.kr
   #define SPAT_API_KEY  "..."   // t-data.seoul.go.kr (이미 있음)
   ```

2. **장치 설치 예상 위치 결정** → 그래야 어떤 버스/지하철/교차로 모니터링할지 정해짐
   - 예: "OO대학교 정문" → 근처 버스 노선 2개, 지하철 1개역, 교차로 1개

3. **위 위치의 ID 3종 수집**
   - `busRouteId` (버스 노선 9자리) — TOPIS 또는 버스 정류장 안내에서 확인
   - 지하철 역명 (한글 그대로)
   - `itstId` (교차로 ID) — t-data 사이트에서 검색

4. **Phase 1-2 (버스 API 검증)부터 시작**
   - 가장 간단한 XML 응답이라 워밍업으로 적절

---

## 작업 순서 우선순위 (제 추천)

```
[지금] ────────────────────────────────→ [센서 도착 후]

Phase 0  →  1-2 버스  →  1-3 지하철  →  1-4 OLED  →  1-5 입력
                                                       ↓
                                                    1-6 톤
                                                       ↓
                                                Phase 2 통합
                                                       ↓
                                              [센서 도착]
                                                       ↓
                                                Phase 3 통합
```

각 단계는 **시리얼 모니터로 검증 가능** → 하드웨어 없이도 진도 나갈 수 있음.

---

## Claude Code 사용 팁

이 프로젝트 진행 시:

- **각 Phase 시작할 때 새 Claude 세션** 시작 → 컨텍스트 깔끔
- "1-2 버스 API 스케치 만들어줘" 식으로 **Phase 번호로 요청**하면 빠름
- 에러 나면 **시리얼 모니터 출력 전체 복붙** → 가장 효율적
- API 응답이 이상하면 **postman/브라우저로 호출** 후 결과 복붙 → Claude가 파싱 코드 작성
- `secrets.h`는 Claude에게 **절대 보여주지 말 것** (보면 컨텍스트에 남음)

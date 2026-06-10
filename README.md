# adventure_2026 — 스몸비(보행자) 안전 경고 장치

Arduino UNO R4 WiFi 기반 보행자 위험 감지/경고 장치.
대중교통/신호등 API와 인체/차량 센서를 조합해 횡단보도·골목·교차로에서의 위험을 음향(780Hz + 2kHz)과 OLED로 안내합니다.

## 4개 동작 모드

장치의 **BTN_MODE** 버튼을 누를 때마다 순환 전환됩니다.

| # | 모드 | 데이터 소스 | 사용 환경 |
|---|---|---|---|
| ① | **버스 API** | 서울시 버스도착정보 (`getStationByUid`) | 버스 정류장 근처 |
| ② | **지하철 API** | 서울시 지하철 실시간 도착 (`realtimeStationArrival`) | 지하철역 환승 경로 |
| ③ | **C-ITS API** | 서울시 V2X 신호 잔여시간 (data_id=10120) | 신호등 있는 횡단보도 |
| ④ | **센서 모드** | RCWL-0516 + HC-SR505 (오프라인) | **신호등 없는 골목/도로** |

WiFi 끊김 시 모드 ①~③ 은 자동으로 ④ 센서 모드로 폴백됩니다.

## 하드웨어

- **MCU**: Arduino UNO R4 WiFi
- **센서**: HC-SR505 (PIR, 인체 감지), RCWL-0516 (마이크로파 도플러, 차량 감지)
- **출력**: SSD1306 OLED 128×64 (I2C), DuoBell 2채널 오디오 (A0 780Hz 사인 + D6 2kHz 사각 → 스피커 2개)
- **입력**: OLED 모듈 내장 4버튼 (버튼 D=API↔센서 토글, 나머지=BUS/SUBWAY/CITS 선택)

## 핀맵

> 실측 PCB 스키매틱 기준 (커넥터 J 번호 포함).

```
                  Arduino UNO R4 WiFi
          ┌─────────────────────────────────┐
          │  DIGITAL (PWM ~)                │
          │                                 │
RCWL OUT ─┤ D2                  A5/D19 ├─ SCL (OLED, J7)
          │ D3                  A4/D18 ├─ SDA (OLED, J7)
          │ D4                      A3 │
 PIR OUT ─┤ D5~                     A2 │
고음2k OUT ┤ D6~ (스피커2, J9)        A1 │
          │ D7              A0/D14 ├─ 저음780 OUT (스피커1, DAC, J9)
          │ D8 (예비)             AREF │
          │ D9~                     GND │
          │ D10~                     13 │
          │ D11~                     12 │
   MODE ──┤ D12 (OLED 버튼 D)      11~│
          │ D13 (LED_BUILTIN)       10~│
          │                          9~│
          │  POWER                          │
          │  5V ── +5V (센서/OLED, J1)      │
          │  GND── GND (전체 공통)          │
          └─────────────────────────────────┘
```

| 핀 | 연결 대상 | 커넥터 | 방향 | 비고 |
|----|-----------|--------|------|------|
| D2 | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 |
| D5~ | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| D6~ | 고음 스피커2 → R1 → C1 → GND | J9 | OUTPUT (PWM) | 2kHz 사각파 (tone, 스윕 옵션) |
| D12 | 모드 버튼 (OLED 내장 버튼 D) | J7 | INPUT_PULLUP | 누르면 API ↔ SENSOR 토글 |
| D18 (A4) | OLED SDA | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| D19 (A5) | OLED SCL | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| A0 (D14) | 저음 스피커1 → R_a → C → GND | J9 | OUTPUT (DAC) | 780Hz 사인파 (analogWave) |
| +5V | VCC (센서·OLED) | J1 | POWER | |
| GND | GND (전체 공통) | J1/J6 | POWER | |

## 셋업 (개발자)

1. **저장소 클론**
   ```
   git clone https://github.com/<USER>/adventure_2026.git
   ```

2. **`secrets.h` 작성** (저장소에 포함되지 않음)
   ```cpp
   #pragma once
   #define WIFI_SSID      "your-ssid"
   #define WIFI_PASS      "your-password"
   #define BUS_API_KEY    "..."   // data.go.kr
   #define SUBWAY_API_KEY "..."   // data.seoul.go.kr
   #define SPAT_API_KEY   "..."   // t-data.seoul.go.kr (UUID)
   ```

3. **`config.h` 에서 모니터링 대상 지정**
   - `BUS_ARS_ID` — 정류장 ARS 5자리
   - `SUBWAY_STATION` — 역명 (한글, "역" 글자 제외)
   - `SPAT_ITST_ID` — 교차로 ID

4. **필요 라이브러리** (Arduino IDE 라이브러리 매니저)
   - WiFiS3 (보드 패키지 포함)
   - ArduinoHttpClient
   - ArduinoJson **v7**
   - U8g2lib

## 문서

- [`WIRING.md`](./WIRING.md) — **모듈별 결선 가이드** (실제 조립 시 참고)
- [`API_GUIDE.md`](./API_GUIDE.md) — 사용하는 3개 API의 엔드포인트/파라미터/응답 형식 + 통합 아키텍처
- [`DEV_PROCESS.md`](./DEV_PROCESS.md) — Phase 1~3 개발 프로세스 + 메인 loop 의사코드
- [`NOTES_RCWL_FALSEPOSITIVE.md`](./NOTES_RCWL_FALSEPOSITIVE.md) — RCWL의 사용자 본인 오감지 문제와 PIR 게이팅 해결책

## 라이선스

본 저장소의 코드는 학습/연구 목적으로 자유롭게 사용 가능합니다.
사용된 API는 각 제공기관의 이용약관을 따릅니다 (data.go.kr / data.seoul.go.kr / t-data.seoul.go.kr).

# adventure_2026 — 스몸비(보행자) 안전 경고 장치

Arduino UNO R4 WiFi 기반 보행자 위험 감지/경고 장치.
대중교통/신호등 API와 인체/차량 센서를 조합해 횡단보도·골목·교차로에서의 위험을 음향(`sound.py` 합성 경고음 PCM 재생)과 OLED로 안내합니다.

## 4개 동작 모드

OLED 모듈에 내장된 **4개의 모멘터리 버튼**으로 모드를 전환합니다. 버튼 A/B/C 는 각 API 모드를 직접 선택하고(센서 모드였어도 곧바로 API 로 복귀), 버튼 D 는 API 카테고리 ↔ SENSOR 모드를 토글합니다. 현재 활성 모드의 위험 트리거만 경고를 발생시킵니다.

| # | 모드 | 선택 버튼 | 데이터 소스 | 사용 환경 |
|---|---|---|---|---|
| ① | **버스 API** | B (D10) | 서울 TOPIS 버스도착정보 (`getStationByUid`) | 버스 정류장 근처 |
| ② | **지하철 API** | A (D9) | 서울 열린데이터 실시간 도착 (`realtimeStationArrival`) | 지하철역 환승 경로 |
| ③ | **C-ITS API** | C (D11) | 서울 t-data V2X 신호 잔여시간 (SPAT) | 신호등 있는 횡단보도 |
| ④ | **센서 모드** | D (D12) 토글 | RCWL-0516 + HC-SR505 (오프라인) | **신호등 없는 골목/도로** |

WiFi 가 끊기면 해당 API 모드는 OLED 에 **OFFLINE** 을 표시하며(모드를 강제로 바꾸지 않음), 필요 시 D12 버튼으로 센서 모드로 전환해 오프라인 동작을 이어갈 수 있습니다.

## 하드웨어

- **MCU**: Arduino UNO R4 WiFi
- **센서**: HC-SR505 (PIR, 인체 감지), RCWL-0516 (마이크로파 도플러, 차량 감지)
- **출력**: SSD1306 OLED 128×64 (I2C), 단일 채널 DAC PCM 오디오 (`alert_pcm.h` 22050Hz → RC 로우패스 → 스피커)
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
          │ D6~ (예비, 구 PWM)        A1 │
          │ D7              A0/D14 ├─ AUDIO OUT (DAC PCM, J9)
          │ D8 (예비)             AREF │
 SUBWAY ──┤ D9  (OLED 버튼 A)      GND │
    BUS ──┤ D10 (OLED 버튼 B)       13 │
   CITS ──┤ D11 (OLED 버튼 C)       12 │
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
| D2 | RCWL-0516 OUT | J8 | INPUT | 마이크로파 차량 감지 (`HAS_RCWL` 토글) |
| D5 | HC-SR505 OUT | J11 | INPUT | PIR 인체 감지 |
| D6~ | (구 DuoBell 고음) 미사용 | J9 | — | PCM 재생으로 대체 → 예비 |
| D9 | 모드 버튼 A (OLED 내장) | J7 | INPUT_PULLUP | 누르면 SUBWAY 모드 선택 |
| D10 | 모드 버튼 B (OLED 내장) | J7 | INPUT_PULLUP | 누르면 BUS 모드 선택 |
| D11 | 모드 버튼 C (OLED 내장) | J7 | INPUT_PULLUP | 누르면 CITS 모드 선택 |
| D12 | 모드 버튼 D (OLED 내장) | J7 | INPUT_PULLUP | 누르면 API ↔ SENSOR 토글 |
| D18 (A4) | OLED SDA | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| D19 (A5) | OLED SCL | J7 | I2C | 소프트웨어 I2C (U8g2 `_SW_I2C`) |
| A0 (D14) | 경고음 PCM 출력 → R_a → C → 스피커 | J9 | OUTPUT (DAC) | `alert_pcm.h` 22050Hz 12bit |
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
   #define WIFI_PASSWORD  "your-password"
   #define BUS_API_KEY    "..."   // 서울 TOPIS ws.bus.go.kr 서비스키
   #define SUBWAY_API_KEY "..."   // 서울 열린데이터 swopenAPI.seoul.go.kr 인증키
   #define SPAT_API_KEY   "..."   // 서울 t-data.seoul.go.kr apikey (UUID)
   ```

3. **`config.h` 에서 모니터링 대상 지정**
   - `BUS_ARS_ID` — 정류장 ARS 5자리
   - `SUBWAY_STATION` — 역명 (한글, "역" 글자 제외)
   - `SPAT_ITST_ID` — 교차로 ID

4. **필요 라이브러리** (Arduino IDE 라이브러리 매니저)
   - WiFiS3 (UNO R4 보드 패키지 포함)
   - FspTimer (UNO R4 보드 패키지 포함 — 22050Hz DAC 오디오 ISR)
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

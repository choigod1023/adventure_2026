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
- **출력**: SSD1306 OLED 128×64, PAM8610 앰프 + 스피커 2개
- **입력**: 푸시버튼 4개 (BTN_MODE + 페이지/볼륨/디버그)

자세한 핀 배치는 [`config.h`](./config.h) 참조.

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

- [`API_GUIDE.md`](./API_GUIDE.md) — 사용하는 3개 API의 엔드포인트/파라미터/응답 형식 + 통합 아키텍처
- [`DEV_PROCESS.md`](./DEV_PROCESS.md) — Phase 1~3 개발 프로세스 + 메인 loop 의사코드
- [`NOTES_RCWL_FALSEPOSITIVE.md`](./NOTES_RCWL_FALSEPOSITIVE.md) — RCWL의 사용자 본인 오감지 문제와 PIR 게이팅 해결책

## 라이선스

본 저장소의 코드는 학습/연구 목적으로 자유롭게 사용 가능합니다.
사용된 API는 각 제공기관의 이용약관을 따릅니다 (data.go.kr / data.seoul.go.kr / t-data.seoul.go.kr).

#line 1 "/Users/jangjunhyeok/Documents/Arduino/adventure_2026/config.h"
#pragma once

// ============================================================
//  config.h  -  하드웨어/모니터링 대상 설정값
// ------------------------------------------------------------
//  이 파일은 공유해도 OK (비밀 정보 없음)
//  값이 바뀔 수 있는 모든 상수는 여기 모아둡니다.
// ============================================================

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  1. 모니터링 대상 (장치 설치 위치 기반)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 버스 정류장 ARS ID (정류장 표지판 5자리 숫자)
// 예: "23282" (서울대입구역)
// 찾는 법:
//   1) 정류장 표지판에 적힌 5자리 숫자 (예: 23-282)
//   2) 또는 https://bus.go.kr 에서 정류장 검색
//   3) 또는 getStationByName API 호출
#define BUS_ARS_ID "09153"

// 지하철 모니터링 역명 (한글 그대로, "역" 글자 제외)
// 예: "서울", "강남", "잠실"
#define SUBWAY_STATION "수유"

// 지하철 역명 UTF-8 URL 인코딩 (API URL 안전 전송용)
// "수유"   = %EC%88%98%EC%9C%A0
// "서울"   = %EC%84%9C%EC%9A%B8
// "강남"   = %EA%B0%95%EB%82%A8
// "잠실"   = %EC%9E%A0%EC%8B%A4
// "교대"   = %EA%B5%90%EB%8C%80
// SUBWAY_STATION 변경 시 반드시 같이 갱신할 것.
#define SUBWAY_STATION_ENC "%EC%88%98%EC%9C%A0"

// C-ITS 교차로 ID (itstId, 문자열). t-data 사이트 또는 응답의 itstId 필드에서 확인.
// ⚠️ 반드시 모니터링할 교차로 ID 를 지정할 것. "" 로 두면 임의 교차로가 잡힘.
//    "1063" = 번동사거리
#define SPAT_ITST_ID "1063"

// OLED 에 표시할 교차로 이름 (API 응답엔 한글 이름이 없어서 직접 지정)
#define SPAT_ITST_NAME "번동사거리"

// 감시할 보행 신호 방향 — 응답 필드명 + 표시 라벨
//   북=nt 동=et 남=st 서=wt / 대각: ne se sw nw
//   접미사: PdsgRmdrCs = 보행 잔여, StsgRmdrCs = 직진(차량) 잔여
//
//   ⚠️ 번동사거리(1063) raw 응답 분석 결과 (2026-06-03):
//      - 남(st): stStsgRmdrCs 항상 null → PED/CAR 비교 불가 → phase 판정 안 됨
//      - 서(wt): wtPdsgRmdrCs / wtStsgRmdrCs 둘 다 값 있음 → 비교 가능 ✓
//
//   페이즈 판정 (apis.ino fetchSpat):
//     활성 페이즈의 RmdrCs 가 더 짧음 (활성 = 곧 종료될 phase) →
//       PED < CAR → 보행 활성 = 보행 GREEN
//       CAR < PED → 차량 활성 = 보행 RED
//       한 쪽만 값 → 그 쪽이 활성
#define SPAT_PED_FIELD "wtPdsgRmdrCs"  // 서(西) 보행 잔여
#define SPAT_CAR_FIELD "wtStsgRmdrCs"  // 서(西) 직진(차량) 잔여
#define SPAT_PED_LABEL "서 보행"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  2. 핀 배치  (실측 PCB 스키매틱 기준)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// OLED (I2C) — J7 커넥터 (display)
//   UNO R4 WiFi 기본값: D18=SDA(=A4), D19=SCL(=A5)
//   U8g2 소프트웨어 I2C(_SW_I2C) 사용 — SCL/SDA 매크로(=A5/A4)를 비트뱅잉.
//   물리 핀은 하드웨어 I2C와 동일, 별도 핀 정의 불필요.

// 센서 입력
#define PIN_RADAR 2 // RCWL-0516 차량 감지 — J8
#define PIN_PIR 5   // HC-SR505 보행자 감지 — J11

// 입력 버튼 (전부 모멘터리 푸시, INPUT_PULLUP / 눌림 = LOW)
//   D9~D12 : OLED 모듈 내장 4 버튼 (A=D9, B=D10, C=D11, D=D12) — J7
//     A=SUBWAY, B=BUS, C=CITS, D=API↔SENSOR 토글
#define PIN_BTN_SUBWAY 9  // A → SUBWAY
#define PIN_BTN_BUS 10    // B → BUS
#define PIN_BTN_SPAT 11   // C → CITS
#define PIN_BTN_MODE 12   // D → API ↔ SENSOR 토글

// 버튼 디바운스용 — Arduino IDE 가 자동으로 함수 prototype 을 파일 최상단에 끼워
// 넣기 때문에, Btn 을 인자로 받는 함수보다 *먼저* 보이도록 헤더에 정의해 둔다.
struct Btn { uint8_t pin; bool prev; unsigned long lastMs; };
// D8 : 예비 (외부 모드 스위치 제거 → 미사용)

// 스피커 출력 (듀오벨: 저음 DAC + 고음 PWM → 하드웨어 저항 합산 → 모노 스피커) — J9
//   A0(DAC) 사인파 저음 + D6~(PWM) 사각파 고음을 '동시' 출력해 한 스피커로 믹싱.
//   UNO R4 WiFi 의 진짜 DAC 는 A0 한 개뿐 — 다른 용도로 쓰지 말 것.
#define PIN_SPK_DAC A0 // DAC 사인 출력 — 저음 (TONE_FREQ_PRIMARY)
#define PIN_SPK_PWM 6  // PWM 사각 출력 — 고음 (TONE_FREQ_SECONDARY)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  3. 동작 파라미터
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 폴링 주기 (ms) — API 호출 빈도. 너무 짧으면 일일 한도 초과
#define POLL_INTERVAL_BUS_MS                                                   \
  15000UL // 버스 (1000회/일 → 15초 = 5760회/일 → ⚠️ 30초 권장)
#define POLL_INTERVAL_SUBWAY_MS 15000UL
// 신호등: HTTPS TLS 핸드셰이크가 매 fetch 2~3 초 블로킹 → 5초마다 하면 freeze 가 자주 보임.
// 폴링 간격을 늘리고, 그 사이는 spatLiveSec() 로컬 보간이 부드럽게 카운트다운한다.
// 페이즈 전환은 다음 폴링에서 동기화됨 (최대 N초 지연 허용).
#define POLL_INTERVAL_SPAT_MS 45000UL

// 위험 판정 임계값
#define WARN_PEDESTRIAN_SEC 3.0f // 보행자 신호 잔여 N초 미만 → 경고
#define WARN_DURATION_MS 5000UL  // 위험 해소 후 N ms 유지
#define WARN_MAX_DURATION_MS                                                   \
  15000UL // 위험 시작 후 최대 N ms → 계속 감지돼도 강제 해제 (센서 재풀림 전까진 재발 억제)

// 톤 주파수 (듀오벨)
#define TONE_FREQ_PRIMARY 780    // Hz — 저음, DAC 사인 (ANC 취약점 → 이어폰 뚫음)
#define TONE_FREQ_SECONDARY 2000 // Hz — 고음, PWM 사각 (습관화 방지 → 귀 깨움)

// 경고음 펄싱 + 고음 스윕 (정적 드론 대신; 돌출/습관화 방지/in-ear ANC 회피)
//   · 짧은 on/off 펄싱이 정적음보다 잘 알아차려지고, 음악의 순간 빈틈을 더 자주 통과.
//   · 고음을 왕복 스윕하면 스펙트럼이 움직여 ANC 적응형 필터가 락온하기 어렵고
//     습관화도 방지됨. 사각파 배음(6k/10kHz)이 in-ear ANC 침투대역을 함께 커버.
#define WARN_PULSE_ON_MS   150   // 한 발 길이 (ms)
#define WARN_PULSE_OFF_MS   90   // 펄스 사이 무음 (ms)
#define WARN_HF_SWEEP        1   // 1=고음 TONE_FREQ_SECONDARY↔WARN_SWEEP_TOP_HZ 왕복, 0=고정
#define WARN_SWEEP_TOP_HZ 3500   // 고음 스윕 상단 (Hz)
#define WARN_SWEEP_MS      120   // 스윕 편도 시간 (ms)

// 디바운스
#define DEBOUNCE_MS 20
#define LONG_PRESS_MS 800

// SPAT 응답에서 "비활성/점멸"을 의미하는 센티넬 값
#define SPAT_SENTINEL 36001.0f

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  4. 하드웨어 부착 토글 (부품 도착 전 임시 비활성)
//     1 로 바꾸면 활성화. 다른 코드 수정 불필요.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#define HAS_RCWL 1  // RCWL-0516 차량 감지 모듈 (D2)
// PIR(D5), OLED(I2C), 모드 버튼(OLED 내장 D12), DuoBell 오디오(A0+D6) 는 기본 활성

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
//   북=nt 동=et 남=st 서=wt / 대각: ne se sw nw  (+ "PdsgRmdrCs")
//   ⚠️ 번동사거리(1063)는 동(et)·북(nt) 보행 신호가 null. 남(st) 사용.
#define SPAT_PED_FIELD "stPdsgRmdrCs"  // 남(南) 보행
#define SPAT_PED_LABEL "남 보행"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  2. 핀 배치  (실측 PCB 스키매틱 기준)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// OLED (I2C) — J7 커넥터 (display)
//   UNO R4 WiFi 기본값: D18=SDA(=A4), D19=SCL(=A5)
//   별도 핀 정의 불필요 (U8g2 하드웨어 I2C 사용)

// 센서 입력
#define PIN_RADAR 2 // RCWL-0516 차량 감지 — J8
#define PIN_PIR 5   // HC-SR505 보행자 감지 — J11

// 입력 버튼 (전부 모멘터리 푸시, INPUT_PULLUP / 눌림 = LOW)
//   D8  : API 카테고리 ↔ SENSOR 모드 토글 — J10
//   D9~ : API 모드 직접 선택 (OLED 모듈 내장 버튼) — J7
#define PIN_BTN_MODE 8   // API ↔ SENSOR 토글
#define PIN_BTN_BUS 9    // → BUS 모드
#define PIN_BTN_SUBWAY 10 // → SUBWAY 모드
#define PIN_BTN_SPAT 11  // → CITS 모드
// D12 : 예비 (미사용)

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
#define POLL_INTERVAL_SPAT_MS 5000UL // 신호등은 빨라야 함 (잔여 3초 감지)

// 위험 판정 임계값
#define WARN_PEDESTRIAN_SEC 3.0f // 보행자 신호 잔여 N초 미만 → 경고
#define WARN_DURATION_MS 5000UL  // 위험 해소 후 N ms 유지

// 톤 주파수 (듀오벨)
#define TONE_FREQ_PRIMARY 780    // Hz — 저음, DAC 사인 (ANC 취약점 → 이어폰 뚫음)
#define TONE_FREQ_SECONDARY 2000 // Hz — 고음, PWM 사각 (습관화 방지 → 귀 깨움)

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
// PIR(D5), OLED(I2C), 모드 스위치(D8), DuoBell 오디오(A0+D6) 는 기본 활성

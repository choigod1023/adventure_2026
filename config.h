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
#define BUS_ARS_ID "02153"  // 충무로역2번출구 (명동역 방향)

// 지하철 모니터링 역명 (한글 그대로, "역" 글자 제외)
// 예: "서울", "강남", "잠실"
#define SUBWAY_STATION "충무로"

// 지하철 역명 UTF-8 URL 인코딩 (API URL 안전 전송용)
// "수유"   = %EC%88%98%EC%9C%A0
// "서울"   = %EC%84%9C%EC%9A%B8
// "강남"   = %EA%B0%95%EB%82%A8
// "잠실"   = %EC%9E%A0%EC%8B%A4
// "교대"   = %EA%B5%90%EB%8C%80
// "충무로" = %EC%B6%A9%EB%AC%B4%EB%A1%9C
// SUBWAY_STATION 변경 시 반드시 같이 갱신할 것.
#define SUBWAY_STATION_ENC "%EC%B6%A9%EB%AC%B4%EB%A1%9C"

// 노선/방향 필터 (빈 문자열 "" 이면 필터 안 함)
//   충무로 4호선 하행(사당행-명동방면): subwayId=1004, updnLine="하행"
//   호선코드: 1호선=1001 2호선=1002 3호선=1003 4호선=1004 ...
#define SUBWAY_FILTER_LINE "1004"   // subwayId (4호선)
#define SUBWAY_FILTER_DIR  "하행"    // updnLine 부분일치 ("상행"/"하행")

// C-ITS 교차로 ID (itstId, 문자열). t-data 사이트 또는 응답의 itstId 필드에서 확인.
// ⚠️ 반드시 모니터링할 교차로 ID 를 지정할 것. "" 로 두면 임의 교차로가 잡힘.
//    "22871" = 충무로역 1번출구 앞 횡단보도 / "1063" = 번동사거리(구)
#define SPAT_ITST_ID "1063"

// OLED 에 표시할 교차로 이름 (API 응답엔 한글 이름이 없어서 직접 지정)
#define SPAT_ITST_NAME "번동사거리"

// 감시할 보행 신호 방향 — 응답 필드명 + 표시 라벨
//   북=nt 동=et 남=st 서=wt / 대각: ne se sw nw
//   접미사: PdsgRmdrCs = 보행 잔여, StsgRmdrCs = 직진(차량) 잔여
//
//   ⚠️ 충무로(22871) 방향(보행/차량 필드)은 API 500 로 미검증 — 아래는 임시값(wt).
//      API 복구 후 시리얼 [raw] 로그를 보며 nt/et/st/wt 중 보행+차량 둘 다 값 있는
//      방향으로 SPAT_PED_FIELD/CAR_FIELD/LABEL 을 맞출 것. (null 이면 "신호 없음" 표시)
//   (참고) 번동사거리(1063): 서(wt) 가 둘 다 값 있어 비교 가능했음.
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
//
//   OLED_USE_SW_I2C  0 = 하드웨어 I2C (A4=SDA, A5=SCL, Wire 페리페럴) ← 기본/추천
//                        부팅 시 I2C 스캔으로 OLED(0x3C) 감지 여부를 시리얼에 찍는다.
//                        스캔이 아무것도 못 찾으면 → A4/A5 핀/결선 사망 → 아래 1 로.
//                    1 = 소프트웨어 I2C (임의 핀 비트뱅잉) — A4/A5 손상 시 폴백.
//                        OLED 의 SCL/SDA 선을 아래 핀(D8/D7)으로 옮겨 꽂을 것.
#define OLED_USE_SW_I2C 1
#define OLED_SW_SCL_PIN 8   // 소프트 I2C 일 때 SCL — OLED SCL → D8
#define OLED_SW_SDA_PIN 7   // 소프트 I2C 일 때 SDA — OLED SDA → D7

// 센서 입력
#define PIN_RADAR 2 // RCWL-0516 차량 감지 — J8
#define PIN_PIR 5   // HC-SR505 보행자 감지 — J11

// 입력 버튼 (전부 모멘터리 푸시, INPUT_PULLUP / 눌림 = LOW) — J7
//   현재 실측 배선: A=D13, B=D11, C=D10, D=D9
//     A=SUBWAY, B=BUS, C=CITS, D=API↔SENSOR 토글
//   ⚠️ D13 은 온보드 LED(LED_BUILTIN) 핀 — 버튼으로 쓰면 LED 회로 영향으로 INPUT_PULLUP
//      읽기가 불안정할 수 있음. 증상 있으면 외부 풀업/다른 핀(D12 비었음) 고려.
//   D8 = OLED SW I2C(SCL) 로 사용 중, D12 = 비어있음(예비).
#define PIN_BTN_SUBWAY 13  // A → SUBWAY
#define PIN_BTN_BUS 11    // B → BUS
#define PIN_BTN_SPAT 10   // C → CITS
#define PIN_BTN_MODE 9   // D → API ↔ SENSOR 토글

// 버튼 디바운스용 — Arduino IDE 가 자동으로 함수 prototype 을 파일 최상단에 끼워
// 넣기 때문에, Btn 을 인자로 받는 함수보다 *먼저* 보이도록 헤더에 정의해 둔다.
struct Btn { uint8_t pin; bool prev; unsigned long lastMs; };
// D12 : 비어있음(예비) / D8 : OLED SW I2C(SCL) 로 사용

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
#define WARN_HF_SWEEP        1   // (참고) 고음 스윕 플래그 — 듀티-PWM 음량 경로에선 미적용(고정 2kHz)
#define WARN_SWEEP_TOP_HZ 3500   // 고음 스윕 상단 (Hz)
#define WARN_SWEEP_MS      120   // 스윕 편도 시간 (ms)

// ── 시간대 음량 스케줄 (NTP→KST 시각 기준) ──────────────────────────
//   낮 = 교통소음 마스킹(Salford 보고서: masker 지배 영역 → 더 크게)으로 크게,
//   밤 = 민원 회피로 작게. 값은 '소프트 신호레벨' 0~1.
//   ⚠️ 절대 SPL/고막안전은 SW 로 보장 불가 → 설치 시 PAM8302 트리머(노즐)로 최종 캘리브레이션.
//      (소프트 음량) × (앰프 노즐 게인) = 실제 음량 → 노즐 수동조절도 그대로 동작.
#define VOL_DAY_START_MIN  (9 * 60)        // 낮 시작 09:00
#define VOL_DAY_END_MIN    (17 * 60 + 30)  // 낮 끝   17:30
#define VOL_DAY            0.85f           // 낮 음량 (크게, 헤드룸 남김)
#define VOL_NIGHT          0.30f           // 밤 음량 (민원 회피)
#define VOL_MAX            0.90f           // 소프트 상한 (클리핑/안전 헤드룸)
#define VOL_PRESET_SUBWAY  0.50f           // 지하철 (실내, 울림 소음 고려해 작게)
#define VOL_PRESET_BUS     0.75f           // 버스 정류장 (반실외 소음)
#define VOL_PRESET_SPAT    1.00f           // 횡단보도 (차도 인접, 최대 볼륨)
#define VOL_PRESET_SENSOR  0.80f           // 골목/센서 (기본 소음 대비 중간 볼륨)
#define NTP_TZ_OFFSET_SEC  (9 * 3600)      // KST = UTC+9

// ── 웹 디스플레이 push (Vercel Next.js /api/status) ─────────────────
//   1 로 켜면 평가 결과를 큰 화면 웹앱(web/)으로 POST. Vercel 배포 URL 을 호스트에 넣을 것.
//   HTTPS(443) 라 push 1회당 TLS 핸드셰이크가 블로킹(~1~2s) → 간격 넉넉히(>=3s).
#define ENABLE_WEB_PUSH          1
#define WEB_PUSH_HOST            "adventure-2026-web.vercel.app"  // Vercel 배포 도메인 (https, 443)
#define WEB_PUSH_PATH            "/api/status"
#define WEB_PUSH_MIN_INTERVAL_MS 10000UL  // 내용 변화 없을 때 하트비트 주기(블로킹 잦으면 OLED 끊김 → 길게)

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

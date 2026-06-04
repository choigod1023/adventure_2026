#line 1 "/Users/jangjunhyeok/Documents/Arduino/adventure_2026/API_GUIDE.md"
# BionicGPT - 외부 API 사용 가이드

## 프로젝트 개요

스몸비(보행자) 안전 장치. 횡단보도/대중교통 외부 신호를 수신하여 위험 상황을 음향(780Hz/2000Hz)으로 경고.

**MCU**: Arduino UNO R4 WiFi (또는 ESP32) — 둘 다 WiFi 내장이라 직접 HTTP 호출 가능
**제어 모드**: **버튼 클릭으로 4개 모드 순환 전환**
  1. 버스 도착 API 모드
  2. 지하철 도착 API 모드
  3. 신호등 C-ITS API 모드 (신호등 있는 환경)
  4. 센서 의존 모드 — RCWL(차량) + SR505(인체) (신호등 없는 환경)

---

## 사용할 API 3종 요약

| # | API | 출처 | 용도 | 인증 |
|---|---|---|---|---|
| 1 | 서울특별시 버스도착정보조회 | data.go.kr | 버스 도착 임박 시 보행자에게 경고 | serviceKey |
| 2 | 서울시 지하철 실시간 도착정보 | data.seoul.go.kr | 지하철 도착 시 환승 경로 위험 알림 | API_KEY |
| 3 | 서울 교통빅데이터 (data_id=10120) | t-data.seoul.go.kr | 추가 교통/C-ITS 데이터 (회원가입 후 확인 필요) | API_KEY |

---

## API 1. 서울특별시 정류소정보조회 (정류장 기준)

**원문**: https://www.data.go.kr/data/15000303/openapi.do
**오퍼레이션**: `getStationByUid` — 정류장 ID로 그 정류장에 들어오는 **모든 버스의 실시간 도착정보**

> ⚠️ 본 프로젝트는 "특정 정류장에 들어오는 모든 버스"를 봐야 하므로 노선 기준(15000314)이 아닌 **정류소 기준(15000303)** 을 사용. 기존 노선 기준은 한 노선 전체의 정류장을 봐야 해서 부적합.

### 기본 정보
- **엔드포인트**: `http://ws.bus.go.kr/api/rest/stationinfo/getStationByUid`
- **제공기관**: 서울특별시 미래첨단교통과
- **응답 형식**: XML (HTTP, HTTPS 아님)
- **개발계정 트래픽**: 1,000회/일

### 인증키 발급
1. https://www.data.go.kr 가입 (이미 완료)
2. 데이터셋 15000303 페이지에서 **[활용신청]** 한 번 더 클릭 → 자동승인
3. **serviceKey는 기존 BUS_API_KEY 그대로 사용** (계정당 1개 키)

### 요청 파라미터
| 파라미터 | 필수 | 설명 | 예시 |
|---|---|---|---|
| `serviceKey` | O | 발급받은 인증키 (URL 인코딩된 그대로) | `vXvsHQ...%3D%3D` |
| `arsId` | O | **정류장 ARS 번호 (5자리)** | `23282` |

### 정류장 ARS ID 찾는 법
1. 정류장 표지판에 **5자리 숫자**가 적혀 있음 (예: `23-282` → `23282`)
2. 또는 https://bus.go.kr 에서 정류장 검색
3. 또는 `getStationByName` 오퍼레이션으로 이름 검색

### 호출 예시
```
http://ws.bus.go.kr/api/rest/stationinfo/getStationByUid
  ?serviceKey=YOUR_KEY
  &arsId=23282
```

### 응답 구조 (XML)
```xml
<ServiceResult>
  <msgBody>
    <itemList>                          <!-- 버스 1대당 1 블록 -->
      <stNm>서울대입구역</stNm>
      <arsId>23282</arsId>
      <rtNm>406</rtNm>                  <!-- 노선번호 (사용자에게 보이는 이름) -->
      <busRouteId>100100118</busRouteId>
      <arrmsg1>2분30초후[3번째 전]</arrmsg1>   <!-- 첫번째 차량 도착 -->
      <arrmsg2>10분후[8번째 전]</arrmsg2>      <!-- 두번째 차량 도착 -->
      ...
    </itemList>
    <itemList>...</itemList>
    ...
  </msgBody>
</ServiceResult>
```

### 주요 응답 필드
| 필드 | 의미 | 활용 |
|---|---|---|
| `stNm` | 정류장 이름 | OLED 표시 |
| `rtNm` | 노선번호 (예: "406", "마을02") | OLED 표시 |
| `arrmsg1` | 첫번째 도착 메시지 | **위험 판정** |
| `arrmsg2` | 두번째 도착 메시지 | (선택) |
| `busRouteId` | 노선 내부 ID | (선택) |

### 위험 트리거 조건 (회의록 기준)
- `arrmsg1` 에 **"곧 도착"** 포함
- 또는 `arrmsg1` 이 **"출발"** 로 시작 ("출발대기" 제외)
- 또는 첫 단어가 **"0분"** / **"1분"**

### 아두이노 사용 흐름
```
1. WiFi 연결
2. HTTP GET → getStationByUid
3. XML 스트림 파싱:
   - <itemList> 블록 단위로 순회
   - 각 블록에서 rtNm + arrmsg1 추출
4. arrmsg1 위험 패턴 일치 시 → 경고 트리거
```

---

## API 2. 서울시 지하철 실시간 도착정보

**원문**: https://data.seoul.go.kr/dataList/OA-12764/A/1/datasetView.do

### 기본 정보
- **엔드포인트**: `http://swopenAPI.seoul.go.kr/api/subway/{KEY}/{TYPE}/realtimeStationArrival/{START}/{END}/{STATION_NAME}`
- **제공기관**: 서울시 TOPIS
- **응답 형식**: XML 또는 JSON (URL의 `{TYPE}` 으로 선택)
- **연락처**: 02-2133-4969 (문서 페이지 명시)

### 인증키 발급
1. https://data.seoul.go.kr 회원가입
2. 마이페이지 → **[인증키 발급]** → API 신청
3. 받은 KEY 사용 (별도 승인 없이 즉시 사용)

### URL 파라미터 (Path 방식)
| 파라미터 | 위치 | 설명 | 예시 |
|---|---|---|---|
| `{KEY}` | path | 발급키 | `sample` (테스트용) |
| `{TYPE}` | path | 응답 포맷 | `json` 또는 `xml` |
| `{START}` | path | 시작 인덱스 | `0` |
| `{END}` | path | 끝 인덱스 (max 5) | `5` |
| `{STATION_NAME}` | path | 역명 (UTF-8) | `서울`, `강남` |

### 호출 예시 (테스트키)
```
http://swopenAPI.seoul.go.kr/api/subway/sample/json/realtimeStationArrival/0/5/서울
```

> **주의**: 한글 역명은 반드시 **URL 인코딩** 필요. 아두이노에서는 미리 인코딩된 문자열을 상수로 박아두는 게 편함.

### 응답 주요 필드 (JSON)
| 필드 | 의미 |
|---|---|
| `subwayId` | 지하철 호선 코드 |
| `trainLineNm` | 행선지/방면 |
| `arvlMsg2` | 도착 메시지 ("전역 출발", "곧 도착") |
| `arvlMsg3` | 현재 위치 |
| `barvlDt` | 도착 예정 시간(초) |
| `bstatnNm` | 종착역 |

### 아두이노 사용 흐름
```
1. ArduinoJson 라이브러리 설치
2. WiFi 연결 후 GET 요청
3. JSON 파싱 → arvlMsg2 검사
4. "곧 도착" 또는 barvlDt < 30 → 위험 모드 트리거
```

---

## API 3. 서울시 신호제어기 잔여시간 (C-ITS SPAT)

**원문**: https://t-data.seoul.go.kr/dataprovide/trafficdataviewopenapi.do?data_id=10120
**정식명**: `v2xSignalPhaseTimingInformation` (V2X Signal Phase and Timing)

### 기본 정보
- **엔드포인트**: `https://t-data.seoul.go.kr/apig/apiman-gateway/tapi/v2xSignalPhaseTimingInformation/1.0`
- **프로토콜**: **HTTPS 필수** (포트 443, `WiFiSSLClient` 사용)
- **제공기관**: 서울시 교통빅데이터 플랫폼
- **응답 형식**: JSON (`type=json` 지정 시) — 최상위가 **JSON 배열** `[{...}, ...]`
- **인증 방식**: 쿼리 파라미터 `apikey` (⚠️ **소문자**, 대문자 `apiKey` 안 됨)

### 인증키 발급
1. https://t-data.seoul.go.kr 회원가입 + 로그인
2. 마이페이지 → API 키 발급 (UUID 형식: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`)

### 요청 파라미터 ⭐
| 파라미터 | 필수 | 설명 | 비고 |
|---|---|---|---|
| `apikey` | O | 발급키 (소문자!) | UUID |
| `type` | 권장 | `json` 또는 `xml` | 아두이노는 `json` |
| `pageNo` | **필수에 가까움** | 페이지 번호 | `1` |
| `numOfRows` | **필수에 가까움** | 페이지당 row 수 | **`1` 또는 매우 작게** |
| `itstId` | 선택 | 교차로 ID 필터 | 빈 값 = 전체 |

> ⚠️ **메모리 경고 (실측됨)**: `numOfRows`를 안 주거나 크게 주면 응답이 수십 KB가 되어 **UNO R4의 SRAM 32KB를 초과**합니다. **반드시 `pageNo=1&numOfRows=1` 로 한 row만 받아오세요.** 이게 안 되면 보드가 리부트하거나 JSON 파싱이 깨집니다. (실측 코드에 명시된 사항)

### 호출 예시
```
https://t-data.seoul.go.kr/apig/apiman-gateway/tapi/v2xSignalPhaseTimingInformation/1.0
  ?apikey=2a06fabb-fd26-47bf-a096-c06afdcd0a97
  &type=json
  &pageNo=1
  &numOfRows=1
```

### 응답 필드 (각 item)
| 필드 | 의미 |
|---|---|
| `itstId` | 교차로 ID |
| `eqmnId` | 장비 ID |
| `trsmYear` / `trsmMt` / `trsmDy` / `trsmTm` | 전송 시각 (년/월/일/시각) |
| `{방향}{신호}RmdrCs` | **잔여시간 (센티초, 1/10초 단위)** |

**방향 prefix**: `nt`(북) `et`(동) `st`(남) `wt`(서) `ne` `se` `sw` `nw`(대각)
**신호 type**: `Stsg`(직진) `Ltsg`(좌회전) `Pdsg`(보행)
**suffix**: `RmdrCs` (잔여 센티초)

예: `ntPdsgRmdrCs` = 북측 **보행자 신호 잔여 센티초**

### 특수 값 처리
| 값 | 의미 |
|---|---|
| `null` | 해당 신호 없음 |
| `36001.0` (`SENTINEL`) | **신호 미운영 / 점멸 모드** |
| 그 외 숫자 | `÷10` = 잔여 초 |

### 본 프로젝트에서 활용 포인트 ⭐
회의록의 "**스몸비 안전**" 목적에 정확히 부합:

```cpp
// 보행자 신호 잔여시간이 짧으면(예: 3초 이내) → 빠른 경고
float ntPdsg = item["ntPdsgRmdrCs"] | 36001.0f;
if (ntPdsg < 36001.0f && (ntPdsg / 10.0f) < 3.0f) {
    triggerWarning();  // 780Hz + 2kHz 출력
}
```

→ 차량/지하철 도착 API와 함께 **3중 감지** 가능:
1. 보행자가 무단횡단 직전 (PIR + RCWL)
2. 차량/열차 접근 (RCWL + 지하철 API)
3. **신호등 곧 바뀜** (이 API)

### 실측 동작 확인된 사항 (사용자 제공 코드 기준)
- ✅ `WiFiSSLClient` + `ArduinoHttpClient` + `ArduinoJson v7` 조합
- ✅ **스트림 직접 파싱** (`deserializeJson(doc, http)`) — String 버퍼 안 만듦 → 메모리 절약
- ✅ 10초 폴링 주기 안정 동작
- ✅ `apikey` 소문자가 정답 (대문자는 실패)

전체 동작 코드: `adventure_2026.ino` 또는 별도 참조 코드 확인.

---

## 통합 아키텍처

```
                  ┌─────────────────────────────┐
                  │   UNO R4 WiFi (메인 MCU)    │
                  └────────────┬────────────────┘
                               │
        ┌──────────────────────┼────────────────────────┐
        │                      │                        │
   [입력 - 센서]          [입력 - API]            [출력 - 음향/표시]
        │                      │                        │
   ┌────┴────┐         ┌───────┴────────┐       ┌───────┴──────┐
   │ HC-SR505│         │ WiFi → 3개 API │       │ PAM8610 앰프 │
   │ (PIR)   │         │  - 버스 도착   │       │  ├ SPK1: 780Hz│
   │ RCWL-   │         │  - 지하철 도착 │       │  └ SPK2: 2kHz │
   │ 0516    │         │  - C-ITS(10120)│       │              │
   │ (레이다)│         └────────────────┘       │ SSD1306 OLED │
   └─────────┘                                   │  + 4 버튼    │
                                                 └──────────────┘
        ↑
   [BTN_MODE] 누를 때마다 다음 모드로 순환
   ① 버스 API → ② 지하철 API → ③ C-ITS API → ④ 센서 모드 → ①…
```

### 모드별 동작 로직 (4개 모드)

| # | 모드 | 데이터 소스 | 위험 트리거 조건 | OLED 표시 |
|---|---|---|---|---|
| ① | **버스 API** | `getStationByUid` (BUS_ARS_ID) | `arrmsg1` 에 "곧 도착" 또는 "출발" 또는 "0분/1분" | "🚌 노선 N번 곧 도착" |
| ② | **지하철 API** | `realtimeStationArrival` (SUBWAY_STATION) | `arvlMsg2`="곧 도착" 또는 `barvlDt`<30s | "🚇 N호선 곧 도착" |
| ③ | **C-ITS API** | `v2xSignalPhaseTimingInformation` (SPAT_ITST_ID) | 보행자 신호 잔여 < `WARN_PEDESTRIAN_SEC` (3초) | "🚦 신호 N초 남음" |
| ④ | **센서 모드** | RCWL-0516 + HC-SR505 (오프라인) | PIR 게이팅 통과한 RCWL 차량 감지 | "⚠ 차량 접근" |

#### 모드 ④ 센서 모드 상세 (신호등 없는 환경 전용)

본 모드는 **신호등이 없는 도로 / 골목 / 주차장 출입구** 등에서 사용. API 없이 100% 오프라인 동작.

- **RCWL-0516**: 마이크로파 도플러로 움직임 감지 → 차량 후보
- **HC-SR505 (PIR)**: 인체 적외선 감지 → 사용자 본인 움직임 식별 (게이팅에 사용)
- **위험 판정**: PIR 게이팅으로 사용자 본인 움직임 배제한 RCWL 트리거 = 차량 확정
- 자세한 PIR 게이팅 로직은 [`NOTES_RCWL_FALSEPOSITIVE.md`](./NOTES_RCWL_FALSEPOSITIVE.md) 참조

#### 공통

- 모든 모드에서 위험 감지 시: **780Hz + 2kHz 동시 출력 + OLED 빨강 표시**
- 모드 ①~③에서 WiFi 끊김 시: 자동으로 ④ 센서 모드 폴백 (OLED에 "OFFLINE → SENSOR" 표시)
- BTN_PAGE (별도 버튼): 모드 내부 페이지 전환 (디버그/볼륨/상태 등)

---

## 다음 단계 (코딩 순서 제안)

1. **WiFi + OLED 기본 셋업** (`adventure_2026.ino`)
   - U8G2 초기화, WiFi 연결 확인 화면
2. **버스 API 호출 모듈** (`bus_api.h/.cpp`)
   - 노선 ID 하드코딩으로 시작 → 응답 파싱
3. **지하철 API 호출 모듈** (`subway_api.h/.cpp`)
   - ArduinoJson 사용
4. **C-ITS 모듈** (data_id=10120 정보 확보 후)
5. **OLED 내장 4버튼 처리** (버튼 D=모드 토글 + BUS/SUBWAY/CITS 선택)
6. **PAM8610 톤 출력** (`tone()` 함수 또는 DAC)
7. **센서 통합** (실물 도착 후)

---

## 필요한 아두이노 라이브러리

| 라이브러리 | 용도 | 설치 |
|---|---|---|
| `WiFiS3` | UNO R4 WiFi 연결 | 보드 패키지 포함 |
| `ArduinoHttpClient` | HTTPS GET (스트림 파싱 지원) | 라이브러리 매니저 |
| `ArduinoJson` (**v7**) | JSON 파싱 | 라이브러리 매니저 (Benoit Blanchon) |
| `U8g2lib` | OLED | 라이브러리 매니저 |
| (XML 처리) | 버스 API는 XML — `indexOf()` / `substring()` 으로 간단 처리 권장 | - |

> **HTTPS 주의**: 3개 API 중 t-data만 HTTPS 필수, 나머지 둘은 HTTP. 클라이언트 객체를 두 종류 만들어야 함 (`WiFiSSLClient` + `WiFiClient`).

---

## 미해결/대기 항목

- [x] ~~data_id=10120 정체 확인~~ → **C-ITS SPAT 신호 잔여시간** 확정
- [ ] 노선 ID `busRouteId` 어떤 버스 사용할지 결정 (장치 설치 위치 기준)
- [ ] 지하철 호출 시 어느 역 모니터링할지 결정
- [ ] 모니터링할 교차로 `itstId` 결정 (전체 받기엔 메모리 부족)
- [ ] 인증키 3종 `secrets.h` 분리 + `.gitignore` 등록

# Claude Code Arduino Notifier

Claude Code가 명령을 실행하거나 작업을 완료할 때, **ESP32에 연결된 컬러 TFT 디스플레이**에 메시지를 표시하고 **조이스틱**으로 스크롤 확인 후 승인하는 임베디드 연동 시스템.  
조이스틱을 좌우로 기울이면 GIF 재생 모드로 전환되고, 알림이 오면 자동으로 Claude 모드로 돌아온다.

```
Claude Code가 Bash 실행 직전
           ↓
┌────────────────────────────────┐  320×240 TFT (가로)
│▓▓▓▓▓▓[ Claude ]  CONFIRM▓▓▓▓▓│  ← 주황 헤더
│────────────────────────────────│
│ [이름]님! 이거 실행해도      ^ │  ← ^ = 위에 더 있음
│ 될까요~?                     v │  ← v = 아래 더 있음
│ $ git commit -m "feat: 로그인  │
│ 화면 UI 추가" && npm run build │
│ ...                            │
│────────────────────────────────│
│ [SW] Confirm        JOY scroll │
└────────────────────────────────┘
           ↓
   조이스틱 SW 버튼 → 실행 진행
   (타임아웃 없음 — 버튼 누를 때까지 대기)
```

### 조이스틱 동작
| 입력 | Claude 모드 | GIF 재생 모드 | GIF 목록 |
|------|------------|--------------|---------|
| ↑↓ 기울이기 | 텍스트 스크롤 | — | 목록 탐색 |
| ←→ 기울이기 | GIF 모드 전환 | Claude 모드 복귀 | 취소·재생 복귀 |
| SW 버튼 | CONFIRM 승인 | GIF 목록 열기 | GIF 선택·재생 |
| SW 유지 0.4초 | — | — | 자동 반복 스크롤 |

---

## 목차

1. [필요 부품](#1-필요-부품)
2. [하드웨어 배선](#2-하드웨어-배선)
3. [Arduino IDE 설정](#3-arduino-ide-설정)
4. [Python 환경 설정](#4-python-환경-설정)
5. [Claude Code hooks 연동](#5-claude-code-hooks-연동) ([OLED 표시 예시](#5-4-oled-표시-예시))
6. [테스트](#6-테스트)
7. [아두이노 미연결 시 동작](#7-아두이노-미연결-시-동작)
8. [트러블슈팅](#8-트러블슈팅)

---

## 1. 필요 부품

| 부품명 | 수량 | 용도 |
|--------|------|------|
| ESP32 개발 보드 (DevKit v1 등) | 1 | 메인 컨트롤러 |
| TFT LCD 240×320 SPI (ST7789) | 1 | 컬러 메시지 출력 + GIF 재생 |
| 아날로그 조이스틱 모듈 (KY-023) | 1 | 스크롤 + GIF 모드 전환 + 승인 |
| 브레드보드 | 1 | 부품 연결 |
| 점퍼 와이어 | 12줄 | 배선 |
| USB Micro-B 케이블 | 1 | PC ↔ ESP32 연결 |

### TFT 디스플레이 (ST7789) 핀 구성

| 핀 | 설명 |
|----|------|
| VCC | 3.3V 전원 |
| GND | 그라운드 |
| SCL | SPI 클럭 |
| SDA (MOSI) | SPI 데이터 |
| RES (RST) | 리셋 |
| DC | 데이터/명령 선택 |
| BLK | 백라이트 (3.3V 직결) |

### 조이스틱 모듈 (KY-023) 핀 구성

| 핀 | 설명 |
|----|------|
| GND | 그라운드 |
| +5V | 전원 |
| VRx | X축 아날로그 → 모드 전환 (←→) |
| VRy | Y축 아날로그 → 스크롤 (↑↓) |
| SW | 버튼 → CONFIRM 승인 / GIF 목록 |

---

## 2. 하드웨어 배선

### 배선 다이어그램

```
ESP32 DevKit
┌──────────────────────────────────────────┐
│                                          │
│  3.3V ─────────────────── VCC ───────────┼──→ TFT VCC
│  GND  ─────────────────── GND ───────────┼──→ TFT GND
│  GPIO18 ────────────────── SCL ───────────┼──→ TFT SCL
│  GPIO23 ────────────────── SDA ───────────┼──→ TFT SDA (MOSI)
│  GPIO15 ────────────────── CS  ───────────┼──→ TFT CS
│  GPIO2  ────────────────── DC  ───────────┼──→ TFT DC
│  GPIO4  ────────────────── RST ───────────┼──→ TFT RST
│  3.3V ─────────────────── BLK ───────────┼──→ TFT BLK (백라이트)
│                                          │
│  3.3V ─────────────────── +5V ───────────┼──→ 조이스틱 전원
│  GND  ─────────────────── GND ───────────┼──→ 조이스틱 GND
│  GPIO34 ───────────────── VRy ───────────┼──→ 조이스틱 Y축 (↑↓)
│  GPIO35 ───────────────── VRx ───────────┼──→ 조이스틱 X축 (←→)
│  GPIO32 ───────────────── SW  ───────────┼──→ 조이스틱 버튼
│         (INPUT_PULLUP 내부 풀업)          │
└──────────────────────────────────────────┘
```

### TFT 배선 상세

| ESP32 핀 | TFT 핀 | 설명 |
|----------|--------|------|
| 3.3V | VCC | 전원 |
| GND | GND | 그라운드 |
| GPIO18 | SCL | SPI 클럭 |
| GPIO23 | SDA (MOSI) | SPI 데이터 |
| GPIO15 | CS | Chip Select |
| GPIO2 | DC | 데이터/명령 |
| GPIO4 | RST | 리셋 |
| 3.3V | BLK | 백라이트 (항상 ON) |

### 조이스틱 배선 상세

| ESP32 핀 | 조이스틱 핀 | 설명 |
|----------|------------|------|
| 3.3V | +5V | 전원 |
| GND | GND | 그라운드 |
| GPIO34 | VRy | Y축 아날로그 (↑↓ 스크롤) |
| GPIO35 | VRx | X축 아날로그 (←→ 모드 전환) |
| GPIO32 | SW | 버튼 (INPUT_PULLUP) |

> **GPIO34/35 주의:** 입력 전용 핀 — ADC 사용에 최적, 풀업 저항 없음. 조이스틱 +5V를 3.3V에 연결해 전압 초과 방지.

> **조이스틱 방향 주의:** 위로 기울였을 때 VRy 값이 0에 가까워야 "위 스크롤"로 동작.  
> 반대면 조이스틱을 180도 돌리거나 펌웨어의 `JOY_LOW` / `JOY_HIGH` 조건을 바꾸세요.

---

## 3. Arduino IDE 설정

### 3-1. ESP32 보드 패키지 설치

Arduino IDE **파일 → 환경설정 → 추가 보드 관리자 URL** 에 추가:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
그 다음 **툴 → 보드 → 보드 관리자** 에서 `esp32 by Espressif` 설치.

### 3-2. 라이브러리 설치

Arduino IDE **스케치 → 라이브러리 포함하기 → 라이브러리 관리** 에서:

| 검색어 | 설치할 라이브러리 | 용도 |
|--------|-----------------|------|
| `TFT_eSPI` | TFT_eSPI by Bodmer | ST7789 SPI 디스플레이 드라이버 |
| `AnimatedGIF` | AnimatedGIF by Larry Bank | GIF 재생 |

### 3-3. TFT_eSPI 핀 설정 (필수)

TFT_eSPI 라이브러리는 설치 후 **핀 번호를 직접 수정**해야 동작한다.

1. `arduino/TFT_eSPI_User_Setup.h` 파일 내용 확인
2. Arduino 라이브러리 폴더의 `TFT_eSPI/User_Setup.h` 파일을 열어 해당 내용으로 교체

라이브러리 폴더 위치:
- **Windows:** `C:\Users\[이름]\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`
- **macOS:** `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`

핵심 설정 요약:
```cpp
#define ST7789_DRIVER
#define TFT_MOSI 23   #define TFT_SCLK 18
#define TFT_CS   15   #define TFT_DC    2   #define TFT_RST   4
#define SMOOTH_FONT   // VLW 한글 폰트에 필요
```

### 3-4. 한글 VLW 폰트 생성 (선택)

폰트가 없으면 ASCII로만 표시됨. 한글 표시를 원하면 아래 과정으로 `korean.vlw` 생성:

1. **Processing IDE** 설치 (processing.org)
2. Processing에서 **툴 → Create Font** 실행
3. 한글 TTF 폰트 선택 (예: Noto Sans KR), 크기 **16**, 체크박스 **All characters** → OK
4. 생성된 `.vlw` 파일을 `korean.vlw`로 이름 변경
5. `arduino/firmware/data/` 폴더에 복사

### 3-5. SPIFFS 파일 업로드

GIF 파일과 `korean.vlw`를 ESP32 내부 파일시스템에 올리는 과정:

1. **ESP32 Sketch Data Upload 플러그인** 설치
   - Arduino IDE 1.x: [github.com/me-no-dev/arduino-esp32fs-plugin](https://github.com/me-no-dev/arduino-esp32fs-plugin)
   - Arduino IDE 2.x: [github.com/earlephilhower/arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload) (LittleFS 버전)
2. 원하는 GIF 파일을 `arduino/firmware/data/` 폴더에 복사
   - SPIFFS 전체 용량 ~3MB → 작은 GIF 수십 개 가능
   - 권장 크기: 320×240 이하, 파일당 500KB 이하
3. `arduino/firmware/claude_lcd.ino` 열린 상태에서 **툴 → ESP32 Sketch Data Upload** 실행

### 3-6. 펌웨어 업로드

1. `arduino/firmware/claude_lcd.ino` 열기
2. **툴 → 보드 → ESP32 Arduino → ESP32 Dev Module** 선택
3. **툴 → 포트** → ESP32 포트 선택
4. 업로드 버튼 클릭

### 3-7. 조이스틱 방향 보정

시리얼 모니터로 값 확인 후 필요하면 펌웨어 상단 수정:

```cpp
#define JOY_LOW  200   // 이 값 미만 = 위/좌 방향
#define JOY_HIGH 800   // 이 값 초과 = 아래/우 방향
```

중립 상태에서 VRy/VRx 값은 보통 1800~2000 (ESP32 ADC 12비트, 0~4095 범위).

---

## 4. Python 환경 설정

### 4-1. 가상환경 생성 및 활성화

```bash
python -m venv venv

# Windows
venv\Scripts\activate

# macOS / Linux
source venv/bin/activate
```

### 4-2. 의존성 설치

```bash
pip install -r requirements.txt
```

---

## 5. Claude Code hooks 연동

### 5-1. settings.json 위치

| 위치 | 효과 |
|------|------|
| `<프로젝트>/.claude/settings.json` | 해당 프로젝트에서만 적용 |
| `~/.claude/settings.json` | 모든 프로젝트에 전역 적용 |

### 5-2. 설정 방법

`claude-hooks/settings.json`을 열고, `/ABSOLUTE/PATH/TO/bridge/arduino_bridge.py` 부분을 실제 경로로 교체:

**Windows 예시:**
```json
"command": "python \"C:\\Users\\YourName\\Arduino_claudeNotifier\\bridge\\arduino_bridge.py\" PreToolUse"
```

**macOS / Linux 예시:**
```json
"command": "python3 \"/home/yourname/Arduino_claudeNotifier/bridge/arduino_bridge.py\" PreToolUse"
```

### 5-3. 훅별 동작

| 훅 | 발생 시점 | OLED 동작 | 대기 여부 |
|----|----------|----------|---------|
| `PreToolUse` | Bash/파일 수정 직전 | 전체 명령어 스크롤 표시 | 조이스틱 SW / Enter 대기 |
| `PostToolUse` | 작업 직후 | 완료 메시지 | 3초 표시 후 자동 |
| `Notification` | Claude가 입력 대기 시 | 알림 메시지 | 3초 표시 후 자동 |
| `Stop` | Claude 응답 완료 시 | 완료 메시지 | 3초 표시 후 자동 |

### 5-4. TFT 표시 예시

> `.env`의 `USER_NAME` 값이 `[이름]`에 표시됨 (기본값: `주인님`)  
> 화면: 320×240 가로, 최대 ~8줄 동시 표시

#### PreToolUse — Bash 명령 실행 전 (긴 명령, 스크롤)

```
┌────────────────────────────────┐  ← 첫 화면
│▓▓▓▓[ Claude ]      CONFIRM▓▓▓│  ← 주황 헤더
│────────────────────────────────│
│ [이름]님! 이거 실행해도      v │  ← v = 아래 더 있음
│ 될까요~?                       │
│ $ git commit -m "feat: 로그인  │
│ 화면 UI 추가" && npm run build │
│ ...                            │
│────────────────────────────────│
│ [SW] Confirm        JOY scroll │
└────────────────────────────────┘

┌────────────────────────────────┐  ← 아래로 스크롤
│▓▓▓▓[ Claude ]      CONFIRM▓▓▓│
│────────────────────────────────│
│ ...                          ^ │  ← ^ = 위에 더 있음
│ && echo done                   │  ← v 없음 = 마지막 줄
│                                │
│────────────────────────────────│
│ [SW] Confirm        JOY scroll │
└────────────────────────────────┘
```

SW 버튼 또는 키보드 Enter를 누를 때까지 **무한 대기**.

#### PreToolUse — 파일 수정 전

```
┌────────────────────────────────┐
│▓▓▓▓[ Claude ]      CONFIRM▓▓▓│
│────────────────────────────────│
│ [이름]님! 파일 수정할게요~     │
│ src/components/LoginScreen.tsx │
│────────────────────────────────│
│ [SW] Confirm        JOY scroll │
└────────────────────────────────┘
```

#### PostToolUse — 완료 알림

```
┌────────────────────────────────┐
│████[ Claude ]       NOTIFY████│  ← 초록 헤더
│────────────────────────────────│
│ 실행 완료!                     │
│ 잘 됐어요~                     │
│                                │
│────────────────────────────────│
│ auto 3s                        │
└────────────────────────────────┘
```

3초 후 자동으로 대기 화면 또는 GIF 모드로 복귀.

#### Stop — 응답 완료

```
┌────────────────────────────────┐
│████[ Claude ]       NOTIFY████│
│────────────────────────────────│
│ [이름]님!                      │
│ 다 끝났어요~ 수고!             │
│                                │
│────────────────────────────────│
│ auto 3s                        │
└────────────────────────────────┘
```

#### GIF 재생 모드 (조이스틱 ←→)

```
┌────────────────────────────────┐
│                                │
│      (GIF 전체화면 재생)        │
│         320×240 컬러           │
│                                │
│   SW 버튼 → 목록 열기          │
└────────────────────────────────┘
```

#### GIF 목록 화면 (GIF 모드에서 SW)

```
┌────────────────────────────────┐
│▓▓▓[ GIF List ]           1/3▓▓│
│────────────────────────────────│
│ ██ cat                         │  ← 선택 중 (파랑 하이라이트)
│    wiggle                      │
│    heart                       │
│────────────────────────────────│
│ [SW] Select  [JOY] Scroll      │
└────────────────────────────────┘
```

---

## 6. 테스트

### 단계 1: ESP32 시리얼 모니터 테스트

Arduino IDE **시리얼 모니터** (속도: 9600, **줄 끝: Newline**)에서 직접 입력:

```
CONFIRM:[이름]님! 이거 실행해도 될까요~?|$ git commit -m "feat: 로그인 화면 UI 추가" && npm run build
```
→ TFT에 컬러 CONFIRM 화면 표시. 조이스틱으로 스크롤, SW 누르면 `OK` 출력

```
NOTIFY:실행 완료!|잘 됐어요~
```
→ 초록 NOTIFY 화면, 3초 후 자동 복귀

```
CLEAR
```
→ `Waiting for Claude Code...` 대기 화면

> **프로토콜 참고:** `|`는 줄 구분자. 실제 `\n`(줄바꿈)을 시리얼로 보내면 명령이 잘리므로 반드시 `|` 사용.

### 단계 2: Python 브릿지 단독 테스트

```bash
# 긴 Bash 명령 confirm 시뮬레이션
echo '{"tool_name":"Bash","tool_input":{"command":"git commit -m \"feat: 로그인 화면 UI 추가\" && npm run build && echo done"}}' \
  | python bridge/arduino_bridge.py PreToolUse

# 파일 편집 confirm 시뮬레이션
echo '{"tool_name":"Edit","tool_input":{"file_path":"/very/long/path/to/src/components/LoginScreen.tsx"}}' \
  | python bridge/arduino_bridge.py PreToolUse

# 작업 완료 알림
echo '{}' | python bridge/arduino_bridge.py Stop
```

### 단계 3: Claude Code 통합 테스트

settings.json 설정 후 Claude Code 실행:

```bash
claude
```

Claude에게 Bash 명령 실행을 요청 → OLED에 전체 명령어가 스크롤 가능한 형태로 표시.

---

## 7. 아두이노 미연결 시 동작

`pyserial`이 설치되지 않았거나 Arduino가 연결되지 않은 경우:

- 포트 탐색 실패 → **OLED 전송 없이 폴백**
- `PreToolUse` (confirm 모드): **터미널에서 키보드 Enter 대기 (무한 대기)**
- `PostToolUse` / `Stop` (notify 모드): **3초 대기 후 자동 진행**

**아두이노 없이도 Claude Code 작업이 중단되지 않음.**

---

## 8. 트러블슈팅

### TFT에 아무것도 표시되지 않음

**원인 1: TFT_eSPI User_Setup.h 미설정**  
`arduino/TFT_eSPI_User_Setup.h` 내용을 라이브러리의 `User_Setup.h`에 반영했는지 확인.  
설정 변경 후 반드시 **재업로드** 필요.

**원인 2: 배선 오류**  
MOSI/SCK/CS/DC/RST 핀이 각각 GPIO23/18/15/2/4에 연결됐는지 확인.  
TFT 전원이 3.3V (5V 아님)에 연결됐는지 확인.

**원인 3: 색상이 이상함 (흰/검 반전 또는 RGB 오류)**  
`tft.init()` 후 `tft.invertDisplay(true)` 또는 드라이버 종류를 `ST7789_DRIVER` → `ST7735_DRIVER` 로 변경해 시도.

---

### GIF가 재생되지 않음

**원인 1: SPIFFS에 파일이 없음**  
`data/` 폴더에 `.gif` 파일을 넣고 **ESP32 Sketch Data Upload** 플러그인으로 업로드했는지 확인.

**원인 2: GIF 크기가 너무 큼**  
SPIFFS 전체 용량 ~3MB. 파일당 500KB 이하, 해상도 320×240 이하 권장.

**원인 3: 색상 반전**  
`gif.begin(BIG_ENDIAN_PIXELS)` → `gif.begin(LITTLE_ENDIAN_PIXELS)` 으로 변경.

---

### 조이스틱 방향이 반대

```cpp
// claude_lcd.ino — joyY/joyX 계산 라인 수정
joyY = (vy < JOY_LOW) ? 1 : (vy > JOY_HIGH) ? -1 : 0;  // 위아래 반전
joyX = (vx < JOY_LOW) ? 1 : (vx > JOY_HIGH) ? -1 : 0;  // 좌우 반전
```

---

### SW 버튼이 반응하지 않음

- GPIO32와 조이스틱 SW 연결 확인
- 조이스틱 GND가 ESP32 GND에 연결됐는지 확인
- `INPUT_PULLUP` 방식이므로 버튼 누를 때 LOW 신호가 와야 함

---

### 조이스틱 중립값이 이상함 (ESP32 ADC)

ESP32 ADC는 12비트(0~4095)이므로 중립값이 ~2000. 시리얼 모니터로 확인:
```cpp
// setup()에 임시 추가해서 값 확인
Serial.println(analogRead(34));  // VRy
Serial.println(analogRead(35));  // VRx
```
`JOY_LOW` / `JOY_HIGH` 값을 실측값 기준으로 조정.

---

### 시리얼 포트를 찾지 못함

수동으로 포트 지정:

```python
# arduino_bridge.py의 find_arduino_port() 하단에 추가
return "COM5"              # Windows
return "/dev/ttyUSB0"      # Linux
return "/dev/cu.usbmodem14101"  # macOS
```

**CH340 드라이버 미설치 (호환 보드):**  
Windows: CH340 드라이버 설치 필요.  
macOS Monterey 이상: 별도 드라이버 불필요.

---

### pyserial ImportError

```bash
pip install pyserial
# 또는
pip install -r requirements.txt
```

가상환경이 활성화된 상태에서 실행했는지 확인.

---

## 파일 구조

```
Arduino_claudeNotifier/
├── arduino/
│   ├── firmware/
│   │   ├── claude_lcd.ino          # ESP32 + ST7789 + AnimatedGIF 펌웨어
│   │   └── data/                   # SPIFFS 업로드 폴더 (.ino와 같은 위치)
│   │       ├── .gitkeep
│   │       ├── korean.vlw          # 한글 VLW 폰트 (gitignore)
│   │       └── *.gif               # GIF 파일 (gitignore)
│   └── TFT_eSPI_User_Setup.h       # TFT_eSPI 라이브러리 설정 참고
├── bridge/
│   └── arduino_bridge.py           # Python 시리얼 브릿지
├── claude-hooks/
│   └── settings.json               # Claude Code hooks 설정 예시
├── requirements.txt                # Python 의존성 (pyserial)
└── README.md
```

---

## 라이선스

MIT

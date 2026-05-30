# Claude Code Arduino Notifier

Claude Code가 명령을 실행하거나 작업을 완료할 때, **아두이노에 연결된 OLED 디스플레이**에 메시지를 표시하고 **조이스틱**으로 스크롤 확인 후 승인하는 임베디드 연동 시스템.

```
Claude Code가 Bash 실행 직전
        ↓
┌──────────────────────┐
│ [ Claude ]  CONFIRM  │
│ ─────────────────── │
│ Run this?            │  ← 조이스틱 위/아래로 스크롤
│ $ git commit -m "fe ^│
│ at: 로그인 화면 UI   │
│ 추가" && npm install v│  ← v = 더 내용 있음
│ ─────────────────── │
│ [SW] Confirm         │
└──────────────────────┘
        ↓
   조이스틱 꾹 누르기 → 실행 진행
   30초 무응답 → 자동 진행
```

### 스크롤 동작
| 조이스틱 | 동작 |
|----------|------|
| 위로 기울이기 | 한 줄 위로 스크롤 |
| 아래로 기울이기 | 한 줄 아래로 스크롤 |
| 꾹 누르고 유지 (0.4초 후) | 자동 반복 스크롤 |
| 버튼(SW) 눌러서 놓기 | CONFIRM 승인 |

---

## 목차

1. [필요 부품](#1-필요-부품)
2. [하드웨어 배선](#2-하드웨어-배선)
3. [Arduino IDE 설정](#3-arduino-ide-설정)
4. [Python 환경 설정](#4-python-환경-설정)
5. [Claude Code hooks 연동](#5-claude-code-hooks-연동)
6. [테스트](#6-테스트)
7. [아두이노 미연결 시 동작](#7-아두이노-미연결-시-동작)
8. [트러블슈팅](#8-트러블슈팅)

---

## 1. 필요 부품

| 부품명 | 수량 | 용도 |
|--------|------|------|
| Arduino Uno (또는 호환보드) | 1 | 메인 컨트롤러 |
| OLED 디스플레이 128×64 I2C (SSD1306) | 1 | 메시지 출력 |
| 아날로그 조이스틱 모듈 (KY-023) | 1 | 스크롤 + 승인 버튼 |
| 브레드보드 | 1 | 부품 연결 |
| 점퍼 와이어 | 9줄 | 배선 |
| USB A-B 케이블 | 1 | PC ↔ Arduino 연결 |

### OLED 구매 시 주의사항

- **I2C 4핀 모델** (VCC, GND, SCL, SDA) 구매 → SPI 6핀 모델과 혼동 주의
- SSD1306 컨트롤러 기반인지 확인
- I2C 주소는 보통 **0x3C**, 일부 제품은 **0x3D** (펌웨어에서 자동 탐색)

### 조이스틱 모듈 (KY-023) 핀 구성

| 핀 | 설명 |
|----|------|
| GND | 그라운드 |
| +5V | 전원 |
| VRx | X축 아날로그 (사용 안 함) |
| VRy | Y축 아날로그 → 스크롤 |
| SW | 버튼 → CONFIRM 승인 |

---

## 2. 하드웨어 배선

### 배선 다이어그램

```
Arduino Uno
┌─────────────────────────────────────┐
│                                     │
│  5V  ──────────────────── VCC  ─────┼──→ OLED VCC
│  GND ──────────────────── GND  ─────┼──→ OLED GND
│  A4  ──────────────────── SDA  ─────┼──→ OLED SDA
│  A5  ──────────────────── SCL  ─────┼──→ OLED SCL
│                                     │
│  5V  ──────────────────── +5V  ─────┼──→ 조이스틱 +5V
│  GND ──────────────────── GND  ─────┼──→ 조이스틱 GND
│  A1  ──────────────────── VRy  ─────┼──→ 조이스틱 Y축
│  D3  ──────────────────── SW   ─────┼──→ 조이스틱 버튼
│      (내부 풀업, VRx는 미연결)       │
└─────────────────────────────────────┘
```

### OLED 배선 상세

| Arduino 핀 | OLED 핀 | 설명 |
|------------|---------|------|
| 5V | VCC | 전원 |
| GND | GND | 그라운드 |
| A4 | SDA | I2C 데이터 |
| A5 | SCL | I2C 클럭 |

### 조이스틱 배선 상세

| Arduino 핀 | 조이스틱 핀 | 설명 |
|------------|------------|------|
| 5V | +5V | 전원 |
| GND | GND | 그라운드 |
| A1 | VRy | Y축 아날로그 (위/아래 스크롤) |
| D3 | SW | 버튼 (INPUT_PULLUP, CONFIRM 승인) |
| — | VRx | 미연결 (좌우 미사용) |

> **조이스틱 방향 주의:** VRy를 위로 기울였을 때 값이 0에 가까워야 "위 스크롤"로 동작.  
> 반대로 동작하면 조이스틱을 180도 돌려서 꽂거나, 펌웨어의 `JOY_THRESHOLD_LOW`와 `JOY_THRESHOLD_HIGH` 조건을 바꾸세요.

---

## 3. Arduino IDE 설정

### 3-1. 라이브러리 설치

Arduino IDE에서 **스케치 → 라이브러리 포함하기 → 라이브러리 관리** 열기:

| 검색어 | 설치할 라이브러리 | 용도 |
|--------|-----------------|------|
| `U8g2` | U8g2 by oliver | OLED 드라이버 + **한글 폰트** |

> `Adafruit SSD1306` / `Adafruit GFX`는 더 이상 사용하지 않음 — 설치되어 있어도 무방.

#### 폰트 크기 및 표시 용량

| 영역 | 폰트 | 한 줄 용량 |
|------|------|-----------|
| 헤더 / 풋터 | `u8g2_font_6x12_tf` | ASCII 전용, 21자 |
| 내용 | `u8g2_font_unifont_t_korean2` | ASCII 15자 or 한글 7자 |

CONFIRM 모드는 2줄, NOTIFY 모드는 3줄 표시 (조이스틱으로 나머지 스크롤).

#### ⚠️ Arduino Uno 플래시 경고

`u8g2_font_unifont_t_korean2` 한글 폰트는 플래시를 많이 사용함.  
업로드 시 **"Sketch too large"** 오류가 나면:

1. **Arduino Mega 2560** (256KB 플래시) — 가장 간단한 해결책
2. **ESP32 / STM32** 계열 보드로 교체
3. 또는 `u8g2_font_unifont_t_korean1` (더 작은 서브셋)으로 교체 후 재시도

### 3-2. 업로드

1. `arduino/firmware/claude_lcd.ino` 파일 열기
2. **툴 → 보드 → Arduino Uno** 선택
3. **툴 → 포트** → Arduino가 연결된 포트 선택
4. 업로드 버튼 클릭

### 3-3. I2C 주소 확인법

OLED가 표시되지 않을 때:

**스케치 → 예제 → Wire → i2c_scanner** 업로드 후  
**툴 → 시리얼 모니터** (속도: 9600) 에서 주소 확인.

펌웨어는 `0x3C → 0x3D` 순서로 자동 시도하므로 대부분 별도 수정 불필요.

### 3-4. 조이스틱 방향 보정

시리얼 모니터에서 아날로그 값 확인 후 필요하면 펌웨어 상수 수정:

```cpp
// claude_lcd.ino 상단
#define JOY_THRESHOLD_LOW   200   // 이 값 미만 = 위 스크롤
#define JOY_THRESHOLD_HIGH  800   // 이 값 초과 = 아래 스크롤
```

중립 상태에서 VRy 값은 보통 500~530 정도.

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
| `PostToolUse` | 작업 직후 | `"Done! ..."` | 3초 표시 후 자동 |
| `Notification` | Claude가 입력 대기 시 | 알림 메시지 | 3초 표시 후 자동 |
| `Stop` | Claude 응답 완료 시 | `"All done!"` | 3초 표시 후 자동 |

---

## 6. 테스트

### 단계 1: Arduino 시리얼 모니터 테스트

Arduino IDE **시리얼 모니터** (속도: 9600, **줄 끝: Newline**)에서 직접 입력:

```
CONFIRM:Run this?|$ git commit -m "feat: 로그인 화면 UI 추가"
```
→ 한글 포함 명령어가 줄 단위로 표시됨. 조이스틱으로 스크롤, SW 버튼 누르면 `OK` 출력

```
NOTIFY:작업 완료!|파일이 저장되었습니다.
```
→ 한글 알림 두 줄로 표시

```
CLEAR
```
→ `Waiting for Claude Code...` 초기화

> **프로토콜 참고:** `|`는 줄 구분자. `\n` 문자(실제 줄바꿈)를 시리얼로 보내면 명령이 잘리므로 반드시 `|` 사용.

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
- `PreToolUse` (confirm 모드): **터미널에서 키보드 Enter 대기**
- `PostToolUse` / `Stop` (notify 모드): **3초 대기 후 자동 진행**
- 30초 타임아웃 후 어떤 경우든 자동 진행

**아두이노 없이도 Claude Code 작업이 중단되지 않음.**

---

## 8. 트러블슈팅

### OLED에 아무것도 표시되지 않음

**원인 1: I2C 주소 불일치**  
i2c_scanner로 실제 주소 확인. 펌웨어는 `0x3C → 0x3D` 자동 시도.

**원인 2: 배선 오류**  
SDA ↔ SCL이 바뀌지 않았는지, VCC가 5V에 연결됐는지 확인.

**원인 3: 라이브러리 미설치**  
U8g2 라이브러리 설치 필요 (라이브러리 매니저에서 "U8g2" 검색).

---

### 조이스틱 스크롤 방향이 반대

펌웨어 상단에서 임계값 조건을 반전:

```cpp
// 변경 전
if (val < JOY_THRESHOLD_LOW)       newDir = -1;  // 위
else if (val > JOY_THRESHOLD_HIGH) newDir = 1;   // 아래

// 변경 후 (방향 반전)
if (val < JOY_THRESHOLD_LOW)       newDir = 1;   // 아래
else if (val > JOY_THRESHOLD_HIGH) newDir = -1;  // 위
```

---

### SW 버튼이 반응하지 않음

- D3 핀과 조이스틱 SW 연결 확인
- 조이스틱 GND가 Arduino GND에 연결됐는지 확인
- `INPUT_PULLUP` 방식이므로 버튼 누를 때 LOW 신호가 와야 함

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

### 타임아웃 조정

```python
# bridge/arduino_bridge.py 상단
TIMEOUT_SECONDS = 30  # 원하는 초로 변경
```

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
│   └── firmware/
│       └── claude_lcd.ino       # OLED + 조이스틱 Arduino 펌웨어
├── bridge/
│   └── arduino_bridge.py        # Python 시리얼 브릿지
├── claude-hooks/
│   └── settings.json            # Claude Code hooks 설정 예시
├── requirements.txt             # Python 의존성 (pyserial)
└── README.md
```

---

## 라이선스

MIT

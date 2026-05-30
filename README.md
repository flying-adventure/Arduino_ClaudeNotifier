# Claude Code Arduino Notifier

Claude Code가 명령을 실행하거나 작업을 완료할 때, **아두이노에 연결된 OLED 디스플레이**에 메시지를 표시하고 **물리 버튼**으로 승인하는 임베디드 연동 시스템.

```
Claude Code가 Bash 실행 직전
        ↓
┌──────────────────────┐
│ [ Claude ]  CONFIRM  │
│ ─────────────────── │
│ Should I run this?   │
│ $ git push origin    │
│ ─────────────────── │
│ [BTN] or [Enter]     │
└──────────────────────┘
        ↓
   버튼 누르기 → 실행 진행
   30초 무응답 → 자동 진행
```

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
| 택트 스위치 (푸시버튼) | 1 | 작업 승인 |
| 브레드보드 | 1 | 부품 연결 |
| 점퍼 와이어 (암수) | 6줄 | 배선 |
| USB A-B 케이블 | 1 | PC ↔ Arduino 연결 |

### OLED 구매 시 주의사항

- **I2C 4핀 모델** (VCC, GND, SCL, SDA) 구매 → SPI 6핀 모델과 혼동 주의
- SSD1306 컨트롤러 기반인지 확인
- I2C 주소는 보통 **0x3C**, 일부 제품은 **0x3D** (펌웨어에서 자동 탐색)

---

## 2. 하드웨어 배선

### 배선 다이어그램

```
Arduino Uno
┌─────────────────────────────┐
│                             │
│  5V  ──────────────── VCC   │──→ OLED VCC
│  GND ──────────────── GND   │──→ OLED GND
│  A4  ──────────────── SDA   │──→ OLED SDA
│  A5  ──────────────── SCL   │──→ OLED SCL
│                             │
│  D2  ──── [버튼] ──── GND   │
│      (내부 풀업 사용)        │
└─────────────────────────────┘
```

### 배선 상세

#### OLED 연결 (I2C)

| Arduino 핀 | OLED 핀 | 설명 |
|------------|---------|------|
| 5V | VCC | 전원 |
| GND | GND | 그라운드 |
| A4 | SDA | I2C 데이터 |
| A5 | SCL | I2C 클럭 |

#### 버튼 연결

| Arduino 핀 | 버튼 | 설명 |
|------------|------|------|
| D2 | 한쪽 다리 | 내부 풀업(INPUT_PULLUP) 사용 |
| GND | 반대쪽 다리 | LOW 감지 방식 |

> 브레드보드에 버튼을 꽂을 때, 버튼 다리 방향을 주의하세요.  
> 4핀 버튼은 마주보는 2핀이 연결되어 있으므로, 직각 방향으로 걸쳐서 꽂아야 합니다.

---

## 3. Arduino IDE 설정

### 3-1. 라이브러리 설치

Arduino IDE에서 **스케치 → 라이브러리 포함하기 → 라이브러리 관리** 열기:

| 검색어 | 설치할 라이브러리 | 버전 |
|--------|-----------------|------|
| `Adafruit SSD1306` | Adafruit SSD1306 | 최신 |
| `Adafruit GFX` | Adafruit GFX Library | 최신 (SSD1306 설치 시 자동 추가됨) |

### 3-2. 업로드

1. `arduino/firmware/claude_lcd.ino` 파일 열기
2. **툴 → 보드 → Arduino Uno** 선택
3. **툴 → 포트** → Arduino가 연결된 포트 선택 (Windows: `COM3` ~ `COM9` 중 하나, Mac: `/dev/cu.usbmodem...`)
4. 업로드 버튼 클릭

### 3-3. I2C 주소 확인법

OLED가 표시되지 않을 때 I2C 주소를 직접 확인:

**스케치 → 예제 → Wire → i2c_scanner** 업로드 후  
**툴 → 시리얼 모니터** (속도: 9600) 에서 주소 확인.

- `0x3C` 또는 `0x3D` 중 하나가 표시됨
- 펌웨어는 두 주소를 자동으로 시도하므로 대부분 별도 수정 불필요

---

## 4. Python 환경 설정

### 4-1. 가상환경 생성 및 활성화

```bash
# 프로젝트 루트에서
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

### 4-3. 브릿지 스크립트 단독 실행 테스트

Arduino를 연결한 상태에서:

```bash
# notify 모드 테스트 (OLED에 3초 표시 후 사라짐)
echo '{"tool_name": "Bash", "tool_input": {"command": "git status"}}' | python bridge/arduino_bridge.py PreToolUse

# Stop 이벤트 테스트
echo '{}' | python bridge/arduino_bridge.py Stop
```

---

## 5. Claude Code hooks 연동

### 5-1. settings.json 위치

Claude Code hooks 설정 파일은 두 곳에 놓을 수 있음:

| 위치 | 효과 |
|------|------|
| `<프로젝트>/.claude/settings.json` | 해당 프로젝트에서만 적용 |
| `~/.claude/settings.json` | 모든 프로젝트에 전역 적용 |

### 5-2. 설정 방법

`claude-hooks/settings.json`을 열고, **`/ABSOLUTE/PATH/TO/bridge/arduino_bridge.py`** 부분을 실제 경로로 교체:

**Windows 예시:**
```json
"command": "python \"C:\\Users\\YourName\\claude-arduino-hooks\\bridge\\arduino_bridge.py\" PreToolUse"
```

**macOS / Linux 예시:**
```json
"command": "python3 \"/home/yourname/claude-arduino-hooks/bridge/arduino_bridge.py\" PreToolUse"
```

### 5-3. 기존 settings.json에 병합하기

이미 `.claude/settings.json`이 있다면, `hooks` 키만 추가/병합:

```json
{
  "기존설정키": "기존값",
  "hooks": {
    "PreToolUse": [ ... ],
    "PostToolUse": [ ... ],
    "Notification": [ ... ],
    "Stop": [ ... ]
  }
}
```

### 5-4. 훅별 동작 설명

| 훅 | 발생 시점 | OLED 동작 | 대기 여부 |
|----|----------|----------|---------|
| `PreToolUse` | Bash/파일 수정 직전 | `"Should I run this?"` + 명령 미리보기 | 버튼/Enter 대기 |
| `PostToolUse` | Bash/파일 수정 직후 | `"Done! ..."` | 3초 표시 후 자동 |
| `Notification` | Claude가 입력 대기 시 | 알림 메시지 | 3초 표시 후 자동 |
| `Stop` | Claude 응답 완료 시 | `"All done! That wasn't so bad."` | 3초 표시 후 자동 |

---

## 6. 테스트

### 단계 1: Arduino 단독 테스트

Arduino IDE 시리얼 모니터(속도: 9600)에서 직접 명령 전송:

```
CONFIRM:Should I run this?\n$ git push origin main
```
→ OLED에 메시지 + `[BTN] or [Enter]` 표시, 버튼 누르면 시리얼 모니터에 `OK` 출력

```
NOTIFY:All done!\nThat wasn't so bad.
```
→ OLED에 메시지 표시

```
CLEAR
```
→ OLED가 `Waiting for Claude Code...` 로 초기화

### 단계 2: Python 브릿지 단독 테스트

```bash
# Bash 실행 전 confirm 시뮬레이션
echo '{"tool_name":"Bash","tool_input":{"command":"rm -rf /tmp/test"}}' \
  | python bridge/arduino_bridge.py PreToolUse

# 파일 편집 confirm 시뮬레이션
echo '{"tool_name":"Edit","tool_input":{"file_path":"/src/main.py"}}' \
  | python bridge/arduino_bridge.py PreToolUse

# 작업 완료 알림 시뮬레이션
echo '{}' | python bridge/arduino_bridge.py Stop
```

### 단계 3: Claude Code 통합 테스트

settings.json 설정 후 Claude Code를 실행:

```bash
claude
```

Claude에게 Bash 명령 실행을 요청하면 OLED에 confirm 화면이 뜨는지 확인.

---

## 7. 아두이노 미연결 시 동작

`pyserial`이 설치되지 않았거나 Arduino가 연결되지 않은 경우:

- 포트 탐색 실패 → **OLED 전송 없이 폴백**
- `PreToolUse` (confirm 모드): **터미널에서 키보드 Enter 대기**
- `PostToolUse` / `Stop` (notify 모드): **3초 대기 후 자동 진행**
- 30초 타임아웃 후 어떤 경우든 자동 진행

즉, **아두이노 없이도 Claude Code 작업이 중단되지 않음**.

---

## 8. 트러블슈팅

### OLED에 아무것도 표시되지 않음

**원인 1: I2C 주소 불일치**

시리얼 모니터에서 i2c_scanner 스케치로 실제 주소 확인.  
펌웨어는 `0x3C` → `0x3D` 순서로 자동 시도하므로 대부분 자동 해결됨.

**원인 2: 배선 오류**

- SDA ↔ SCL 핀이 바뀌지 않았는지 확인
- OLED VCC가 5V에 연결되었는지 확인 (3.3V OLED는 3V3 핀 사용)

**원인 3: 라이브러리 미설치**

Adafruit SSD1306, Adafruit GFX Library 둘 다 설치되었는지 확인.

---

### 시리얼 포트를 찾지 못함

```
포트 자동 탐색 실패 → 키보드 모드로 폴백
```

**수동으로 포트 지정하려면** `arduino_bridge.py`의 `find_arduino_port()` 하단에 다음 추가:

```python
# 자동 탐색 대신 직접 지정 (예시)
return "COM5"         # Windows
return "/dev/ttyUSB0" # Linux
return "/dev/cu.usbmodem14101"  # macOS
```

**드라이버 미설치 (CH340 기반 호환 보드):**

Windows: [CH340 드라이버](https://sparks.gogo.co.nz/ch340.html) 설치  
macOS Monterey 이상: 별도 드라이버 불필요

---

### confirm 모드에서 Enter 키가 동작하지 않음

원인: stdin이 JSON 읽기로 이미 소비된 상태.

브릿지는 stdin 대신 터미널을 직접 엽니다:
- Windows: `msvcrt` 모듈로 키보드 직접 읽기
- Linux/macOS: `/dev/tty` 직접 오픈

터미널이 없는 환경(CI, 파이프라인)에서는 30초 타임아웃 후 자동 진행.

---

### 타임아웃이 너무 짧거나 긺

`bridge/arduino_bridge.py` 상단의 상수 수정:

```python
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
claude-arduino-hooks/
├── arduino/
│   └── firmware/
│       └── claude_lcd.ino       # OLED + 버튼 Arduino 펌웨어
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

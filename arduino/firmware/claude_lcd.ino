#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define I2C_ADDR_PRIMARY  0x3C
#define I2C_ADDR_FALLBACK 0x3D

// 조이스틱 핀
#define JOYSTICK_VRY        A1
#define JOYSTICK_SW         3

// 조이스틱 임계값 및 자동반복 타이밍
#define JOY_THRESHOLD_LOW   200
#define JOY_THRESHOLD_HIGH  800
#define JOY_REPEAT_DELAY    400UL
#define JOY_REPEAT_INTERVAL 150UL
#define DEBOUNCE_MS         50UL

// 레이아웃 상수 (텍스트 size 1 = 6×8px, 20자 × 6px = 120px → 오른쪽 8px에 화살표)
#define CHARS_PER_LINE  20
#define VISIBLE_CONFIRM  4   // CONFIRM 모드: 헤더+푸터 제외 4줄
#define VISIBLE_NOTIFY   6   // NOTIFY 모드: 헤더만 제외 6줄

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String inputBuffer = "";
bool waitingForConfirm = false;

// 현재 표시 중인 메시지 ('|'를 줄 구분자로 사용)
String currentMsg = "";
int    totalLines = 0;
int    scrollOffset = 0;
bool   currentNeedsConfirm = false;

// 조이스틱 스크롤 상태
int           joyDir       = 0;        // -1=위, 0=중립, 1=아래
unsigned long joyPressTime  = 0;
unsigned long joyRepeatTime = 0;
bool          joyRepeating  = false;

// SW 디바운스 상태
unsigned long lastSwTime   = 0;
bool          swWasPressed = false;

// ──────────────────────────────────────────────
// 라인 계산 헬퍼 ('|' 구분자 + CHARS_PER_LINE 강제 줄바꿈)
// ──────────────────────────────────────────────

int calcTotalLines(const String& msg) {
  if (msg.length() == 0) return 0;
  int count = 0;
  int pos = 0;
  int len = (int)msg.length();
  while (pos <= len) {
    int sep    = msg.indexOf('|', pos);
    int segEnd = (sep < 0) ? len : sep;
    int segLen = segEnd - pos;
    count += (segLen == 0) ? 1 : (segLen + CHARS_PER_LINE - 1) / CHARS_PER_LINE;
    pos = (sep < 0) ? len + 1 : sep + 1;
  }
  return count;
}

// idx번째 표시 줄 반환 (0-based)
String getLine(const String& msg, int idx) {
  int count = 0;
  int pos   = 0;
  int len   = (int)msg.length();
  while (pos <= len) {
    int sep      = msg.indexOf('|', pos);
    int segEnd   = (sep < 0) ? len : sep;
    int segStart = pos;
    int segLen   = segEnd - segStart;
    pos = (sep < 0) ? len + 1 : sep + 1;

    if (segLen == 0) {
      if (count == idx) return "";
      count++;
      continue;
    }

    int segLines = (segLen + CHARS_PER_LINE - 1) / CHARS_PER_LINE;
    if (idx < count + segLines) {
      int off     = (idx - count) * CHARS_PER_LINE;
      int copyEnd = min(segStart + off + CHARS_PER_LINE, segEnd);
      return msg.substring(segStart + off, copyEnd);
    }
    count += segLines;
  }
  return "";
}

// ──────────────────────────────────────────────
// 셋업 / 루프
// ──────────────────────────────────────────────

void setup() {
  Serial.begin(9600);
  pinMode(JOYSTICK_SW, INPUT_PULLUP);

  // I2C 주소 0x3C 먼저 시도, 실패하면 0x3D
  if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_PRIMARY)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_FALLBACK)) {
      for (;;);
    }
  }

  showReady();
}

void loop() {
  // 시리얼 수신 처리
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) processCommand(inputBuffer);
      inputBuffer = "";
    } else if (c != '\r' && inputBuffer.length() < 160) {
      inputBuffer += c;
    }
  }

  // 조이스틱 SW 상태 읽기 (엣지 감지)
  bool swPressed = (digitalRead(JOYSTICK_SW) == LOW);
  unsigned long now = millis();
  bool swJustPressed = swPressed && !swWasPressed && (now - lastSwTime > DEBOUNCE_MS);
  if (swJustPressed) lastSwTime = now;

  // CONFIRM 모드: SW 누르면 OK 전송
  if (waitingForConfirm && swJustPressed) {
    waitingForConfirm = false;
    Serial.println("OK");
    while (digitalRead(JOYSTICK_SW) == LOW);  // 뗄 때까지 대기
    delay(100);
    showReady();
  }

  // 조이스틱 스크롤
  if (totalLines > 0) handleJoystickScroll();

  swWasPressed = swPressed;
}

// ──────────────────────────────────────────────
// 명령 처리
// ──────────────────────────────────────────────

void processCommand(const String& cmd) {
  if (cmd.startsWith("CONFIRM:")) {
    currentMsg          = cmd.substring(8);
    totalLines          = calcTotalLines(currentMsg);
    scrollOffset        = 0;
    currentNeedsConfirm = true;
    waitingForConfirm   = true;
    renderScrollView();
  } else if (cmd.startsWith("NOTIFY:")) {
    currentMsg          = cmd.substring(7);
    totalLines          = calcTotalLines(currentMsg);
    scrollOffset        = 0;
    currentNeedsConfirm = false;
    waitingForConfirm   = false;
    renderScrollView();
  } else if (cmd == "CLEAR") {
    currentMsg = "";
    totalLines = 0;
    waitingForConfirm = false;
    showReady();
  }
}

// ──────────────────────────────────────────────
// 화면 렌더링
// ──────────────────────────────────────────────

void renderScrollView() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // 헤더 (y=0~10)
  display.setCursor(0, 0);
  display.print(F("[ Claude ]"));
  if (currentNeedsConfirm) {
    display.setCursor(66, 0);
    display.print(F("CONFIRM"));
  }
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  int visible   = currentNeedsConfirm ? VISIBLE_CONFIRM : VISIBLE_NOTIFY;
  int maxOffset = max(0, totalLines - visible);

  // 텍스트 출력 (x=0, 20자 = 120px, y=14부터 8px 간격)
  for (int i = 0; i < visible && (scrollOffset + i) < totalLines; i++) {
    display.setCursor(0, 14 + i * 8);
    display.print(getLine(currentMsg, scrollOffset + i));
  }

  // 스크롤 화살표 (x=121, 텍스트 영역 오른쪽)
  if (scrollOffset > 0) {
    display.setCursor(121, 14);
    display.print(F("^"));
  }
  if (scrollOffset < maxOffset) {
    display.setCursor(121, 14 + (visible - 1) * 8);
    display.print(F("v"));
  }

  // CONFIRM 푸터 (y=52~63)
  if (currentNeedsConfirm) {
    display.drawFastHLine(0, 52, SCREEN_WIDTH, SSD1306_WHITE);
    display.setCursor(0, 55);
    display.print(F("[SW] Confirm"));
  }

  display.display();
}

// ──────────────────────────────────────────────
// 조이스틱 스크롤 처리 (자동반복 포함)
// ──────────────────────────────────────────────

void handleJoystickScroll() {
  int val    = analogRead(JOYSTICK_VRY);
  int newDir = 0;
  if (val < JOY_THRESHOLD_LOW)        newDir = -1;  // 위
  else if (val > JOY_THRESHOLD_HIGH)  newDir = 1;   // 아래

  unsigned long now = millis();

  if (newDir != 0) {
    if (joyDir != newDir) {
      // 방향 전환 시 즉시 첫 스크롤
      joyDir       = newDir;
      joyPressTime = now;
      joyRepeating = false;
      doScroll(newDir);
    } else if (!joyRepeating && (now - joyPressTime >= JOY_REPEAT_DELAY)) {
      // 첫 자동반복 발동
      joyRepeating  = true;
      joyRepeatTime = now;
      doScroll(newDir);
    } else if (joyRepeating && (now - joyRepeatTime >= JOY_REPEAT_INTERVAL)) {
      // 연속 자동반복
      joyRepeatTime = now;
      doScroll(newDir);
    }
  } else {
    joyDir       = 0;
    joyRepeating = false;
  }
}

void doScroll(int dir) {
  int visible   = currentNeedsConfirm ? VISIBLE_CONFIRM : VISIBLE_NOTIFY;
  int maxOffset = max(0, totalLines - visible);
  int newOffset = constrain(scrollOffset + dir, 0, maxOffset);
  if (newOffset != scrollOffset) {
    scrollOffset = newOffset;
    renderScrollView();
  }
}

// ──────────────────────────────────────────────
// 대기 화면
// ──────────────────────────────────────────────

void showReady() {
  currentMsg = "";
  totalLines = 0;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print(F("[ Claude ]"));
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 24);
  display.println(F("Waiting for"));
  display.println(F("Claude Code..."));

  display.display();
}

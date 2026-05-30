#include <Wire.h>
#include <U8g2lib.h>

// SSD1306 128×64 I2C, 전체 버퍼 모드 (한글 렌더링 가능)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// 조이스틱 핀
#define JOYSTICK_VRY        A1
#define JOYSTICK_SW         3
#define JOY_THRESHOLD_LOW   200
#define JOY_THRESHOLD_HIGH  800
#define JOY_REPEAT_DELAY    400UL
#define JOY_REPEAT_INTERVAL 150UL
#define DEBOUNCE_MS         50UL

// 레이아웃
// - 헤더/풋터: u8g2_font_6x12_tf  (6×12px, ASCII 전용)
// - 내용:      u8g2_font_unifont_t_korean2  (unifont 16px, 한글 포함)
//   ASCII = 8px 너비, 한글 = 16px 너비
#define HEADER_Y         9    // 헤더 텍스트 baseline
#define HDR_DIV_Y        10   // 헤더 구분선
#define CONTENT_Y0       24   // 첫 줄 baseline (unifont: ascent ~13px → top=y-13)
#define LINE_STEP        16   // 줄 간격
#define PIXEL_LINE_WIDTH 120  // 내용 너비 (128 - 8px 화살표 영역)
#define FOOTER_DIV_Y     52   // CONFIRM 풋터 구분선
#define FOOTER_Y         62   // CONFIRM 풋터 텍스트 baseline
#define VISIBLE_CONFIRM   2   // CONFIRM 모드 가시 줄 수
#define VISIBLE_NOTIFY    3   // NOTIFY  모드 가시 줄 수
#define MAX_LINES        12   // 최대 저장 줄 수

// 줄 위치 정보 (currentMsg 내 byte 오프셋 · 길이)
struct LineSpan {
    uint16_t start;
    uint16_t len;
};

String   inputBuffer = "";
bool     waitingForConfirm = false;

String   currentMsg = "";
LineSpan lineSpans[MAX_LINES];
int      totalLines   = 0;
int      scrollOffset = 0;
bool     currentNeedsConfirm = false;

int           joyDir       = 0;
unsigned long joyPressTime  = 0;
unsigned long joyRepeatTime = 0;
bool          joyRepeating  = false;
unsigned long lastSwTime    = 0;
bool          swWasPressed  = false;

// ──────────────────────────────────────────────
// UTF-8 헬퍼
// ──────────────────────────────────────────────

// 첫 바이트로 시퀀스 바이트 수 반환
static int utf8ByteLen(uint8_t b) {
    if (b < 0x80) return 1;
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    return 4;
}

// unifont 기준 픽셀 너비: ASCII/Latin(1~2바이트) = 8px, CJK·한글(3~4바이트) = 16px
static int utf8PixelWidth(uint8_t b) {
    return (b < 0xE0) ? 8 : 16;
}

// ──────────────────────────────────────────────
// 줄 분리 ('|' 구분자 + 픽셀 너비 기반 래핑)
// ──────────────────────────────────────────────

void splitIntoLines(const String& msg) {
    totalLines   = 0;
    scrollOffset = 0;

    int pos    = 0;
    int msgLen = (int)msg.length();

    while (pos <= msgLen && totalLines < MAX_LINES) {
        int sep    = msg.indexOf('|', pos);
        int segEnd = (sep < 0) ? msgLen : sep;

        if (pos == segEnd) {
            lineSpans[totalLines++] = {(uint16_t)pos, 0};
        } else {
            int sp = pos;
            while (sp < segEnd && totalLines < MAX_LINES) {
                int lineStart = sp;
                int pixels    = 0;
                while (sp < segEnd) {
                    uint8_t b  = (uint8_t)msg[sp];
                    int     pw = utf8PixelWidth(b);
                    int     bl = utf8ByteLen(b);
                    if (sp + bl > segEnd) bl = 1;          // 잘린 시퀀스 방어
                    if (pixels + pw > PIXEL_LINE_WIDTH) break;
                    pixels += pw;
                    sp     += bl;
                }
                lineSpans[totalLines++] = {
                    (uint16_t)lineStart,
                    (uint16_t)(sp - lineStart)
                };
            }
        }

        pos = (sep < 0) ? msgLen + 1 : sep + 1;
    }
}

String getLine(int idx) {
    if (idx < 0 || idx >= totalLines) return "";
    const LineSpan& s = lineSpans[idx];
    return currentMsg.substring(s.start, s.start + s.len);
}

// ──────────────────────────────────────────────
// 셋업 / 루프
// ──────────────────────────────────────────────

void setup() {
    Serial.begin(9600);
    pinMode(JOYSTICK_SW, INPUT_PULLUP);

    // I2C 주소 탐색 (0x3C 먼저, 없으면 0x3D)
    Wire.begin();
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() != 0) {
        u8g2.setI2CAddress(0x7A);   // 0x3D << 1 (U8g2는 8비트 주소 사용)
    }

    u8g2.begin();
    showReady();
}

void loop() {
    // 시리얼 수신
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

    // SW 엣지 감지
    bool swPressed = (digitalRead(JOYSTICK_SW) == LOW);
    unsigned long now = millis();
    bool swJustPressed = swPressed && !swWasPressed && (now - lastSwTime > DEBOUNCE_MS);
    if (swJustPressed) lastSwTime = now;

    // CONFIRM 승인
    if (waitingForConfirm && swJustPressed) {
        waitingForConfirm = false;
        Serial.println("OK");
        while (digitalRead(JOYSTICK_SW) == LOW);
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
        splitIntoLines(currentMsg);
        currentNeedsConfirm = true;
        waitingForConfirm   = true;
        renderScrollView();
    } else if (cmd.startsWith("NOTIFY:")) {
        currentMsg          = cmd.substring(7);
        splitIntoLines(currentMsg);
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
    u8g2.clearBuffer();

    // 헤더 (6x12 소형 폰트)
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, HEADER_Y, "[ Claude ]");
    if (currentNeedsConfirm) {
        u8g2.drawStr(66, HEADER_Y, "CONFIRM");
    }
    u8g2.drawHLine(0, HDR_DIV_Y, 128);

    int visible   = currentNeedsConfirm ? VISIBLE_CONFIRM : VISIBLE_NOTIFY;
    int maxOffset = max(0, totalLines - visible);

    // 내용 (unifont 한글 포함)
    u8g2.setFont(u8g2_font_unifont_t_korean2);
    for (int i = 0; i < visible && (scrollOffset + i) < totalLines; i++) {
        String line = getLine(scrollOffset + i);
        u8g2.drawUTF8(0, CONTENT_Y0 + i * LINE_STEP, line.c_str());
    }

    // 스크롤 화살표 (x=122, 6x12 폰트)
    u8g2.setFont(u8g2_font_6x12_tf);
    if (scrollOffset > 0) {
        u8g2.drawStr(122, CONTENT_Y0, "^");
    }
    if (scrollOffset < maxOffset) {
        u8g2.drawStr(122, CONTENT_Y0 + (visible - 1) * LINE_STEP, "v");
    }

    // CONFIRM 풋터
    if (currentNeedsConfirm) {
        u8g2.drawHLine(0, FOOTER_DIV_Y, 128);
        u8g2.drawStr(0, FOOTER_Y, "[SW] Confirm");
    }

    u8g2.sendBuffer();
}

// ──────────────────────────────────────────────
// 조이스틱 스크롤 (자동반복 포함)
// ──────────────────────────────────────────────

void handleJoystickScroll() {
    int val    = analogRead(JOYSTICK_VRY);
    int newDir = 0;
    if (val < JOY_THRESHOLD_LOW)       newDir = -1;
    else if (val > JOY_THRESHOLD_HIGH) newDir =  1;

    unsigned long now = millis();

    if (newDir != 0) {
        if (joyDir != newDir) {
            joyDir       = newDir;
            joyPressTime = now;
            joyRepeating = false;
            doScroll(newDir);
        } else if (!joyRepeating && (now - joyPressTime >= JOY_REPEAT_DELAY)) {
            joyRepeating  = true;
            joyRepeatTime = now;
            doScroll(newDir);
        } else if (joyRepeating && (now - joyRepeatTime >= JOY_REPEAT_INTERVAL)) {
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

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, HEADER_Y, "[ Claude ]");
    u8g2.drawHLine(0, HDR_DIV_Y, 128);
    u8g2.drawStr(0, 32, "Waiting for");
    u8g2.drawStr(0, 44, "Claude Code...");
    u8g2.sendBuffer();
}

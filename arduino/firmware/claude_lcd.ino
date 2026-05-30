/*
 * Claude Code Notifier — ESP32 + ST7789 320×240 (가로) + AnimatedGIF + SPIFFS
 *
 * 모드:
 *   MODE_CLAUDE   — 알림 표시 (기본)
 *   MODE_GIF_PLAY — 선택된 GIF 전체화면 반복 재생
 *   MODE_GIF_LIST — GIF 목록 (↑↓ 탐색, SW 선택)
 *
 * 조이스틱:
 *   VRy ↑↓ — CLAUDE: 스크롤 / GIF_LIST: 탐색
 *   VRx ←→ — CLAUDE ↔ GIF_PLAY 전환
 *   SW      — CLAUDE: 확인 / GIF_PLAY: 목록 열기 / GIF_LIST: 선택
 *
 * 시리얼 프로토콜 (9600 baud):
 *   CONFIRM:<msg>  — SW 누를 때까지 대기 후 "OK" 응답
 *   NOTIFY:<msg>   — 3초 표시 후 자동 복귀
 *   CLEAR          — 대기 화면으로
 *   메시지 내 '|'  — 줄 구분자
 *
 * TFT_eSPI User_Setup.h 설정 필요 → arduino/TFT_eSPI_User_Setup.h 참고
 * SPIFFS 업로드 → arduino/firmware/data/ 폴더에 GIF 및 korean.vlw 배치
 */

#include <TFT_eSPI.h>
#include <AnimatedGIF.h>
#include <SPIFFS.h>

// ─── 핀 ───────────────────────────────────────
#define JOY_VRY 34
#define JOY_VRX 35
#define JOY_SW  32

// ─── 조이스틱 임계값 ──────────────────────────
#define JOY_LOW              200
#define JOY_HIGH             800
#define JOY_REPEAT_DELAY    400UL
#define JOY_REPEAT_INTERVAL 150UL
#define DEBOUNCE_MS          50UL

// ─── 화면 레이아웃 (가로 320×240) ─────────────
#define SCREEN_W         320
#define SCREEN_H         240
#define HEADER_H          32
#define FOOTER_H          32
#define DIV_TOP_Y         HEADER_H
#define DIV_BOT_Y        (SCREEN_H - FOOTER_H)
#define CONTENT_Y0       (DIV_TOP_Y + 6)
#define LINE_H            20
#define MARGIN_X          12
#define PIXEL_LINE_W     (SCREEN_W - MARGIN_X * 2)   // 296px
#define MAX_VIS_LINES    ((DIV_BOT_Y - CONTENT_Y0) / LINE_H)  // ~8

// ─── 색상 ─────────────────────────────────────
#define C_BG       TFT_BLACK
#define C_CONFIRM  0xFD20   // 주황
#define C_NOTIFY   0x07E0   // 초록
#define C_GIF_HDR  0x2945   // 남색
#define C_TEXT     TFT_WHITE
#define C_DIM      0x528A   // 회색
#define C_SEL      0x001F   // 파랑 (목록 선택)

// ─── 한계 ─────────────────────────────────────
#define MAX_GIFS         32
#define MAX_LINES        32
#define GIF_LIST_VISIBLE  6

// ─── 모드 ─────────────────────────────────────
enum Mode { MODE_CLAUDE, MODE_GIF_PLAY, MODE_GIF_LIST };

// ─── 전역 ─────────────────────────────────────
TFT_eSPI   tft;
AnimatedGIF gif;

Mode   currentMode     = MODE_CLAUDE;
Mode   returnMode      = MODE_CLAUDE;   // 알림 후 복귀할 모드
int    currentGifIndex = 0;
int    gifListOffset   = 0;
String gifFiles[MAX_GIFS];
int    gifCount        = 0;
bool   gifIsOpen       = false;
bool   fontLoaded      = false;
int    gifXOff = 0, gifYOff = 0;

// 알림 상태
struct LineSpan { uint16_t start, len; };
String   currentMsg   = "";
LineSpan lineSpans[MAX_LINES];
int      totalLines   = 0;
int      scrollOffset = 0;
bool     needsConfirm = false;
bool     notifyActive = false;
uint32_t notifyStart  = 0;

// 조이스틱 상태
int      joyY = 0, joyX = 0;
int      lastYDir = 0, lastXDir = 0;
uint32_t joyHeldSince = 0;
uint32_t lastRepeat   = 0;
bool     swNow = false, swPrev = false;
uint32_t swEdge = 0;

// 시리얼 버퍼
String inputBuffer = "";


// ══ SPIFFS: GIF 목록 스캔 ════════════════════
void scanGifFiles() {
    gifCount = 0;
    File root = SPIFFS.open("/");
    File f = root.openNextFile();
    while (f && gifCount < MAX_GIFS) {
        String n = String(f.name());
        if (n.endsWith(".gif") || n.endsWith(".GIF"))
            gifFiles[gifCount++] = n;
        f = root.openNextFile();
    }
}


// ══ AnimatedGIF 콜백 ════════════════════════
static File gifFH;

void *gifOpenCB(const char *fname, int32_t *pSize) {
    gifFH = SPIFFS.open(fname);
    if (!gifFH) return nullptr;
    *pSize = gifFH.size();
    return &gifFH;
}

void gifCloseCB(void *h) {
    if (h) ((File *)h)->close();
}

int32_t gifReadCB(GIFFILE *pf, uint8_t *buf, int32_t len) {
    File *f = (File *)pf->fHandle;
    int32_t toRead = min(len, (int32_t)(pf->iSize - pf->iPos));
    if (toRead <= 0) return 0;
    toRead = f->read(buf, toRead);
    pf->iPos = f->position();
    return toRead;
}

int32_t gifSeekCB(GIFFILE *pf, int32_t pos) {
    File *f = (File *)pf->fHandle;
    f->seek(pos);
    pf->iPos = f->position();
    return pf->iPos;
}

static uint16_t gifLine[320];
void gifDrawCB(GIFDRAW *p) {
    uint16_t *pal = p->pPalette;
    uint8_t  *s   = p->pPixels;
    for (int x = 0; x < p->iWidth; x++) {
        gifLine[x] = (p->ucHasTransparency && s[x] == p->ucTransparent)
                     ? C_BG : pal[s[x]];
    }
    int dx = p->iX + gifXOff;
    int dy = p->iY + p->y + gifYOff;
    if (dx >= 0 && dy >= 0 && dx < SCREEN_W && dy < SCREEN_H)
        tft.pushImage(dx, dy, p->iWidth, 1, gifLine);
}


// ══ GIF 열기 / 닫기 ════════════════════════
bool openGif(int idx) {
    if (gifIsOpen) { gif.close(); gifIsOpen = false; }
    if (idx < 0 || idx >= gifCount) return false;
    if (!gif.open(gifFiles[idx].c_str(),
                  gifOpenCB, gifCloseCB, gifReadCB, gifSeekCB, gifDrawCB))
        return false;
    // 화면 중앙에 GIF 배치
    gifXOff = max(0, (SCREEN_W - gif.getCanvasWidth())  / 2);
    gifYOff = max(0, (SCREEN_H - gif.getCanvasHeight()) / 2);
    gifIsOpen = true;
    return true;
}

void closeGif() {
    if (gifIsOpen) { gif.close(); gifIsOpen = false; }
}


// ══ UTF-8 헬퍼 ══════════════════════════════
uint8_t u8len(uint8_t b) {
    if (b < 0x80) return 1;
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    return 4;
}
// ASCII/Latin = 9px, 한글(3바이트 이상) = 18px
uint8_t u8px(uint8_t b) { return (b < 0xE0) ? 9 : 18; }


// ══ 텍스트 라인 분할 ════════════════════════
void splitLines(const String &msg) {
    totalLines = scrollOffset = 0;
    currentMsg = msg;
    const char *s = msg.c_str();
    uint16_t len = msg.length(), i = 0;

    while (i < len && totalLines < MAX_LINES) {
        uint16_t lineStart = i, px = 0;
        while (i < len) {
            if (s[i] == '|') {
                lineSpans[totalLines++] = { lineStart, (uint16_t)(i - lineStart) };
                i++; lineStart = i; px = 0;
                break;
            }
            uint8_t bl = u8len((uint8_t)s[i]);
            uint8_t bp = u8px((uint8_t)s[i]);
            if (px + bp > PIXEL_LINE_W && px > 0) {
                lineSpans[totalLines++] = { lineStart, (uint16_t)(i - lineStart) };
                lineStart = i; px = 0;
                if (totalLines >= MAX_LINES) return;
            }
            px += bp; i += bl;
        }
        if (i >= len && lineStart < len && totalLines < MAX_LINES)
            lineSpans[totalLines++] = { lineStart, (uint16_t)(i - lineStart) };
        if (i >= len) break;
    }
}

String getLine(int idx) {
    if (idx < 0 || idx >= totalLines) return "";
    return currentMsg.substring(lineSpans[idx].start,
                                lineSpans[idx].start + lineSpans[idx].len);
}


// ══ 렌더링 ══════════════════════════════════
void renderClaude() {
    uint16_t hc = needsConfirm ? C_CONFIRM : C_NOTIFY;

    tft.fillScreen(C_BG);

    // 헤더
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, hc);
    tft.setTextColor(TFT_WHITE, hc);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("[ Claude ]", MARGIN_X, HEADER_H / 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(needsConfirm ? "CONFIRM" : "NOTIFY", SCREEN_W - MARGIN_X, HEADER_H / 2);

    tft.drawFastHLine(0, DIV_TOP_Y, SCREEN_W, C_DIM);
    tft.drawFastHLine(0, DIV_BOT_Y, SCREEN_W, C_DIM);

    // 콘텐츠
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < MAX_VIS_LINES && scrollOffset + i < totalLines; i++)
        tft.drawString(getLine(scrollOffset + i), MARGIN_X, CONTENT_Y0 + i * LINE_H);

    // 스크롤 화살표
    tft.setTextColor(C_DIM, C_BG);
    if (scrollOffset > 0)
        tft.drawString("^", SCREEN_W - MARGIN_X - 8, CONTENT_Y0);
    if (scrollOffset + MAX_VIS_LINES < totalLines)
        tft.drawString("v", SCREEN_W - MARGIN_X - 8, DIV_BOT_Y - LINE_H);

    // 풋터
    tft.setTextDatum(ML_DATUM);
    if (needsConfirm) {
        tft.drawString("[SW] Confirm", MARGIN_X, SCREEN_H - FOOTER_H / 2);
        tft.setTextDatum(MR_DATUM);
        tft.drawString("JOY scroll", SCREEN_W - MARGIN_X, SCREEN_H - FOOTER_H / 2);
    } else {
        tft.drawString("auto 3s", MARGIN_X, SCREEN_H - FOOTER_H / 2);
    }
}

void renderWaiting() {
    tft.fillScreen(C_BG);
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Waiting for Claude Code...", SCREEN_W / 2, SCREEN_H / 2 - 16);
    tft.setTextColor(C_GIF_HDR, C_BG);
    tft.drawString("JOY left/right  :  GIF mode", SCREEN_W / 2, SCREEN_H / 2 + 12);
}

void renderGifList() {
    tft.fillScreen(C_BG);

    // 헤더
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_GIF_HDR);
    tft.setTextColor(TFT_WHITE, C_GIF_HDR);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("[ GIF List ]", MARGIN_X, HEADER_H / 2);
    tft.setTextDatum(MR_DATUM);
    String info = String(gifCount > 0 ? currentGifIndex + 1 : 0) + "/" + String(gifCount);
    tft.drawString(info, SCREEN_W - MARGIN_X, HEADER_H / 2);

    tft.drawFastHLine(0, DIV_TOP_Y, SCREEN_W, C_DIM);
    tft.drawFastHLine(0, DIV_BOT_Y, SCREEN_W, C_DIM);

    if (gifCount == 0) {
        tft.setTextColor(C_DIM, C_BG);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No GIF files in SPIFFS", SCREEN_W / 2, SCREEN_H / 2);
        return;
    }

    int avail = DIV_BOT_Y - DIV_TOP_Y;
    int itemH = avail / GIF_LIST_VISIBLE;

    for (int i = 0; i < GIF_LIST_VISIBLE; i++) {
        int idx = gifListOffset + i;
        if (idx >= gifCount) break;
        int y = DIV_TOP_Y + i * itemH;
        bool sel = (idx == currentGifIndex);
        tft.fillRect(0, y + 1, SCREEN_W, itemH - 1, sel ? C_SEL : C_BG);
        tft.setTextColor(TFT_WHITE, sel ? C_SEL : C_BG);
        tft.setTextDatum(ML_DATUM);

        // 파일명에서 경로·확장자 제거
        String name = String(gifFiles[idx]);
        int sl = name.lastIndexOf('/');
        if (sl >= 0) name = name.substring(sl + 1);
        int dt = name.lastIndexOf('.');
        if (dt > 0) name = name.substring(0, dt);

        tft.drawString(name, MARGIN_X, y + itemH / 2);
    }

    tft.setTextColor(C_DIM, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("[SW] Select  [JOY] Scroll  [LR] Cancel", SCREEN_W / 2, SCREEN_H - FOOTER_H / 2);
}


// ══ 시리얼 처리 ════════════════════════════
void processCmd(const String &cmd) {
    returnMode = currentMode;   // 알림 후 돌아갈 모드 저장

    if (cmd.startsWith("CONFIRM:")) {
        splitLines(cmd.substring(8));
        needsConfirm = true;
        notifyActive = false;
        closeGif();
        currentMode = MODE_CLAUDE;
        renderClaude();

    } else if (cmd.startsWith("NOTIFY:")) {
        splitLines(cmd.substring(7));
        needsConfirm = false;
        notifyActive = true;
        notifyStart  = millis();
        closeGif();
        currentMode = MODE_CLAUDE;
        renderClaude();

    } else if (cmd == "CLEAR") {
        needsConfirm = notifyActive = false;
        currentMsg = ""; totalLines = 0;
        currentMode = MODE_CLAUDE;
        renderWaiting();
    }
}

void checkSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            if (inputBuffer.length()) { processCmd(inputBuffer); inputBuffer = ""; }
        } else if (c != '\r' && inputBuffer.length() < 250) {
            inputBuffer += c;
        }
    }
}


// ══ 조이스틱 ════════════════════════════════
void readJoy() {
    int vy = analogRead(JOY_VRY), vx = analogRead(JOY_VRX);
    joyY = (vy < JOY_LOW) ? -1 : (vy > JOY_HIGH) ? 1 : 0;
    joyX = (vx < JOY_LOW) ? -1 : (vx > JOY_HIGH) ? 1 : 0;

    swPrev = swNow;
    bool raw = (digitalRead(JOY_SW) == LOW);
    if (raw != swNow && millis() - swEdge > DEBOUNCE_MS) {
        swNow = raw; swEdge = millis();
    }
}

// Y 방향 자동 반복 트리거 여부
bool yTriggered() {
    if (joyY == 0) { lastYDir = 0; joyHeldSince = 0; return false; }
    uint32_t now = millis();
    if (joyY != lastYDir) {
        lastYDir = joyY; joyHeldSince = now; lastRepeat = now;
        return true;
    }
    if (now - joyHeldSince > JOY_REPEAT_DELAY && now - lastRepeat > JOY_REPEAT_INTERVAL) {
        lastRepeat = now; return true;
    }
    return false;
}

void handleJoy() {
    readJoy();
    bool swReleased = (!swNow && swPrev);

    switch (currentMode) {

    case MODE_CLAUDE:
        // ↑↓ 스크롤
        if (yTriggered()) {
            int n = constrain(scrollOffset + joyY, 0, max(0, totalLines - MAX_VIS_LINES));
            if (n != scrollOffset) { scrollOffset = n; renderClaude(); }
        }
        // SW: CONFIRM 승인
        if (swReleased && needsConfirm) {
            Serial.println("OK");
            needsConfirm = notifyActive = false;
            currentMsg = ""; totalLines = 0;
            if (returnMode == MODE_GIF_PLAY && gifCount > 0) {
                currentMode = MODE_GIF_PLAY;
                openGif(currentGifIndex);
                tft.fillScreen(C_BG);
            } else {
                currentMode = MODE_CLAUDE;
                renderWaiting();
            }
        }
        // ←→: GIF 모드로
        if (joyX != 0 && lastXDir == 0) {
            lastXDir = joyX;
            if (gifCount > 0) {
                currentMode = MODE_GIF_PLAY;
                openGif(currentGifIndex);
                tft.fillScreen(C_BG);
            }
        } else if (joyX == 0) lastXDir = 0;
        break;

    case MODE_GIF_PLAY:
        // ←→: Claude 모드로
        if (joyX != 0 && lastXDir == 0) {
            lastXDir = joyX;
            closeGif();
            currentMode = MODE_CLAUDE;
            currentMsg = ""; totalLines = 0;
            renderWaiting();
        } else if (joyX == 0) lastXDir = 0;
        // SW: 목록 열기
        if (swReleased) {
            closeGif();
            gifListOffset = constrain(currentGifIndex - GIF_LIST_VISIBLE / 2,
                                      0, max(0, gifCount - GIF_LIST_VISIBLE));
            currentMode = MODE_GIF_LIST;
            renderGifList();
        }
        break;

    case MODE_GIF_LIST:
        // ↑↓ 탐색
        if (yTriggered()) {
            int n = constrain(currentGifIndex + joyY, 0, gifCount - 1);
            if (n != currentGifIndex) {
                currentGifIndex = n;
                if (currentGifIndex < gifListOffset)
                    gifListOffset = currentGifIndex;
                if (currentGifIndex >= gifListOffset + GIF_LIST_VISIBLE)
                    gifListOffset = currentGifIndex - GIF_LIST_VISIBLE + 1;
                renderGifList();
            }
        }
        // SW: 선택 후 재생
        if (swReleased) {
            currentMode = MODE_GIF_PLAY;
            openGif(currentGifIndex);
            tft.fillScreen(C_BG);
        }
        // ←→: 취소, 재생으로 복귀
        if (joyX != 0 && lastXDir == 0) {
            lastXDir = joyX;
            currentMode = MODE_GIF_PLAY;
            openGif(currentGifIndex);
            tft.fillScreen(C_BG);
        } else if (joyX == 0) lastXDir = 0;
        break;
    }
}


// ══ NOTIFY 타임아웃 ═════════════════════════
void handleTimeout() {
    if (!notifyActive || millis() - notifyStart < 3000UL) return;
    notifyActive = false;
    currentMsg = ""; totalLines = 0;

    if (returnMode == MODE_GIF_PLAY && gifCount > 0) {
        currentMode = MODE_GIF_PLAY;
        openGif(currentGifIndex);
        tft.fillScreen(C_BG);
    } else {
        currentMode = MODE_CLAUDE;
        renderWaiting();
    }
}


// ══ setup / loop ════════════════════════════
void setup() {
    Serial.begin(9600);
    pinMode(JOY_SW, INPUT_PULLUP);
    analogSetAttenuation(ADC_11db);   // GPIO34/35: 0~3.3V 풀레인지

    tft.init();
    tft.setRotation(1);               // 가로 320×240
    tft.fillScreen(C_BG);

    if (SPIFFS.begin(true)) {
        // 한글 VLW 폰트 로드 (없으면 내장 Font2 사용)
        if (SPIFFS.exists("/korean.vlw")) {
            tft.loadFont("korean", SPIFFS);
            fontLoaded = true;
        }
        scanGifFiles();
    }
    if (!fontLoaded) tft.setTextFont(2);
    tft.setTextSize(1);

    gif.begin(BIG_ENDIAN_PIXELS);

    renderWaiting();
}

void loop() {
    checkSerial();
    handleJoy();
    handleTimeout();

    if (currentMode == MODE_GIF_PLAY && gifIsOpen) {
        int frameDelay = 0;
        if (!gif.playFrame(false, &frameDelay)) gif.reset();
        if (frameDelay > 0) {
            uint32_t deadline = millis() + (uint32_t)frameDelay;
            while (millis() < deadline) {
                checkSerial();
                if (currentMode != MODE_GIF_PLAY) break;
                delay(5);
            }
        }
    }
}

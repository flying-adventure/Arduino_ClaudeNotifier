#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define I2C_ADDR_PRIMARY  0x3C
#define I2C_ADDR_FALLBACK 0x3D

#define BUTTON_PIN 2
#define DEBOUNCE_MS 50

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String inputBuffer = "";
bool waitingForConfirm = false;
unsigned long lastButtonTime = 0;

void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // I2C 주소 0x3C 먼저 시도, 실패하면 0x3D
  if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_PRIMARY)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_FALLBACK)) {
      // 둘 다 실패 시 무한 루프 (I2C 배선 확인 필요)
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
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
      }
      inputBuffer = "";
    } else if (c != '\r') {
      inputBuffer += c;
    }
  }

  // confirm 모드에서 버튼 감지
  if (waitingForConfirm && digitalRead(BUTTON_PIN) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonTime > DEBOUNCE_MS) {
      lastButtonTime = now;
      waitingForConfirm = false;
      Serial.println("OK");
      // 버튼 뗄 때까지 대기
      while (digitalRead(BUTTON_PIN) == LOW);
      delay(100);
      showReady();
    }
  }
}

void processCommand(String cmd) {
  if (cmd.startsWith("CONFIRM:")) {
    showMessage(cmd.substring(8), true);
    waitingForConfirm = true;
  } else if (cmd.startsWith("NOTIFY:")) {
    showMessage(cmd.substring(7), false);
    waitingForConfirm = false;
  } else if (cmd == "CLEAR") {
    waitingForConfirm = false;
    showReady();
  }
}

void showMessage(String msg, bool needsConfirm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 헤더
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("[ Claude ]"));
  if (needsConfirm) {
    display.setCursor(72, 0);
    display.print(F("CONFIRM"));
  }
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  // 메시지 (최대 72자)
  if (msg.length() > 72) {
    msg = msg.substring(0, 69) + "...";
  }
  display.setCursor(0, 14);
  display.setTextWrap(true);
  display.println(msg);

  // confirm 모드 하단 안내
  if (needsConfirm) {
    display.drawFastHLine(0, 52, SCREEN_WIDTH, SSD1306_WHITE);
    display.setCursor(0, 55);
    display.print(F("[BTN] or [Enter]"));
  }

  display.display();
}

void showReady() {
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

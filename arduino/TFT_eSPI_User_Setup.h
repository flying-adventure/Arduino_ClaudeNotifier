// TFT_eSPI 라이브러리 설정 참고 파일
// 이 파일의 내용을 TFT_eSPI 라이브러리의 User_Setup.h에 복사하세요.
// 위치 예시: C:\Users\USER\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h

// ST7789 드라이버 선택 (다른 #define USER_SETUP_INFO 라인은 주석 처리)
#define ST7789_DRIVER

// 물리 해상도 (세로 기준, setRotation(1)으로 가로 전환)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ESP32 핀 번호
#define TFT_MOSI 23   // SPI MOSI
#define TFT_SCLK 18   // SPI Clock
#define TFT_CS   15   // Chip Select
#define TFT_DC    2   // Data/Command
#define TFT_RST   4   // Reset
// BLK (백라이트) → 3.3V 직결 (항상 ON)

// 내장 폰트 포함
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT   // VLW 한글 폰트 로드에 필요

// SPI 속도
#define SPI_FREQUENCY      40000000
#define SPI_READ_FREQUENCY 20000000

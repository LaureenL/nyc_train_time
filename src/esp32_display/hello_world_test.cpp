#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <SPI.h>

// Waveshare ESP32 Driver Board mapping:
// BUSY -> 25, RST -> 26, DC -> 27, CS -> 15, CLK -> 13, DIN -> 14

// 7.5" B/W 800x480.
// Try this first:
#define GxEPD2_DRIVER_CLASS GxEPD2_750_T7

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

GxEPD2_BW<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(
  GxEPD2_DRIVER_CLASS(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)
);

SPIClass hspi(HSPI);

const char HelloWorld[] = "Hello World!";

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("setup");

  // Required for Waveshare ESP32 e-Paper Driver Board.
  hspi.begin(13, 12, 14, 15); // SCK, MISO unused, MOSI, CS
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  display.init(115200, true, 2, false); // Waveshare "clever" reset circuit
  helloWorld();
  display.hibernate();
}

void helloWorld()
{
  display.setRotation(1);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(HelloWorld, 0, 0, &tbx, &tby, &tbw, &tbh);

  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;

  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(HelloWorld);
  }
  while (display.nextPage());
}

void loop() {};
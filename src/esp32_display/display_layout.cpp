#include <GxEPD2.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic24pt7b.h>
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

const char TestDate[] = "Sun, May 24";
const char TestTime[] = "1:57:45pm";

struct TrainArrival {
  int minutes;
  const char* time;
  const char* ampm;
};

TrainArrival arrivals[] = {
  {3, "2:25:05", "pm"},
  {8, "2:30:30", "pm"},
  {16, "2:38:16", "am"},
  {244, "12:46:59", "am"}
};

void display_upcoming_trains();
void drawHeader();
void drawTrainBadge(const char* route, int centerX, int centerY, int radius);
void drawTrainRow(int y, int minutes, const char* arrivalTime, const char* ampm);
void drawCenteredText(const char* text, int centerX, int centerY);
void drawRightAlignedText(const char* text, int rightX, int baselineY);
int textWidth(const char* text, int baselineY);
void drawMinutes(int minutes, int rightX, int baselineY);

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
  display_upcoming_trains();
  display.hibernate();
}

void display_upcoming_trains()
{
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);

    drawHeader();
    drawTrainBadge("C", 125, 260, 58);

    for (int i = 0; i < 4; i++) {
      drawTrainRow(160 + i * 72, arrivals[i].minutes, arrivals[i].time, arrivals[i].ampm);
    }

  }
  while (display.nextPage());
}

void drawHeader()
{
  const int headerBottom = 86;
  const int margin = 32;
  const int baselineY = 58;

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  display.setCursor(margin, baselineY);
  display.print(TestDate);
  drawRightAlignedText(TestTime, display.width() - margin, baselineY);

  display.drawLine(0, headerBottom, display.width(), headerBottom, GxEPD_BLACK);
}

void drawTrainBadge(const char* route, int centerX, int centerY, int radius)
{
  display.fillCircle(centerX, centerY, radius, GxEPD_BLACK);

  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawCenteredText(route, centerX, centerY + 1);
  display.setTextColor(GxEPD_BLACK);
}

void drawTrainRow(int y, int minutes, const char* arrivalTime, const char* ampm)
{
  const int minutesRightX = 330;
  const int minLabelX = 350;
  const int timeX = 455;
  const int ampmgap = 5;

  display.setTextColor(GxEPD_BLACK);

  drawMinutes(minutes, minutesRightX, y);

  display.setFont(&FreeSerifBoldItalic9pt7b);
  display.setCursor(minLabelX, y - 8);
  display.print("min");

  display.setFont(&FreeSerifBoldItalic24pt7b);
  display.setCursor(timeX, y);
  display.print(arrivalTime);
  int timeWidth = textWidth(arrivalTime, y);

  display.setFont(&FreeSerifBoldItalic9pt7b);
  display.setCursor(timeX + timeWidth + ampmGap, y);
  display.print(ampm);
}

void drawCenteredText(const char* text, int centerX, int centerY)
{
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

  int x = centerX - (tbw / 2) - tbx;
  int y = centerY - (tbh / 2) - tby;

  display.setCursor(x, y);
  display.print(text);
}

void drawRightAlignedText(const char* text, int rightX, int baselineY)
{
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);

  display.setCursor(rightX - tbw - tbx, baselineY);
  display.print(text);
}

int textWidth(const char* text, int baselineY)
{
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);
  return tbw + tbx;
}

void drawMinutes(int minutes, int rightX, int baselineY)
{
  char minutesText[4];
  snprintf(minutesText, sizeof(minutesText), "%d", minutes);

  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawRightAlignedText(minutesText, rightX, baselineY);
}

void loop()
{
}
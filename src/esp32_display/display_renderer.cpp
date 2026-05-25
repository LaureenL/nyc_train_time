#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic24pt7b.h>
#include <SPI.h>

#include "display_renderer.h"
#include "time_utils.h"

// Waveshare ESP32 Driver Board mapping:
// BUSY -> 25, RST -> 26, DC -> 27, CS -> 15, CLK -> 13, DIN -> 14
#define GxEPD2_DRIVER_CLASS GxEPD2_750_T7

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

static const int HEADER_TIME_WINDOW_X = 480;
static const int HEADER_TIME_WINDOW_Y = 0;
static const int HEADER_TIME_WINDOW_W = 320;
static const int HEADER_TIME_WINDOW_H = 86;

GxEPD2_BW<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(
  GxEPD2_DRIVER_CLASS(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)
);

SPIClass hspi(HSPI);
char lastHeaderTime[16] = "";

static void drawHeader(time_t currentTime);
static void drawHeaderTime(time_t currentTime);
static void drawTrainBadge(const char* route, int centerX, int centerY, int radius);
static void drawTrainRow(int y, const TrainArrival &arrival);
static void drawCenteredText(const char* text, int centerX, int centerY);
static void drawRightAlignedText(const char* text, int rightX, int baselineY);
static int textWidth(const char* text, int baselineY);
static void drawMinutes(int minutes, int rightX, int baselineY);

void setupDisplay() {
    hspi.begin(13, 12, 14, 15); // SCK, MISO unused, MOSI, CS
    display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    display.init(115200, true, 2, false); // Waveshare "clever" reset circuit
}

void refreshHeaderTimePartial(time_t currentTime) {
  char timeText[16];
  formatTime(currentTime, timeText, sizeof(timeText));
  if (strcmp(timeText, lastHeaderTime) == 0) {
    return;
  }

  Serial.print("Partial header time refresh: ");
  Serial.println(timeText);

  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setPartialWindow(HEADER_TIME_WINDOW_X, HEADER_TIME_WINDOW_Y, HEADER_TIME_WINDOW_W, HEADER_TIME_WINDOW_H);
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);
    drawHeaderTime(currentTime);
  }
  while (display.nextPage());

  strncpy(lastHeaderTime, timeText, sizeof(lastHeaderTime) - 1);
  lastHeaderTime[sizeof(lastHeaderTime) - 1] = '\0';
}

void renderArrivalsToDisplay(const DecodeStats &stats) {
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);
    drawHeader(stats.currentTime);

    if (stats.storedArrivalCount == 0) {
      display.setFont(&FreeSerifBoldItalic24pt7b);
      display.setCursor(80, 250);
      display.print("No upcoming trains");
    }
    else {
      for (size_t i = 0; i < stats.storedArrivalCount; i++) {
        drawTrainRow(150 + i * 78, stats.arrivals[i]);
      }
    }
  }
  while (display.nextPage());

  formatTime(stats.currentTime, lastHeaderTime, sizeof(lastHeaderTime));
}

static void drawHeader(time_t currentTime) {
  const int headerBottom = 86;
  const int margin = 32;
  const int baselineY = 58;

  char dateText[24];
  formatDate(currentTime, dateText, sizeof(dateText));

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  display.setCursor(margin, baselineY);
  display.print(dateText);
  drawHeaderTime(currentTime);

  display.drawLine(0, headerBottom, display.width(), headerBottom, GxEPD_BLACK);
}

static void drawHeaderTime(time_t currentTime) {
  const int margin = 32;
  const int baselineY = 58;

  char timeText[16];
  formatTime(currentTime, timeText, sizeof(timeText));

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawRightAlignedText(timeText, display.width() - margin, baselineY);
}

static void drawTrainBadge(const char* route, int centerX, int centerY, int radius) {
  display.fillCircle(centerX, centerY, radius, GxEPD_BLACK);

  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawCenteredText(route, centerX, centerY + 1);
  display.setTextColor(GxEPD_BLACK);
}

static void drawTrainRow(int y, const TrainArrival &arrival) {
  const int badgeCenterX = 95;
  const int minutesRightX = 320;
  const int minLabelX = 340;
  const int timeX = 455;
  const int ampmGap = 5;
  const int rightMargin = 32;

  char arrivalTime[16];
  char arrivalAmPm[4];
  formatArrivalTime(arrival.arrivalTime, arrivalTime, sizeof(arrivalTime), arrivalAmPm, sizeof(arrivalAmPm));

  drawTrainBadge(arrival.line, badgeCenterX, y - 15, 34);
  drawMinutes(arrival.secondsUntilArrival / 60, minutesRightX, y);

  display.setFont(&FreeSerifBoldItalic9pt7b);
  display.setCursor(minLabelX, y - 8);
  display.print("min");

  display.setFont(&FreeSerifBoldItalic24pt7b);
  int arrivalTimeWidth = textWidth(arrivalTime, y);
  display.setFont(&FreeSerifBoldItalic9pt7b);
  int ampmWidth = textWidth(arrivalAmPm, y);
  int adjustedTimeX = timeX;
  int rightEdge = adjustedTimeX + arrivalTimeWidth + ampmGap + ampmWidth;
  int maxRightEdge = display.width() - rightMargin;
  if (rightEdge > maxRightEdge) {
    adjustedTimeX -= rightEdge - maxRightEdge;
  }

  display.setFont(&FreeSerifBoldItalic24pt7b);
  display.setCursor(adjustedTimeX, y);
  display.print(arrivalTime);

  display.setFont(&FreeSerifBoldItalic9pt7b);
  display.setCursor(adjustedTimeX + arrivalTimeWidth + ampmGap, y);
  display.print(arrivalAmPm);
}

static void drawCenteredText(const char* text, int centerX, int centerY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

  int x = centerX - (tbw / 2) - tbx;
  int y = centerY - (tbh / 2) - tby;

  display.setCursor(x, y);
  display.print(text);
}

static void drawRightAlignedText(const char* text, int rightX, int baselineY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);

  display.setCursor(rightX - tbw - tbx, baselineY);
  display.print(text);
}

static int textWidth(const char* text, int baselineY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);
  return tbw + tbx;
}

static void drawMinutes(int minutes, int rightX, int baselineY) {
  char minutesText[4];
  snprintf(minutesText, sizeof(minutesText), "%d", minutes);

  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawRightAlignedText(minutesText, rightX, baselineY);
}

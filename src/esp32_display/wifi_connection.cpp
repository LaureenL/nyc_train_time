#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic24pt7b.h>
#include <SPI.h>
#include <time.h>
#include "mta_cert.h"
#include "secrets.h"
#include <MtaGtfsNanopb.h>

// Waveshare ESP32 Driver Board mapping:
// BUSY -> 25, RST -> 26, DC -> 27, CS -> 15, CLK -> 13, DIN -> 14
#define GxEPD2_DRIVER_CLASS GxEPD2_750_T7

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

const char* MTA_HOST = "api-endpoint.mta.info";
const char* MTA_PATH = "/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
const char* NEW_YORK_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";
const int HTTPS_PORT = 443;
const char* TARGET_ROUTES[] = {"A", "C"};
const size_t TARGET_ROUTE_COUNT = sizeof(TARGET_ROUTES) / sizeof(TARGET_ROUTES[0]);
const size_t MAX_ARRIVALS = 4;
const size_t ROUTE_ID_SIZE = 8;
const size_t STOP_ID_SIZE = 16;
const unsigned long REFRESH_INTERVAL_MS = 60000;
const unsigned long CLOCK_REFRESH_INTERVAL_MS = 10000;
const int HEADER_TIME_WINDOW_X = 480;
const int HEADER_TIME_WINDOW_Y = 0;
const int HEADER_TIME_WINDOW_W = 320;
const int HEADER_TIME_WINDOW_H = 86;

GxEPD2_BW<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(
  GxEPD2_DRIVER_CLASS(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)
);

SPIClass hspi(HSPI);
unsigned long lastRefreshMs = 0;
unsigned long lastClockRefreshMs = 0;
char lastHeaderTime[16] = "";

void connectWifi();
void syncClock();
void setupDisplay();
void refreshTrainDisplay();
void refreshHeaderTimePartial(time_t currentTime);
bool fetchAndParseArrivals(struct DecodeStats *stats);
bool readFromClient(pb_istream_t *stream, pb_byte_t *buffer, size_t count);
bool decodeFeedEntity(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool decodeString(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool decodeStopTimeUpdate(pb_istream_t *stream, const pb_field_t *field, void **arg);
bool isTargetRoute(const char *routeId);
void sortArrivals(struct DecodeStats *stats);
void printArrivals(const struct DecodeStats &stats, time_t currentTime);
void renderArrivalsToDisplay(const struct DecodeStats &stats);
void drawHeader(time_t currentTime);
void drawHeaderTime(time_t currentTime);
void drawTrainBadge(const char* route, int centerX, int centerY, int radius);
void drawTrainRow(int y, const struct TrainArrival &arrival);
void drawCenteredText(const char* text, int centerX, int centerY);
void drawRightAlignedText(const char* text, int rightX, int baselineY);
int textWidth(const char* text, int baselineY);
void drawMinutes(int minutes, int rightX, int baselineY);
void formatTime(time_t timestamp, char *buffer, size_t bufferSize);
void formatDate(time_t timestamp, char *buffer, size_t bufferSize);
void formatArrivalTime(time_t timestamp, char *timeBuffer, size_t timeBufferSize, char *ampmBuffer, size_t ampmBufferSize);

struct TrainArrival {
    char line[ROUTE_ID_SIZE];
    int64_t arrivalTime;
    int32_t secondsUntilArrival;
};

struct DecodeStats {
    uint32_t entityCount;
    uint32_t tripUpdateCount;
    uint32_t matchingArrivalCount;
    uint32_t storedArrivalCount;
    time_t currentTime;
    TrainArrival arrivals[MAX_ARRIVALS];
};

struct StopTimeUpdateContext {
    DecodeStats *stats;
    const char *routeId;
};

struct StringDecodeContext {
    char *buffer;
    size_t capacity;
};

void setup(){
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Booting...");
  setupDisplay();
  connectWifi();
  if (WiFi.status() == WL_CONNECTED)
  {
    syncClock();
    refreshTrainDisplay();
    lastRefreshMs = millis();
    lastClockRefreshMs = millis();
  }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWifi();
    }

    if (WiFi.status() == WL_CONNECTED && millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
        refreshTrainDisplay();
        lastRefreshMs = millis();
        lastClockRefreshMs = millis();
    }
    else if (WiFi.status() == WL_CONNECTED && millis() - lastClockRefreshMs >= CLOCK_REFRESH_INTERVAL_MS) {
        refreshHeaderTimePartial(time(nullptr));
        lastClockRefreshMs = millis();
    }
}

void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);

    int tries = 60;
    while (WiFi.status() != WL_CONNECTED && tries-- > 0)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println();
        Serial.println("WiFi connect timeout");
    }
}

void syncClock() {
    configTzTime(NEW_YORK_TZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", NEW_YORK_TZ, 1);
    tzset();

    Serial.print("Waiting for NTP time sync");
    time_t now = time(nullptr);
    int tries = 30;
    while (now < 8 * 3600 * 2 && tries-- > 0)
    {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }

    Serial.println();
    if (now < 8 * 3600 * 2)
    {
        Serial.println("NTP sync failed; HTTPS certificate validation may fail");
        return;
    }

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    Serial.print("Current local time: ");
    Serial.print(asctime(&timeinfo));
}

void setupDisplay() {
    hspi.begin(13, 12, 14, 15); // SCK, MISO unused, MOSI, CS
    display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    display.init(115200, true, 2, false); // Waveshare "clever" reset circuit
}

void refreshTrainDisplay() {
    DecodeStats stats = {};
    stats.currentTime = time(nullptr);

    Serial.println();
    Serial.println("Refreshing train display");

    if (fetchAndParseArrivals(&stats)) {
        sortArrivals(&stats);
        printArrivals(stats, stats.currentTime);
    }
    else {
        Serial.println("Using empty arrival state due to fetch/decode failure");
    }

    renderArrivalsToDisplay(stats);
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

bool fetchAndParseArrivals(DecodeStats *stats) {
    WiFiClientSecure client;
    client.setCACert(MTA_ROOT_CA);

    Serial.print("Connecting to ");
    Serial.println(MTA_HOST);

    if (!client.connect(MTA_HOST, HTTPS_PORT))
    {
        Serial.println("MTA feed connection failed");
        return false;
    }

    Serial.print("Requesting ");
    Serial.println(MTA_PATH);

    client.print(String("GET ") + MTA_PATH + " HTTP/1.1\r\n" +
                 "Host: " + MTA_HOST + "\r\n" +
                 "User-Agent: nyc-train-time-esp32\r\n" +
                 "Accept: application/x-google-protobuf\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long start = millis();
    while (!client.available() && millis() - start < 10000)
    {
        delay(100);
    }

    if (!client.available())
    {
        Serial.println("Timed out waiting for MTA response");
        client.stop();
        return false;
    }

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    Serial.print("MTA response: ");
    Serial.println(statusLine);

    if (!statusLine.startsWith("HTTP/1.1 200") && !statusLine.startsWith("HTTP/1.0 200"))
    {
        Serial.println("MTA feed smoke test failed");
        client.stop();
        return false;
    }

    Serial.println("MTA feed smoke test passed");

    size_t contentLength = 0;
    while (client.connected()) {
        String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r") {
            break;
        }

        headerLine.trim();
        if (headerLine.startsWith("Content-Length:") || headerLine.startsWith("content-length:")) {
            contentLength = headerLine.substring(headerLine.indexOf(':') + 1).toInt();
        }
    }

    if (contentLength == 0) {
        Serial.println("Missing Content-Length; cannot stream decode feed");
        client.stop();
        return false;
    }

    Serial.print("Protobuf body bytes: ");
    Serial.println(contentLength);

    transit_realtime_FeedMessage feed = transit_realtime_FeedMessage_init_zero;
    feed.entity.funcs.decode = decodeFeedEntity;
    feed.entity.arg = stats;

    pb_istream_t stream = {readFromClient, &client, contentLength, 0};

    bool ok = pb_decode(&stream, transit_realtime_FeedMessage_fields, &feed);

    if (!ok) {
        Serial.print("Decode failed: ");
        Serial.println(PB_GET_ERROR(&stream));
        client.stop();
        return false;
    }

    Serial.println("Decoded feed successfully");
    Serial.print("Feed entities: ");
    Serial.println(stats->entityCount);
    Serial.print("Trip updates: ");
    Serial.println(stats->tripUpdateCount);

    client.stop();
    return true;
}

bool readFromClient(pb_istream_t *stream, pb_byte_t *buffer, size_t count) {
  WiFiClientSecure *client = (WiFiClientSecure *)stream->state;
  size_t bytesRead = 0;
  unsigned long start = millis();

  while (bytesRead < count) {
    if (client->available()) {
      int value = client->read();
      if (value < 0) {
        return false;
      }

      if (buffer != nullptr) {
        buffer[bytesRead] = (pb_byte_t)value;
      }
      bytesRead++;
      start = millis();
    }
    else if (!client->connected()) {
      return false;
    }
    else if (millis() - start > 10000) {
      return false;
    }
    else {
      delay(1);
    }
  }

  return true;
}

bool decodeFeedEntity(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  DecodeStats *stats = (DecodeStats *)(*arg);

  transit_realtime_FeedEntity entity = transit_realtime_FeedEntity_init_zero;
  char routeId[ROUTE_ID_SIZE] = "";
  StringDecodeContext routeContext = {routeId, sizeof(routeId)};
  StopTimeUpdateContext stopContext = {stats, routeId};

  entity.trip_update.trip.route_id.funcs.decode = decodeString;
  entity.trip_update.trip.route_id.arg = &routeContext;
  entity.trip_update.stop_time_update.funcs.decode = decodeStopTimeUpdate;
  entity.trip_update.stop_time_update.arg = &stopContext;

  if (!pb_decode(stream, transit_realtime_FeedEntity_fields, &entity)) {
    return false;
  }

  stats->entityCount++;

  if (entity.has_trip_update) {
    stats->tripUpdateCount++;
  }

  return true;
}

bool decodeString(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  StringDecodeContext *context = (StringDecodeContext *)(*arg);
  if (context == nullptr || context->buffer == nullptr || context->capacity == 0) {
    return false;
  }

  size_t bytesToRead = stream->bytes_left;
  if (bytesToRead >= context->capacity) {
    bytesToRead = context->capacity - 1;
  }

  if (!pb_read(stream, (pb_byte_t *)context->buffer, bytesToRead)) {
    return false;
  }
  context->buffer[bytesToRead] = '\0';

  while (stream->bytes_left > 0) {
    pb_byte_t ignored;
    if (!pb_read(stream, &ignored, 1)) {
      return false;
    }
  }

  return true;
}

bool decodeStopTimeUpdate(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  StopTimeUpdateContext *context = (StopTimeUpdateContext *)(*arg);
  char stopId[STOP_ID_SIZE] = "";
  StringDecodeContext stopContext = {stopId, sizeof(stopId)};

  transit_realtime_TripUpdate_StopTimeUpdate stopUpdate = transit_realtime_TripUpdate_StopTimeUpdate_init_zero;
  stopUpdate.stop_id.funcs.decode = decodeString;
  stopUpdate.stop_id.arg = &stopContext;

  if (!pb_decode(stream, transit_realtime_TripUpdate_StopTimeUpdate_fields, &stopUpdate)) {
    return false;
  }

  if (!isTargetRoute(context->routeId)) {
    return true;
  }

  if (!stopUpdate.has_arrival || !stopUpdate.arrival.has_time) {
    return true;
  }

  if (strcmp(stopId, MTA_TARGET_STOP_ID) != 0) {
    return true;
  }

  int64_t secondsUntilArrival = stopUpdate.arrival.time - context->stats->currentTime;
  if (secondsUntilArrival < 0) {
    return true;
  }

  TrainArrival arrival = {};
  strncpy(arrival.line, context->routeId, ROUTE_ID_SIZE - 1);
  arrival.line[ROUTE_ID_SIZE - 1] = '\0';
  arrival.arrivalTime = stopUpdate.arrival.time;
  arrival.secondsUntilArrival = secondsUntilArrival;

  if (context->stats->storedArrivalCount < MAX_ARRIVALS) {
    context->stats->arrivals[context->stats->storedArrivalCount++] = arrival;
  }
  else {
    size_t latestIndex = 0;
    for (size_t i = 1; i < MAX_ARRIVALS; i++) {
      if (context->stats->arrivals[i].arrivalTime > context->stats->arrivals[latestIndex].arrivalTime) {
        latestIndex = i;
      }
    }

    if (arrival.arrivalTime < context->stats->arrivals[latestIndex].arrivalTime) {
      context->stats->arrivals[latestIndex] = arrival;
    }
  }

  context->stats->matchingArrivalCount++;
  return true;
}

bool isTargetRoute(const char *routeId) {
  for (size_t i = 0; i < TARGET_ROUTE_COUNT; i++) {
    if (strcmp(routeId, TARGET_ROUTES[i]) == 0) {
      return true;
    }
  }

  return false;
}

void sortArrivals(DecodeStats *stats) {
  size_t count = stats->storedArrivalCount;

  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (stats->arrivals[j].arrivalTime < stats->arrivals[i].arrivalTime) {
        TrainArrival temp = stats->arrivals[i];
        stats->arrivals[i] = stats->arrivals[j];
        stats->arrivals[j] = temp;
      }
    }
  }
}

void printArrivals(const DecodeStats &stats, time_t currentTime) {
  struct tm timeinfo;
  localtime_r(&currentTime, &timeinfo);

  char currentTimeBuffer[24];
  strftime(currentTimeBuffer, sizeof(currentTimeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

  Serial.print("NOW: ");
  Serial.println(currentTimeBuffer);
  Serial.println();

  Serial.print("Matching arrivals: ");
  Serial.println(stats.matchingArrivalCount);

  size_t count = stats.storedArrivalCount;
  for (size_t i = 0; i < count; i++) {
    time_t arrivalTime = stats.arrivals[i].arrivalTime;
    struct tm arrivalTimeInfo;
    localtime_r(&arrivalTime, &arrivalTimeInfo);

    char arrivalTimeBuffer[24];
    strftime(arrivalTimeBuffer, sizeof(arrivalTimeBuffer), "%Y-%m-%d %H:%M:%S", &arrivalTimeInfo);

    float minutesUntilArrival = stats.arrivals[i].secondsUntilArrival / 60.0;
    int32_t intMinutesUntilArrival = stats.arrivals[i].secondsUntilArrival / 60;

    Serial.println("Stop Update: ");
    Serial.print("Line: ");
    Serial.println(stats.arrivals[i].line);
    Serial.print("Minutes until arrival: ");
    Serial.println(minutesUntilArrival);
    Serial.print("Floor of Minutes until arrival: ");
    Serial.println(intMinutesUntilArrival);
    Serial.print("ARRIVING AT: ");
    Serial.println(arrivalTimeBuffer);
    Serial.println("~~~~~~~~~~~~~");
    Serial.println();
  }
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

void drawHeader(time_t currentTime) {
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

void drawHeaderTime(time_t currentTime) {
  const int margin = 32;
  const int baselineY = 58;

  char timeText[16];
  formatTime(currentTime, timeText, sizeof(timeText));

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawRightAlignedText(timeText, display.width() - margin, baselineY);
}

void drawTrainBadge(const char* route, int centerX, int centerY, int radius) {
  display.fillCircle(centerX, centerY, radius, GxEPD_BLACK);

  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawCenteredText(route, centerX, centerY + 1);
  display.setTextColor(GxEPD_BLACK);
}

void drawTrainRow(int y, const TrainArrival &arrival) {
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

void drawCenteredText(const char* text, int centerX, int centerY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

  int x = centerX - (tbw / 2) - tbx;
  int y = centerY - (tbh / 2) - tby;

  display.setCursor(x, y);
  display.print(text);
}

void drawRightAlignedText(const char* text, int rightX, int baselineY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);

  display.setCursor(rightX - tbw - tbx, baselineY);
  display.print(text);
}

int textWidth(const char* text, int baselineY) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, baselineY, &tbx, &tby, &tbw, &tbh);
  return tbw + tbx;
}

void drawMinutes(int minutes, int rightX, int baselineY) {
  char minutesText[4];
  snprintf(minutesText, sizeof(minutesText), "%d", minutes);

  display.setFont(&FreeSerifBoldItalic24pt7b);
  drawRightAlignedText(minutesText, rightX, baselineY);
}

void formatTime(time_t timestamp, char *buffer, size_t bufferSize) {
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);
  int hour = timeinfo.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }

  snprintf(buffer, bufferSize, "%d:%02d:%02d%s", hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_hour < 12 ? "am" : "pm");
}

void formatDate(time_t timestamp, char *buffer, size_t bufferSize) {
  static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);

  const char *weekday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) ? WEEKDAYS[timeinfo.tm_wday] : "";
  const char *month = (timeinfo.tm_mon >= 0 && timeinfo.tm_mon < 12) ? MONTHS[timeinfo.tm_mon] : "";
  snprintf(buffer, bufferSize, "%s, %s %d", weekday, month, timeinfo.tm_mday);
}

void formatArrivalTime(time_t timestamp, char *timeBuffer, size_t timeBufferSize, char *ampmBuffer, size_t ampmBufferSize) {
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);
  int hour = timeinfo.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }

  snprintf(timeBuffer, timeBufferSize, "%d:%02d", hour, timeinfo.tm_min);
  snprintf(ampmBuffer, ampmBufferSize, "%s", timeinfo.tm_hour < 12 ? "am" : "pm");
}

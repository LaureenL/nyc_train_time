#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <MtaGtfsNanopb.h>

#include "mta_cert.h"
#include "mta_feed.h"

static const char* MTA_HOST = "api-endpoint.mta.info";
static const char* MTA_PATH = "/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
static const int HTTPS_PORT = 443;
static const char* TARGET_ROUTES[] = {"A", "C"};
static const size_t TARGET_ROUTE_COUNT = sizeof(TARGET_ROUTES) / sizeof(TARGET_ROUTES[0]);

struct StopTimeUpdateContext {
    DecodeStats *stats;
    const char *routeId;
    const char *targetStopId;
};

struct StringDecodeContext {
    char *buffer;
    size_t capacity;
};

static bool readFromClient(pb_istream_t *stream, pb_byte_t *buffer, size_t count);
static bool decodeFeedEntity(pb_istream_t *stream, const pb_field_t *field, void **arg);
static bool decodeString(pb_istream_t *stream, const pb_field_t *field, void **arg);
static bool decodeStopTimeUpdate(pb_istream_t *stream, const pb_field_t *field, void **arg);
static bool isTargetRoute(const char *routeId);

bool fetchAndParseArrivals(DecodeStats *stats, const char *targetStopId) {
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
    StopTimeUpdateContext feedContext = {stats, "", targetStopId};
    feed.entity.funcs.decode = decodeFeedEntity;
    feed.entity.arg = &feedContext;

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

static bool readFromClient(pb_istream_t *stream, pb_byte_t *buffer, size_t count) {
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

static bool decodeFeedEntity(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  StopTimeUpdateContext *feedContext = (StopTimeUpdateContext *)(*arg);

  transit_realtime_FeedEntity entity = transit_realtime_FeedEntity_init_zero;
  char routeId[ROUTE_ID_SIZE] = "";
  StringDecodeContext routeContext = {routeId, sizeof(routeId)};
  StopTimeUpdateContext stopContext = {feedContext->stats, routeId, feedContext->targetStopId};

  entity.trip_update.trip.route_id.funcs.decode = decodeString;
  entity.trip_update.trip.route_id.arg = &routeContext;
  entity.trip_update.stop_time_update.funcs.decode = decodeStopTimeUpdate;
  entity.trip_update.stop_time_update.arg = &stopContext;

  if (!pb_decode(stream, transit_realtime_FeedEntity_fields, &entity)) {
    return false;
  }

  feedContext->stats->entityCount++;

  if (entity.has_trip_update) {
    feedContext->stats->tripUpdateCount++;
  }

  return true;
}

static bool decodeString(pb_istream_t *stream, const pb_field_t *field, void **arg) {
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

static bool decodeStopTimeUpdate(pb_istream_t *stream, const pb_field_t *field, void **arg) {
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

  if (strcmp(stopId, context->targetStopId) != 0) {
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

static bool isTargetRoute(const char *routeId) {
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

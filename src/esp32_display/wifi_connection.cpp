#include <Arduino.h>
#include <time.h>

#include "display_renderer.h"
#include "mta_feed.h"
#include "secrets.h"
#include "time_utils.h"
#include "wifi_manager.h"

const unsigned long REFRESH_INTERVAL_MS = 60000;
const unsigned long CLOCK_REFRESH_INTERVAL_MS = 10000;

unsigned long lastRefreshMs = 0;
unsigned long lastClockRefreshMs = 0;
bool clockSynced = false;

void refreshTrainDisplay();

void setup(){
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Booting...");
  setupDisplay();
  connectWifi(WIFI_SSID, WIFI_PASSWORD);
  if (isWifiConnected())
  {
    clockSynced = syncClock();
    if (clockSynced) {
      refreshTrainDisplay();
      lastRefreshMs = millis();
      lastClockRefreshMs = millis();
    }
  }
}

void loop() {
    if (!isWifiConnected()) {
        connectWifi(WIFI_SSID, WIFI_PASSWORD);
    }

    if (isWifiConnected() && !clockSynced) {
        clockSynced = syncClock();
        if (clockSynced) {
            refreshTrainDisplay();
            lastRefreshMs = millis();
            lastClockRefreshMs = millis();
        }
        return;
    }

    if (isWifiConnected() && clockSynced && millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
        refreshTrainDisplay();
        lastRefreshMs = millis();
        lastClockRefreshMs = millis();
    }
    else if (isWifiConnected() && clockSynced && millis() - lastClockRefreshMs >= CLOCK_REFRESH_INTERVAL_MS) {
        refreshHeaderTimePartial(time(nullptr));
        lastClockRefreshMs = millis();
    }
}

void refreshTrainDisplay() {
    DecodeStats stats = {};
    stats.currentTime = time(nullptr);

    if (!isClockSynced()) {
        Serial.println("Skipping train refresh until clock is synced");
        clockSynced = false;
        return;
    }

    Serial.println();
    Serial.println("Refreshing train display");

    if (fetchAndParseArrivals(&stats, MTA_TARGET_STOP_ID)) {
        sortArrivals(&stats);
        printArrivals(stats, stats.currentTime);
    }
    else {
        Serial.println("Using empty arrival state due to fetch/decode failure");
    }

    renderArrivalsToDisplay(stats);
}

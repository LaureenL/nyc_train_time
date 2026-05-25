#include "time_utils.h"

static const char* NEW_YORK_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";

bool isClockSynced() {
    return time(nullptr) > 1700000000;
}

bool syncClock() {
    configTzTime(NEW_YORK_TZ, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", NEW_YORK_TZ, 1);
    tzset();

    Serial.print("Waiting for NTP time sync");
    struct tm timeinfo;
    int tries = 60;
    while (!getLocalTime(&timeinfo, 500) && tries-- > 0)
    {
        Serial.print(".");
    }

    Serial.println();
    if (!isClockSynced())
    {
        Serial.println("NTP sync failed; HTTPS certificate validation may fail");
        return false;
    }

    Serial.print("Current local time: ");
    Serial.print(asctime(&timeinfo));
    return true;
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

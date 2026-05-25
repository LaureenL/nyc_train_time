#pragma once

#include <Arduino.h>
#include <time.h>

bool syncClock();
bool isClockSynced();
void formatTime(time_t timestamp, char *buffer, size_t bufferSize);
void formatDate(time_t timestamp, char *buffer, size_t bufferSize);
void formatArrivalTime(time_t timestamp, char *timeBuffer, size_t timeBufferSize, char *ampmBuffer, size_t ampmBufferSize);

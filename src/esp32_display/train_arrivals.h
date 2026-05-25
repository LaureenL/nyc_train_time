#pragma once

#include <Arduino.h>
#include <time.h>

const size_t MAX_ARRIVALS = 4;
const size_t ROUTE_ID_SIZE = 8;
const size_t STOP_ID_SIZE = 16;

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

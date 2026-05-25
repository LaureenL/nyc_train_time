#pragma once

#include "train_arrivals.h"

bool fetchAndParseArrivals(struct DecodeStats *stats, const char *targetStopId);
void sortArrivals(struct DecodeStats *stats);
void printArrivals(const struct DecodeStats &stats, time_t currentTime);
